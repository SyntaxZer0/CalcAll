#include "calc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "parser.h"

#define DEFAULT_DIGITS 20
#define MAX_DIGITS 1000
#define GUARD_DIGITS 10   /* approximate values keep this many digits beyond those shown */

static const char HELP[] =
    "Operators:  + - * / % ^ !  and ( )\n"
    "Numbers:    42  3.14  .5  6.02e23    (exact: 0.1 + 0.2 = 0.3)\n"
    "Constants:  pi (or \xCF\x80)  e\n"
    "Sum:        sum(k=1, 100, k^2)        also written \xCE\xA3(k=1, 100, k^2)\n"
    "Product:    prod(k=1, 10, k)          also written \xCE\xA0(k=1, 10, k)\n"
    "Roots:      sqrt(x)  cbrt(x)  root(x, n)  x^(1/n)\n"
    "Logs:       ln(x)  log(x) (base 10)  log2(x)  log(x, base)  exp(x)\n"
    "Trig:       sin cos tan asin acos atan sinh cosh tanh\n"
    "Complex:    i  3+4i  re im conj arg abs  (sqrt(-4) = 2i)\n"
    "Other:      abs floor ceil round gcd lcm choose(n,k) perm(n,k)\n"
    "Variables:  x = 5   rate = 0.05   then use x, rate   (ans = previous result)\n"
    "Functions:  f(x) = x^2 + 1   g(a, b) = sqrt(a^2 + b^2)   then f(3), g(3, 4)\n"
    "Implicit:   2pi  3(4+1)  2sin(30)  mean 2*pi, 3*(4+1), 2*sin(30)\n"
    "Commands:   :deg  :rad  :sci  :fix  :digits N  :help  :quit\n"
    "Saving:     :vars  :clear [name]  :save [file]  :load file   (saved automatically)\n"
    "History:    :history [N]  :history clear   (kept between runs)\n"
    "Calculus:   d/dx(x^3, 2) = 12 (exact)   int(x=0, 1, x^2) = 1/3   int(sin(x), 0, pi)\n"
    "Checks:     2 + 2 = 4  \xE2\x86\x92 True    sqrt(2) > 1.5   x == 3   1 < x \xE2\x89\xA4 5   3 != 4 (or \xE2\x89\xA0)\n"
    "Tables:     :table x^2 + 1 from 1 to 5 step 1    (add ; g(x) after f(x) for two columns)\n"
    "\xE2\x89\x88 marks an approximate result; everything else is exact.";

static char *dup_str(const char *s)
{
    char *d = malloc(strlen(s) + 1);
    if (d)
        strcpy(d, s);
    return d;
}

void calc_init(Calc *c)
{
    c->digits = DEFAULT_DIGITS;
    c->sci = 0;
    c->degrees = 0;
    val_init(&c->ans);
    sym_init(&c->syms);
    c->result_is_value = 0;
    c->store = NULL;
    c->history = NULL;
    c->nhistory = 0;
    c->history_file = NULL;
}

void calc_free(Calc *c)
{
    val_free(&c->ans);
    sym_free(&c->syms);
    free(c->store);
    for (int i = 0; i < c->nhistory; i++) {
        free(c->history[i].input);
        free(c->history[i].result);
    }
    free(c->history);
    free(c->history_file);
}

/* Growable string for building messages. */
typedef struct {
    char *s;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 128;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *p = realloc(b->s, cap);
        if (!p)
            exit(1);
        b->s = p;
        b->cap = cap;
    }
    memcpy(b->s + b->len, s, n + 1);
    b->len += n;
}

/* "x = 5", or "x ≈ 1.414..." for an approximate value */
static void put_var(Buf *b, Calc *c, const char *name, const Value *v)
{
    char *s = val_to_str(v, c->digits, c->sci);
    put(b, name);
    if (strncmp(s, "\xE2\x89\x88 ", 4) == 0) {
        put(b, " \xE2\x89\x88 ");
        put(b, s + 4);
    } else {
        put(b, " = ");
        put(b, s);
    }
    free(s);
}

