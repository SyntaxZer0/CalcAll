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
    "Commands:   :deg  :rad  :sci  :fix  :digits N  :vars  :clear [name]  :help  :quit\n"
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
}

void calc_free(Calc *c)
{
    val_free(&c->ans);
    sym_free(&c->syms);
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
    if (strcmp(word, "clear") == 0) {
        char name[64];
        if (sscanf(cmd, "%31s %63s", word, name) == 2) {
            if (!sym_remove(&c->syms, name)) {
                snprintf(err, errlen, "'%s' isn't defined", name);
                return NULL;
            }
            snprintf(msg, sizeof msg, "removed %s", name);
            return dup_str(msg);
        }
        int n = c->syms.nvars + c->syms.nfuncs;
        sym_free(&c->syms);
        snprintf(msg, sizeof msg, "removed %d definition%s", n, n == 1 ? "" : "s");
        return dup_str(msg);
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
    return b.s;
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
    if (tree->kind == NODE_DEFUN) {
        char *out = define_function(c, tree, err, errlen);
        node_free(tree);
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
    } else {
        out = val_to_str(&v, c->digits, c->sci);
    }
    node_free(tree);
    val_swap(&c->ans, &v);
    val_free(&v);
    c->result_is_value = 1;
    return out;
}