/* "f(a, b) = body" */
static void put_func(Buf *b, const UserFunc *f)
{
    put(b, f->name);
    put(b, "(");
    for (int i = 0; i < f->nparams; i++) {
        put(b, i ? ", " : "");
        put(b, f->params[i]);
    }
    put(b, ") = ");
    put(b, f->text);
}

static char *list_symbols(Calc *c)
{
    Buf b = {0};
    put(&b, "");
    for (int i = 0; i < c->syms.nvars; i++) {
        put(&b, b.len ? "\n" : "");
        put_var(&b, c, c->syms.vars[i].name, &c->syms.vars[i].value);
    }
    for (int i = 0; i < c->syms.nfuncs; i++) {
        put(&b, b.len ? "\n" : "");
        put_func(&b, &c->syms.funcs[i]);
    }
    if (b.len == 0)
        put(&b, "nothing defined yet (try x = 5 or f(x) = x^2)");
    return b.s;
}

/* ---- Saving definitions ------------------------------------------------------
 *
 * The file is plain text, one definition per line:
 *
 *     var x 1 1/3 0/1          name, exact (1) or approximate (0), real part,
 *                              imaginary part, as exact fractions
 *     fn f(x) = x^2 + 1        a function, exactly as defined
 */

static void put_rat(Buf *b, const Rational *r)
{
    char *n = bi_to_str(&r->num), *d = bi_to_str(&r->den);
    put(b, " ");
    put(b, n);
    put(b, "/");
    put(b, d);
    free(n);
    free(d);
}

/* Writes every definition to `path`. Returns how many, or -1 on failure. */
static int save_store(Calc *c, const char *path)
{
    Buf b = {0};
    put(&b, "# Calc definitions; written automatically\n");
    for (int i = 0; i < c->syms.nvars; i++) {
        const UserVar *v = &c->syms.vars[i];
        put(&b, "var ");
        put(&b, v->name);
        put(&b, v->value.exact ? " 1" : " 0");
        put_rat(&b, &v->value.r);
        put_rat(&b, &v->value.im);
        put(&b, "\n");
    }
    for (int i = 0; i < c->syms.nfuncs; i++) {
        put(&b, "fn ");
        put_func(&b, &c->syms.funcs[i]);
        put(&b, "\n");
    }

    /* Write a temporary file and rename it over the old one, so a crash
     * halfway through never leaves a half-written file. */
    size_t n = strlen(path) + 5;
    char *tmp = malloc(n);
    if (!tmp)
        exit(1);
    snprintf(tmp, n, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    int ok = f && fputs(b.s, f) >= 0;
    if (f && fclose(f) != 0)
        ok = 0;
    if (ok)
        ok = rename(tmp, path) == 0;
    if (!ok)
        remove(tmp);
    free(tmp);
    free(b.s);
    return ok ? c->syms.nvars + c->syms.nfuncs : -1;
}

/* After a change: save if there's a store. Appends a note to *msg on failure. */
static void autosave(Calc *c, char **msg)
{
    if (!c->store || save_store(c, c->store) >= 0)
        return;
    Buf b = {0};
    put(&b, *msg ? *msg : "");
    put(&b, " (couldn't save to ");
    put(&b, c->store);
    put(&b, ")");
    free(*msg);
    *msg = b.s;
}

/* "-12/5" -> -12/5. Returns 0 on success. */
static int parse_fraction(Rational *r, const char *s)
{
    const char *slash = strchr(s, '/');
    int neg = *s == '-';
    if (!slash || slash == s + neg)
        return -1;
    BigInt n, d;
    bi_init(&n);
    bi_init(&d);
    int ok = bi_set_str(&n, s + neg, (size_t)(slash - s - neg)) == 0 &&
             bi_set_str(&d, slash + 1, strlen(slash + 1)) == 0 && !bi_is_zero(&d);
    if (ok) {
        if (neg)
            bi_neg(&n);
        rat_set_frac(r, &n, &d);
    }
    bi_free(&n);
    bi_free(&d);
    return ok ? 0 : -1;
}

/* One line of any length, without the newline; NULL at the end of the file. */
static char *read_line(FILE *f)
{
    Buf b = {0};
    int ch;
    char one[2] = {0, 0};
    while ((ch = fgetc(f)) != EOF && ch != '\n') {
        one[0] = (char)ch;
        put(&b, one);
    }
    if (ch == EOF && !b.s)
        return NULL;
    if (!b.s)
        put(&b, "");
    if (b.len && b.s[b.len - 1] == '\r')
        b.s[--b.len] = '\0';
    return b.s;
}

static int define_line(Calc *c, const char *line);

/* Reads definitions from `path`. Returns how many, or -1 if it can't be read. */
static int load_store(Calc *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int count = 0;
    char *line;
    while ((line = read_line(f))) {
        if (strncmp(line, "fn ", 3) == 0) {
            count += define_line(c, line + 3);
        } else if (strncmp(line, "var ", 4) == 0) {
            char *name = strtok(line + 4, " "), *exact = strtok(NULL, " ");
            char *re = strtok(NULL, " "), *im = strtok(NULL, " ");
            Value v;
            val_init(&v);
            if (name && exact && re && im && !eval_is_reserved(name) &&
                parse_fraction(&v.r, re) == 0 && parse_fraction(&v.im, im) == 0) {
                v.exact = strcmp(exact, "1") == 0;
                sym_set_var(&c->syms, name, &v);
                count++;
            }
            val_free(&v);
        }
        free(line);
    }
    fclose(f);
    return count;
}

const char *calc_default_store(void)
{
    static char path[4096];
    const char *env = getenv("CALC_DEFINITIONS");
    if (env)
        return *env ? env : NULL;
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    snprintf(path, sizeof path, "%s/.calc_definitions", home);
    return path;
}

int calc_use_store(Calc *c, const char *path)
{
    free(c->store);
    c->store = path ? dup_str(path) : NULL;
    if (!path)
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;   /* nothing saved yet */
    fclose(f);
    return load_store(c, path);
}

/* ---- Tables ------------------------------------------------------------------ */

/* Evaluates a parsed expression with `var` (if not NULL) bound to x. */
static int eval_with(Calc *c, const Node *tree, const char *var, const Value *x, Value *out, char *err,
                     size_t errlen)
{
    Binding ans_lower = {"ans", &c->ans, NULL};
    Binding ans = {"Ans", &c->ans, &ans_lower};
    Binding xb = {var, x, &ans};
    EvalOptions opt = {c->digits + GUARD_DIGITS, c->degrees, &c->syms};
    return eval(tree, var ? &xb : &ans, &opt, out, err, errlen);
}

/* Parses an expression that must not be a definition. */
static Node *parse_expression(const char *text, const char *what, char *err, size_t errlen)
{
    char msg[200];
    Node *tree = parse(text, msg, sizeof msg);
    if (tree && (tree->kind == NODE_ASSIGN || tree->kind == NODE_DEFUN)) {
        node_free(tree);
        snprintf(err, errlen, "%s can't be a definition", what);
        return NULL;
    }
    if (!tree)
        snprintf(err, errlen, "%s: %s", what, msg);
    return tree;
}

/* Evaluates a real number like Start or Step. */
static int eval_real(Calc *c, const char *text, const char *what, Value *out, char *err, size_t errlen)
{
    char msg[200];
    Node *tree = parse_expression(text, what, err, errlen);
    if (!tree)
        return -1;
    int rc = eval_with(c, tree, NULL, NULL, out, msg, sizeof msg);
    node_free(tree);
    if (rc) {
        snprintf(err, errlen, "%s: %s", what, msg);
        return -1;
    }
    if (!rat_is_zero(&out->im)) {
        snprintf(err, errlen, "%s must be a real number", what);
        return -1;
    }
    return 0;
}

void table_free(Table *t)
{
    for (int i = 0; i < t->count; i++) {
        val_free(&t->rows[i].x);
        val_free(&t->rows[i].f);
        val_free(&t->rows[i].g);
        free(t->rows[i].f_error);
        free(t->rows[i].g_error);
    }
    free(t->rows);
    t->rows = NULL;
    t->count = 0;
}

int calc_table(Calc *c, const char *f, const char *g, const char *start, const char *end,
               const char *step, Table *out, char *err, size_t errlen)
{
    Value a, b, h;
    Rational span;
    Node *ft = NULL, *gt = NULL;
    int rc = -1;
    val_init(&a);
    val_init(&b);
    val_init(&h);
    rat_init(&span);
    out->rows = NULL;
    out->count = 0;
    out->has_g = g && *g;

    if (!(ft = parse_expression(f, "f(x)", err, errlen)))
        goto done;
    if (out->has_g && !(gt = parse_expression(g, "g(x)", err, errlen)))
        goto done;
    if (eval_real(c, start, "Start", &a, err, errlen) || eval_real(c, end, "End", &b, err, errlen) ||
        eval_real(c, step, "Step", &h, err, errlen))
        goto done;
    if (rat_is_zero(&h.r)) {
        snprintf(err, errlen, "Step can't be 0");
        goto done;
    }

    /* rows = floor((end - start) / step) + 1 */
    rat_sub(&span, &b.r, &a.r);
    rat_div(&span, &span, &h.r);
    if (rat_sign(&span) < 0) {
        snprintf(err, errlen, "End can't be reached from Start with this Step");
        goto done;
    }
    rat_floor(&span, &span);
    int64_t n;
    if (rat_to_i64(&span, &n) || n + 1 > CALC_TABLE_MAX) {
        snprintf(err, errlen, "that's more than %d rows; use a bigger Step", CALC_TABLE_MAX);
        goto done;
    }
    n++;

    out->rows = calloc((size_t)n, sizeof *out->rows);
    if (!out->rows)
        exit(1);
    for (int i = 0; i < n; i++) {
        TableRow *row = &out->rows[i];
        char msg[256];
        val_init(&row->x);
        val_init(&row->f);
        val_init(&row->g);
        out->count = i + 1;
        /* x = start + i * step, exactly (approximate only if Start or Step is) */
        rat_set_i64(&row->x.r, i);
        rat_mul(&row->x.r, &row->x.r, &h.r);
        rat_add(&row->x.r, &row->x.r, &a.r);
        row->x.exact = a.exact && h.exact;
        if (eval_with(c, ft, "x", &row->x, &row->f, msg, sizeof msg))
            row->f_error = dup_str(msg);
        if (gt && eval_with(c, gt, "x", &row->x, &row->g, msg, sizeof msg))
            row->g_error = dup_str(msg);
    }
    rc = 0;

done:
    if (rc)
        table_free(out);
    node_free(ft);
    node_free(gt);
    val_free(&a);
    val_free(&b);
    val_free(&h);
    rat_free(&span);
    return rc;
}

static size_t text_width(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        n += ((unsigned char)*s & 0xC0) != 0x80;
    return n;
}

/* val_to_str without the "≈ " and "  (= 1/3)" decorations */
static char *bare_str(const Value *v, int digits, int sci)
{
    char *s = val_to_str(v, digits, sci);
    if (!s)
        exit(1);
    char *note = strstr(s, "  (=");
    if (note)
        *note = '\0';
    if (strncmp(s, "\xE2\x89\x88 ", 4) == 0)
        memmove(s, s + 4, strlen(s + 4) + 1);
    return s;
}

char *calc_cell(const Calc *c, const Value *v, int width)
{
    (void)c;
    /* Exact and short enough: show it as is. */
    char *s = bare_str(v, width, 0);
    if (!strstr(s, "...") && (int)text_width(s) <= width)
        return s;
    free(s);

    /* Otherwise round to as many significant digits as fit. */
    Value t;
    val_init(&t);
    val_copy(&t, v);
    t.exact = 0;
    for (int d = width; d >= 2; d--) {
        s = bare_str(&t, d, 0);
        if ((int)text_width(s) <= width)
            break;
        free(s);
        s = NULL;
    }
    for (int d = width; !s && d >= 1; d--) {
        s = bare_str(&t, d, 1);
        if ((int)text_width(s) > width && d > 1) {
            free(s);
            s = NULL;
        }
    }
    val_free(&t);
    return s;
}

/* ---- History ------------------------------------------------------------------
 *
 * The history file has one calculation per line: what was typed, a tab, and
 * the result, with any tabs or newlines in the result written as spaces.
 */

static void add_history_entry(Calc *c, const char *input, const char *result)
{
    if (c->nhistory == CALC_HISTORY_MAX) {
        free(c->history[0].input);
        free(c->history[0].result);
        memmove(c->history, c->history + 1, (CALC_HISTORY_MAX - 1) * sizeof *c->history);
        c->nhistory--;
    }
    HistoryEntry *h = realloc(c->history, (size_t)(c->nhistory + 1) * sizeof *h);
    if (!h)
        exit(1);
    c->history = h;
    h[c->nhistory].input = dup_str(input);
    h[c->nhistory].result = dup_str(result);
    for (char *p = h[c->nhistory].result; *p; p++)
        if (*p == '\t' || *p == '\n')
            *p = ' ';
    c->nhistory++;
}

static int write_history(Calc *c, const char *mode, int from)
{
    FILE *f = fopen(c->history_file, mode);
    if (!f)
        return -1;
    for (int i = from; i < c->nhistory; i++)
        fprintf(f, "%s\t%s\n", c->history[i].input, c->history[i].result);
    return fclose(f);
}

/* After a calculation: remember it, and add it to the file. */
static void record(Calc *c, const char *input, const char *result)
{
    add_history_entry(c, input, result);
    if (c->history_file)
        write_history(c, "a", c->nhistory - 1);
}

const char *calc_default_history(void)
{
    static char path[4096];
    const char *env = getenv("CALC_HISTORY");
    if (env)
        return *env ? env : NULL;
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    snprintf(path, sizeof path, "%s/.calc_history", home);
    return path;
}

int calc_use_history(Calc *c, const char *path)
{
    free(c->history_file);
    c->history_file = path ? dup_str(path) : NULL;
    if (!path)
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int lines = 0;
    char *line;
    while ((line = read_line(f))) {
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            add_history_entry(c, line, tab + 1);
        }
        free(line);
        lines++;
    }
    fclose(f);
    if (lines > CALC_HISTORY_MAX)   /* trim a file that has grown long */
        write_history(c, "w", 0);
    return c->nhistory;
}

void calc_clear_history(Calc *c)
{
    for (int i = 0; i < c->nhistory; i++) {
        free(c->history[i].input);
        free(c->history[i].result);
    }
    c->nhistory = 0;
    if (c->history_file)
        write_history(c, "w", 0);   /* an empty file */
}

static char *history_command(Calc *c, const char *args)
{
    char word[16] = "";
    int n = 20;
    if (sscanf(args, "%15s", word) == 1 && strcmp(word, "clear") == 0) {
        calc_clear_history(c);
        return dup_str("history cleared");
    }
    if (word[0])
        n = atoi(word) > 0 ? atoi(word) : n;
    if (c->nhistory == 0)
        return dup_str("no calculations yet");
    Buf b = {0};
    put(&b, "");
    char num[32];
    for (int i = c->nhistory > n ? c->nhistory - n : 0; i < c->nhistory; i++) {
        snprintf(num, sizeof num, "%s%4d  ", b.len ? "\n" : "", i + 1);
        put(&b, num);
        put(&b, c->history[i].input);
        put(&b, "   \xE2\x86\x92 ");   /* → */
        put(&b, c->history[i].result);
    }
    return b.s;
}

/* :table F [; G] from A to B [step S] */
static char *table_command(Calc *c, const char *args, char *err, size_t errlen)
{
    const char *from = strstr(args, " from ");
    const char *to = from ? strstr(from, " to ") : NULL;
    const char *step = to ? strstr(to, " step ") : NULL;
    if (!from || !to) {
        snprintf(err, errlen, "usage: :table x^2 + 1 from 1 to 5 step 1  (; g(x) after f(x) adds a column)");
        return NULL;
    }
    char *fg = malloc((size_t)(from - args) + 1);
    if (!fg)
        exit(1);
    memcpy(fg, args, (size_t)(from - args));
    fg[from - args] = '\0';
    char *semi = strchr(fg, ';');
    if (semi)
        *semi = '\0';
    size_t lt = (size_t)(to - from - 6), le = step ? (size_t)(step - to - 4) : strlen(to + 4);
    char *a = malloc(lt + 1), *b = malloc(le + 1);
    if (!a || !b)
        exit(1);
    memcpy(a, from + 6, lt);
    a[lt] = '\0';
    memcpy(b, to + 4, le);
    b[le] = '\0';

    Table t;
    int rc = calc_table(c, fg, semi ? semi + 1 : NULL, a, b, step ? step + 6 : "1", &t, err, errlen);
    free(a);
    free(b);
    free(fg);
    if (rc)
        return NULL;

    enum { W = 22 };
    Buf out = {0};
    char line[256];
    if (t.has_g)
        snprintf(line, sizeof line, "%-*s  %-*s  g(x)", W, "x", W, "f(x)");
    else
        snprintf(line, sizeof line, "%-*s  f(x)", W, "x");
    put(&out, line);
    for (int i = 0; i < t.count; i++) {
        TableRow *row = &t.rows[i];
        char *cells[3] = {calc_cell(c, &row->x, W), row->f_error ? dup_str("ERROR") : calc_cell(c, &row->f, W),
                          !t.has_g ? dup_str("") : row->g_error ? dup_str("ERROR") : calc_cell(c, &row->g, W)};
        put(&out, "\n");
        for (int k = 0; k < (t.has_g ? 3 : 2); k++) {
            put(&out, cells[k]);
            if (k + 1 < (t.has_g ? 3 : 2))
                for (size_t pad = text_width(cells[k]); pad < W + 2; pad++)
                    put(&out, " ");
        }
        for (int k = 0; k < 3; k++)
            free(cells[k]);
    }
    table_free(&t);
    return out.s;
}

static char *command(Calc *c, const char *cmd, char *err, size_t errlen)
{
    char word[32], extra[2];
    int n;
    char msg[96];

    if (sscanf(cmd, "%31s", word) != 1)
        word[0] = '\0';

    if (strcmp(word, "help") == 0)
        return dup_str(HELP);
    if (strcmp(word, "sci") == 0) {
        c->sci = 1;
        snprintf(msg, sizeof msg, "scientific notation on (%d significant digits)", c->digits);
        return dup_str(msg);
    }
    if (strcmp(word, "fix") == 0) {
        c->sci = 0;
        return dup_str("scientific notation off (whole numbers show every digit)");
    }
    if (strcmp(word, "deg") == 0) {
        c->degrees = 1;
        return dup_str("angles in degrees");
    }
    if (strcmp(word, "rad") == 0) {
        c->degrees = 0;
        return dup_str("angles in radians");
    }
    if (strcmp(word, "vars") == 0)
        return list_symbols(c);
    if (strcmp(word, "history") == 0)
        return history_command(c, cmd + strlen("history"));
    if (strcmp(word, "table") == 0)
        return table_command(c, cmd + strlen("table"), err, errlen);
    if (strcmp(word, "save") == 0 || strcmp(word, "load") == 0) {
        char path[1024];
        int load = word[0] == 'l';
        if (sscanf(cmd, "%31s %1023[^\n]", word, path) != 2) {
            if (load || !c->store) {
                snprintf(err, errlen, "usage: :%s file", load ? "load" : "save");
                return NULL;
            }
            snprintf(path, sizeof path, "%s", c->store);
        }
        int n = load ? load_store(c, path) : save_store(c, path);
        if (n < 0) {
            snprintf(err, errlen, "couldn't %s %s", load ? "read" : "write", path);
            return NULL;
        }
        char *out = malloc(strlen(path) + 64);
        if (!out)
            exit(1);
        sprintf(out, "%s %d definition%s %s %s", load ? "loaded" : "saved", n, n == 1 ? "" : "s",
                load ? "from" : "to", path);
        if (load)
            autosave(c, &out);
        return out;
    }
    if (strcmp(word, "clear") == 0) {
        char name[64];
        if (sscanf(cmd, "%31s %63s", word, name) == 2) {
            if (!sym_remove(&c->syms, name)) {
                snprintf(err, errlen, "'%s' isn't defined", name);
                return NULL;
            }
            snprintf(msg, sizeof msg, "removed %s", name);
            char *out = dup_str(msg);
            autosave(c, &out);
            return out;
        }
        int n = c->syms.nvars + c->syms.nfuncs;
        sym_free(&c->syms);
        snprintf(msg, sizeof msg, "removed %d definition%s", n, n == 1 ? "" : "s");
        char *out = dup_str(msg);
        autosave(c, &out);
        return out;
    }
    if (strcmp(word, "digits") == 0) {
        if (sscanf(cmd, "%31s %d %1s", word, &n, extra) != 2 || n < 1 || n > MAX_DIGITS) {
            snprintf(err, errlen, "usage: :digits N  (N from 1 to %d)", MAX_DIGITS);
            return NULL;
        }
        c->digits = n;
        snprintf(msg, sizeof msg, "showing %d digits", n);
        return dup_str(msg);
    }
    snprintf(err, errlen, "unknown command ':%s' (try :help)", word);
    return NULL;
}

/* f(x, y) = body: checks the names and stores the function. */
static char *define_function(Calc *c, Node *tree, char *err, size_t errlen)
{
    if (eval_is_reserved(tree->name)) {
        snprintf(err, errlen, "'%s' is built in and can't be redefined", tree->name);
        return NULL;
    }
    for (size_t i = 0; i < tree->nparams; i++)
        for (size_t j = 0; j < i; j++)
            if (strcmp(tree->params[i], tree->params[j]) == 0) {
                snprintf(err, errlen, "parameter '%s' appears twice", tree->params[i]);
                return NULL;
            }

    /* The symbol table takes over the body and parameter names. */
    sym_set_func(&c->syms, tree->name, tree->params, (int)tree->nparams, tree->args[0], tree->text);
    tree->params = NULL;
    tree->nparams = 0;
    tree->args[0] = NULL;

    Buf b = {0};
    put_func(&b, sym_func(&c->syms, tree->name));
    autosave(c, &b.s);
    return b.s;
}

/* Defines a function from a saved line; returns 1 if it worked. */
static int define_line(Calc *c, const char *line)
{
    char err[256];
    Node *tree = parse(line, err, sizeof err);
    if (!tree)
        return 0;
    char *out = NULL;
    if (tree->kind == NODE_DEFUN) {
        char *store = c->store;   /* loading shouldn't rewrite the file line by line */
        c->store = NULL;
        out = define_function(c, tree, err, sizeof err);
        c->store = store;
    }
    node_free(tree);
    free(out);
    return out != NULL;
}

/* ---- True or false checks: 2 + 2 = 4, sqrt(2) < 1.5, 1 < x < 3 ---------------- */

/*
 * Is |d| small enough to call a and b equal? Exact values must match exactly.
 * Approximate ones only agree to about as many digits as are shown, so they
 * count as equal when they agree to that many: sqrt(2)^2 = 2 is true. Next to
 * an exact 0, "small" means below 10^-(digits + 5), so sin(pi) = 0 is too.
 */
static int negligible(const Calc *c, const Rational *d, const Rational *a, const Rational *b, int exact)
{
    if (rat_is_zero(d) || exact)
        return rat_is_zero(d);
    double la = rat_log10_abs(a), lb = rat_log10_abs(b);
    double scale = la > lb ? la : lb;
    if (rat_is_zero(a) || rat_is_zero(b))
        scale = -5;
    return rat_log10_abs(d) < scale - c->digits;
}

/* Compares a and b with op (see NODE_REL); -1 with a message if it can't. */
static int compare(const Calc *c, char op, const Value *a, const Value *b, char *err, size_t errlen)
{
    int exact = a->exact && b->exact;
    Rational d, dim;
    rat_init(&d);
    rat_init(&dim);
    rat_sub(&d, &a->r, &b->r);
    rat_sub(&dim, &a->im, &b->im);
    /* one scale for both parts, so 1 + 1e-40i counts as 1 */
    const Rational *big_a = rat_log10_abs(&a->im) > rat_log10_abs(&a->r) ? &a->im : &a->r;
    const Rational *big_b = rat_log10_abs(&b->im) > rat_log10_abs(&b->r) ? &b->im : &b->r;
    int re_zero = negligible(c, &d, big_a, big_b, exact);
    int im_zero = negligible(c, &dim, big_a, big_b, exact);
    int sign = re_zero ? 0 : rat_sign(&d);
    rat_free(&d);
    rat_free(&dim);

    if (op == '=' || op == 'n')
        return (re_zero && im_zero) == (op == '=');
    if (!negligible(c, &a->im, &a->im, &b->r, exact) || !negligible(c, &b->im, &b->im, &a->r, exact)) {
        snprintf(err, errlen, "complex numbers can't be compared with < or >, only = and \xE2\x89\xA0");
        return -1;
    }
    switch (op) {
    case '<': return sign < 0;
    case '>': return sign > 0;
    case 'l': return sign <= 0;
    default:  return sign >= 0;   /* 'g' */
    }
}

/*
 * Works out a check like "2 + 2 = 4", or a chain like "1 < x < 3" (every
 * step must hold). Leaves the rightmost side in *right. Returns 1 for true,
 * 0 for false, -1 on error.
 */
static int check(Calc *c, const Node *rel, Value *right, char *err, size_t errlen)
{
    Value left;
    val_init(&left);
    int truth = 1;
    if (rel->args[0]->kind == NODE_REL)
        truth = check(c, rel->args[0], &left, err, errlen);
    else if (eval_with(c, rel->args[0], NULL, NULL, &left, err, errlen))
        truth = -1;
    if (truth >= 0 && eval_with(c, rel->args[1], NULL, NULL, right, err, errlen))
        truth = -1;
    if (truth >= 0) {
        int holds = compare(c, rel->op, &left, right, err, errlen);
        truth = holds < 0 ? -1 : truth && holds;
    }
    val_free(&left);
    return truth;
}

char *calc_eval(Calc *c, const char *line, char *err, size_t errlen)
{
    c->result_is_value = 0;
    while (isspace((unsigned char)*line))
        line++;
    if (*line == '\0')
        return dup_str("");
    if (*line == ':')
        return command(c, line + 1, err, errlen);

    Node *tree = parse(line, err, errlen);
    if (!tree)
        return NULL;
    char builtin[200] = "";   /* why the line can't be a definition, if it looked like one */
    if ((tree->kind == NODE_ASSIGN || tree->kind == NODE_DEFUN) && eval_is_reserved(tree->name)) {
        /* "pi = 3.14" or "sin(pi) = 0" can't be definitions, so they're checks */
        snprintf(builtin, sizeof builtin, "'%s' is built in and can't be %s", tree->name,
                 tree->kind == NODE_ASSIGN ? "changed" : "redefined");
        char msg[200];
        Node *as_check = parse_check(line, msg, sizeof msg);
        if (as_check) {
            node_free(tree);
            tree = as_check;
        }
    }
    if (tree->kind == NODE_DEFUN) {
        char *out = define_function(c, tree, err, errlen);
        node_free(tree);
        if (out)
            record(c, line, out);
        return out;
    }
    if (tree->kind == NODE_REL) {
        Value right;
        val_init(&right);
        int truth = check(c, tree, &right, err, errlen);
        val_free(&right);
        node_free(tree);
        if (truth < 0 && builtin[0])   /* "sin(x) = 2" with no x: it was meant as a definition */
            snprintf(err, errlen, "%s", builtin);
        if (truth < 0)
            return NULL;
        char *out = dup_str(truth ? "True" : "False");
        record(c, line, out);
        return out;
    }
    if (tree->kind == NODE_ASSIGN && eval_is_reserved(tree->name)) {
        snprintf(err, errlen, "'%s' is built in and can't be changed", tree->name);
        node_free(tree);
        return NULL;
    }

    Binding ans_lower = {"ans", &c->ans, NULL};
    Binding ans = {"Ans", &c->ans, &ans_lower};
    EvalOptions opt = {c->digits + GUARD_DIGITS, c->degrees, &c->syms};
    Value v;
    val_init(&v);
    const Node *expr = tree->kind == NODE_ASSIGN ? tree->args[0] : tree;
    int rc = eval(expr, &ans, &opt, &v, err, errlen);
    if (rc) {
        node_free(tree);
        val_free(&v);
        return NULL;
    }

    char *out;
    if (tree->kind == NODE_ASSIGN) {
        sym_set_var(&c->syms, tree->name, &v);
        Buf b = {0};
        put_var(&b, c, tree->name, &v);
        out = b.s;
        autosave(c, &out);
    } else {
        out = val_to_str(&v, c->digits, c->sci);
    }
    node_free(tree);
    val_swap(&c->ans, &v);
    val_free(&v);
    c->result_is_value = 1;
    record(c, line, out);
    return out;
}
