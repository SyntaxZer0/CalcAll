#include "feval.h"

#include "calculus.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SLOTS 32         /* parameters and sum indices in one function */
#define MAX_DEPTH 64         /* user functions calling user functions */
#define MAX_TERMS 100000     /* terms in one sum or product */
#define MAX_UNKNOWNS 16

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

enum { F_NUM, F_SLOT, F_NEG, F_BIN, F_FACT, F_CALL, F_USER, F_SUM, F_PROD, F_LET, F_INT };

enum {
    B_SQRT, B_CBRT, B_ROOT, B_EXP, B_LN, B_LOG, B_LOG10, B_LOG2, B_SIN, B_COS, B_TAN, B_ASIN,
    B_ACOS, B_ATAN, B_SINH, B_COSH, B_TANH, B_ABS, B_ARG, B_RE, B_IM, B_CONJ, B_FLOOR, B_CEIL,
    B_ROUND, B_GCD, B_LCM, B_CHOOSE, B_PERM
};

static const struct {
    const char *name;
    int id, min_args, max_args;
} BUILTINS[] = {
    {"sqrt", B_SQRT, 1, 1},     {"cbrt", B_CBRT, 1, 1},     {"root", B_ROOT, 2, 2},
    {"exp", B_EXP, 1, 1},       {"ln", B_LN, 1, 1},         {"log", B_LOG, 1, 2},
    {"log10", B_LOG10, 1, 1},   {"log2", B_LOG2, 1, 1},     {"sin", B_SIN, 1, 1},
    {"cos", B_COS, 1, 1},       {"tan", B_TAN, 1, 1},       {"asin", B_ASIN, 1, 1},
    {"acos", B_ACOS, 1, 1},     {"atan", B_ATAN, 1, 1},     {"sinh", B_SINH, 1, 1},
    {"cosh", B_COSH, 1, 1},     {"tanh", B_TANH, 1, 1},     {"abs", B_ABS, 1, 1},
    {"arg", B_ARG, 1, 1},       {"re", B_RE, 1, 1},         {"im", B_IM, 1, 1},
    {"conj", B_CONJ, 1, 1},     {"floor", B_FLOOR, 1, 1},   {"ceil", B_CEIL, 1, 1},
    {"round", B_ROUND, 1, 1},   {"gcd", B_GCD, 2, 2},       {"lcm", B_LCM, 2, 2},
    {"choose", B_CHOOSE, 2, 2}, {"nCr", B_CHOOSE, 2, 2},    {"binomial", B_CHOOSE, 2, 2},
    {"perm", B_PERM, 2, 2},     {"nPr", B_PERM, 2, 2},
};

typedef struct FNode FNode;
typedef struct FFunc FFunc;

struct FNode {
    int kind;
    char op;
    double v;          /* F_NUM */
    int id;            /* F_CALL: builtin */
    int slot;          /* F_SLOT, and the index of F_SUM / F_PROD */
    int odd_root;      /* '^' with a constant exponent p/q, q odd: keep (-8)^(1/3) real */
    long p, q;
    FFunc *fn;         /* F_USER */
    FNode **a;
    int n;
};

struct FFunc {
    char *name;
    char *const *params;
    int nparams;
    const Node *body;
    FNode *code;       /* NULL until compiled */
    int nslots;
    int compiling;
    char *unknowns[MAX_UNKNOWNS];
    int nunknowns;
    int uses_params;   /* bit i: the body mentions parameter i (y = 5 doesn't use x) */
    FFunc *next;
};

typedef struct {
    char *name;
    double value;
} LocalVar;

struct FEnv {
    const Symbols *globals;
    int degrees;
    LocalVar *vars;
    int nvars;
    FFunc *funcs;   /* local functions, then global ones as they're first used */
};

struct FExpr {
    FFunc fn;
    FEnv *env;
};

static void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n, size);
    if (!p) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

static char *dup(const char *s)
{
    return strcpy(xcalloc(strlen(s) + 1, 1), s);
}

/* A Rational to the nearest double, via 17 significant digits. */
static double to_double(const Rational *r)
{
    if (rat_is_zero(r))
        return 0;
    BigInt q;
    long k;
    bi_init(&q);
    rat_sig_digits(r, 17, 1, &q, &k);
    char *digits = bi_to_str(&q);
    char buf[64];
    snprintf(buf, sizeof buf, "%s%c.%se%ld", rat_sign(r) < 0 ? "-" : "", digits[0], digits + 1, k);
    free(digits);
    bi_free(&q);
    return strtod(buf, NULL);
}

/* ---- Environment ------------------------------------------------------------ */

FEnv *fenv_new(const Symbols *globals, int degrees)
{
    FEnv *env = xcalloc(1, sizeof *env);
    env->globals = globals;
    env->degrees = degrees;
    return env;
}

static void free_node(FNode *n)
{
    if (!n)
        return;
    for (int i = 0; i < n->n; i++)
        free_node(n->a[i]);
    free(n->a);
    free(n);
}

static void free_func_parts(FFunc *f)
{
    free(f->name);
    free_node(f->code);
    for (int i = 0; i < f->nunknowns; i++)
        free(f->unknowns[i]);
}

void fenv_free(FEnv *env)
{
    if (!env)
        return;
    for (int i = 0; i < env->nvars; i++)
        free(env->vars[i].name);
    free(env->vars);
    for (FFunc *f = env->funcs, *next; f; f = next) {
        next = f->next;
        free_func_parts(f);
        free(f);
    }
    free(env);
}

void fenv_set_var(FEnv *env, const char *name, double value)
{
    for (int i = 0; i < env->nvars; i++)
        if (strcmp(env->vars[i].name, name) == 0) {
            env->vars[i].value = value;
            return;
        }
    LocalVar *vars = realloc(env->vars, (size_t)(env->nvars + 1) * sizeof *vars);
    if (!vars)
        exit(1);
    env->vars = vars;
    env->vars[env->nvars].name = dup(name);
    env->vars[env->nvars++].value = value;
}

static FFunc *add_func(FEnv *env, const char *name, char *const *params, int nparams, const Node *body)
{
    FFunc *f = xcalloc(1, sizeof *f);
    f->name = dup(name);
    f->params = params;
    f->nparams = nparams;
    f->body = body;
    /* Local functions go first so they win over global ones of the same name. */
    f->next = env->funcs;
    env->funcs = f;
    return f;
}

void fenv_set_func(FEnv *env, const char *name, char *const *params, int nparams, const Node *body)
{
    add_func(env, name, params, nparams, body);
}

/* ---- Compiling ---------------------------------------------------------------- */

typedef struct {
    FEnv *env;
    FFunc *fn;                       /* function being compiled */
    const char *scope[MAX_SLOTS];    /* name of each slot in use */
    int nscope;
} Comp;

static FNode *node(int kind, int nargs)
{
    FNode *n = xcalloc(1, sizeof *n);
    n->kind = kind;
    n->n = nargs;
    if (nargs)
        n->a = xcalloc((size_t)nargs, sizeof *n->a);
    return n;
}

static FNode *number(double v)
{
    FNode *n = node(F_NUM, 0);
    n->v = v;
    return n;
}

static void compile_func(FEnv *env, FFunc *f);
static FNode *compile(Comp *c, const Node *n);

static void note_unknown(Comp *c, const char *name)
{
    FFunc *f = c->fn;
    for (int i = 0; i < f->nunknowns; i++)
        if (strcmp(f->unknowns[i], name) == 0)
            return;
    if (f->nunknowns < MAX_UNKNOWNS)
        f->unknowns[f->nunknowns++] = dup(name);
}

/* A name: a parameter or sum index, a variable, a constant, or letters
 * multiplied together (ax = a*x, as in Desmos). */
static FNode *compile_name(Comp *c, const char *name)
{
    for (int i = c->nscope - 1; i >= 0; i--)
        if (c->scope[i] && strcmp(c->scope[i], name) == 0) {
            FNode *n = node(F_SLOT, 0);
            n->slot = i;
            if (i < c->fn->nparams && i < 30)
                c->fn->uses_params |= 1 << i;
            return n;
        }
    for (int i = 0; i < c->env->nvars; i++)
        if (strcmp(c->env->vars[i].name, name) == 0)
            return number(c->env->vars[i].value);
    const Value *v = c->env->globals ? sym_var(c->env->globals, name) : NULL;
    if (v)
        return number(rat_is_zero(&v->im) ? to_double(&v->r) : NAN);
    if (strcmp(name, "pi") == 0 || strcmp(name, "\xCF\x80") == 0)
        return number(M_PI);
    if (strcmp(name, "e") == 0)
        return number(M_E);
    if (strcmp(name, "i") == 0)
        return number(NAN);   /* graphs are real */

    size_t len = strlen(name);
    int letters = len > 1;
    for (size_t i = 0; i < len && letters; i++)
        letters = isalpha((unsigned char)name[i]);
    if (letters) {
        FNode *prod = NULL;
        for (size_t i = 0; i < len; i++) {
            char one[2] = {name[i], '\0'};
            FNode *f = compile_name(c, one);
            if (!prod) {
                prod = f;
            } else {
                FNode *m = node(F_BIN, 2);
                m->op = '*';
                m->a[0] = prod;
                m->a[1] = f;
                prod = m;
            }
        }
        return prod;
    }
    note_unknown(c, name);
    return number(NAN);
}

/* Is n an integer constant (possibly negated)? */
static int const_int(const Node *n, long *out)
{
    if (n->kind == NODE_NEG) {
        if (!const_int(n->args[0], out))
            return 0;
        *out = -*out;
        return 1;
    }
    int64_t v;
    if (n->kind == NODE_NUM && rat_to_i64(&n->value, &v) == 0 && v > -1000000 && v < 1000000) {
        *out = (long)v;
        return 1;
    }
    return 0;
}

/* One of the user's own functions (from the Calculate tab), on first use. */
static FFunc *add_func_global(FEnv *env, const UserFunc *u)
{
    FFunc *f = add_func(env, u->name, u->params, u->nparams, u->body);
    /* Keep it behind the local ones, which must win if the names match. */
    env->funcs = f->next;
    FFunc **tail = &env->funcs;
    while (*tail)
        tail = &(*tail)->next;
    f->next = NULL;
    *tail = f;
    return f;
}

static FFunc *find_func(FEnv *env, const char *name)
{
    for (FFunc *f = env->funcs; f; f = f->next)
        if (strcmp(f->name, name) == 0)
            return f;
    const UserFunc *u = env->globals ? sym_func(env->globals, name) : NULL;
    if (!u)
        return NULL;
    return add_func_global(env, u);
}

/* ---- Derivatives: names are looked up as compile_name() would ---------------- */

static int is_constant_name(const char *name)
{
    return strcmp(name, "pi") == 0 || strcmp(name, "\xCF\x80") == 0 || strcmp(name, "e") == 0 ||
           strcmp(name, "i") == 0;
}

static int fname_known(void *ctx, const char *name, int closed)
{
    Comp *c = ctx;
    if (!closed)
        for (int i = 0; i < c->nscope; i++)
            if (c->scope[i] && strcmp(c->scope[i], name) == 0)
                return 1;
    for (int i = 0; i < c->env->nvars; i++)
        if (strcmp(c->env->vars[i].name, name) == 0)
            return 1;
    if (c->env->globals && sym_var(c->env->globals, name))
        return 1;
    return is_constant_name(name);
}

static int fname_func(void *ctx, const char *name, char *const **params, int *nparams, const Node **body)
{
    Comp *c = ctx;
    FFunc *f = find_func(c->env, name);
    if (!f)
        return 0;
    *params = f->params;
    *nparams = f->nparams;
    *body = f->body;
    return 1;
}

/* d/dx(f) at a: compiled as f' with x = a. */
static FNode *compile_diff(Comp *c, const Node *n)
{
    char err[256];
    Deriver d = {fname_known, fname_func, c, c->env->degrees, err, sizeof err};
    Node *tree = derive(n->args[0], n->name, &d);
    if (!tree)
        return number(NAN);
    Node *at = node_new(NODE_LET, 2);
    at->name = dup(n->name);
    if (n->nargs > 1) {
        at->args[0] = node_clone(n->args[1]);
    } else {
        at->args[0] = node_new(NODE_VAR, 0);
        at->args[0]->name = dup(n->name);
    }
    at->args[1] = tree;
    FNode *f = compile(c, at);
    node_free(at);   /* compiled code doesn't point into the tree */
    return f;
}

static FNode *compile(Comp *c, const Node *n)
{
    switch (n->kind) {
    case NODE_NUM:
        return number(to_double(&n->value));

    case NODE_VAR:
        return compile_name(c, n->name);

    case NODE_NEG:
    case NODE_FACT: {
        FNode *f = node(n->kind == NODE_NEG ? F_NEG : F_FACT, 1);
        f->a[0] = compile(c, n->args[0]);
        return f;
    }

    case NODE_BINOP: {
        FNode *f = node(F_BIN, 2);
        f->op = n->op;
        f->a[0] = compile(c, n->args[0]);
        f->a[1] = compile(c, n->args[1]);
        /* A constant exponent p/q with odd q keeps roots of negatives real. */
        const Node *e = n->args[1];
        long p, q = 1;
        if (n->op == '^' && ((e->kind == NODE_BINOP && e->op == '/' && const_int(e->args[0], &p) &&
                              const_int(e->args[1], &q)) ||
                             (e->kind == NODE_NEG && e->args[0]->kind == NODE_BINOP &&
                              e->args[0]->op == '/' && const_int(e->args[0]->args[0], &p) &&
                              const_int(e->args[0]->args[1], &q) && (p = -p, 1)))) {
            if (q < 0) {
                p = -p;
                q = -q;
            }
            if (q % 2 == 1) {
                f->odd_root = 1;
                f->p = p;
                f->q = q;
            }
        }
        return f;
    }

    case NODE_CALL: {
        for (size_t i = 0; i < sizeof BUILTINS / sizeof *BUILTINS; i++) {
            if (strcmp(BUILTINS[i].name, n->name) != 0)
                continue;
            if ((int)n->nargs < BUILTINS[i].min_args || (int)n->nargs > BUILTINS[i].max_args)
                return number(NAN);
            FNode *f = node(F_CALL, (int)n->nargs);
            f->id = BUILTINS[i].id;
            for (size_t k = 0; k < n->nargs; k++)
                f->a[k] = compile(c, n->args[k]);
            return f;
        }
        FFunc *fn = find_func(c->env, n->name);
        if (!fn || fn->nparams != (int)n->nargs)
            return number(NAN);   /* an unknown function isn't something a slider can fix */
        compile_func(c->env, fn);
        FNode *f = node(F_USER, (int)n->nargs);
        f->fn = fn;
        for (size_t k = 0; k < n->nargs; k++)
            f->a[k] = compile(c, n->args[k]);
        return f;
    }

    case NODE_REL: {   /* an equation or inequality: compare lhs - rhs with 0 */
        FNode *f = node(F_BIN, 2);
        f->op = '-';
        f->a[0] = compile(c, n->args[0]);
        f->a[1] = compile(c, n->args[1]);
        return f;
    }

    case NODE_SUM:
    case NODE_PROD: {
        if (c->nscope >= MAX_SLOTS)
            return number(NAN);
        FNode *f = node(n->kind == NODE_SUM ? F_SUM : F_PROD, 3);
        f->a[0] = compile(c, n->args[0]);
        f->a[1] = compile(c, n->args[1]);
        f->slot = c->nscope;
        c->scope[c->nscope++] = n->name;
        if (c->nscope > c->fn->nslots)
            c->fn->nslots = c->nscope;
        f->a[2] = compile(c, n->args[2]);
        c->nscope--;
        return f;
    }

    case NODE_DIFF:
        return compile_diff(c, n);

    case NODE_LET:
    case NODE_INT: {
        /* a value for a name (LET), or the integral's variable (INT) */
        if (c->nscope >= MAX_SLOTS)
            return number(NAN);
        int is_let = n->kind == NODE_LET;
        FNode *f = node(is_let ? F_LET : F_INT, is_let ? 2 : 3);
        f->a[0] = compile(c, n->args[0]);
        if (!is_let)
            f->a[1] = compile(c, n->args[1]);
        f->slot = c->nscope;
        c->scope[c->nscope++] = n->name;
        if (c->nscope > c->fn->nslots)
            c->fn->nslots = c->nscope;
        f->a[is_let ? 1 : 2] = compile(c, n->args[is_let ? 1 : 2]);
        c->nscope--;
        return f;
    }

    case NODE_APPLY: {
        /* a user function's body applied to arguments: an unnamed function */
        int np = (int)n->nparams;
        FFunc *fn = add_func(c->env, "", n->params, np, n->args[np]);
        compile_func(c->env, fn);
        fn->params = NULL;   /* they belong to the tree, which goes away */
        fn->body = NULL;
        FNode *f = node(F_USER, np);
        f->fn = fn;
        for (int k = 0; k < np; k++)
            f->a[k] = compile(c, n->args[k]);
        return f;
    }

    default:
        return number(NAN);
    }
}

static void compile_func(FEnv *env, FFunc *f)
{
    if (f->code || f->compiling)
        return;   /* already done, or recursion: the call is wired up all the same */
    f->compiling = 1;
    Comp c = {env, f, {0}, 0};
    for (int i = 0; i < f->nparams && i < MAX_SLOTS; i++)
        c.scope[c.nscope++] = f->params[i];
    f->nslots = c.nscope;
    f->code = compile(&c, f->body);
    f->compiling = 0;
}

FExpr *fexpr_compile(FEnv *env, const Node *expr, char *const *params, int nparams)
{
    FExpr *e = xcalloc(1, sizeof *e);
    e->env = env;
    e->fn.name = dup("");
    e->fn.params = params;
    e->fn.nparams = nparams;
    e->fn.body = expr;
    compile_func(env, &e->fn);
    return e;
}

int fexpr_unknowns(const FExpr *f, const char *const **names)
{
    *names = (const char *const *)f->fn.unknowns;
    return f->fn.nunknowns;
}

int fexpr_uses_params(const FExpr *f)
{
    return f->fn.uses_params != 0;
}

int fexpr_uses_param(const FExpr *f, int i)
{
    return (f->fn.uses_params >> i) & 1;
}

void fexpr_free(FExpr *f)
{
    if (!f)
        return;
    free_func_parts(&f->fn);
    free(f);
}

/* ---- Evaluating -------------------------------------------------------------------- */

static int is_int(double x)
{
    return isfinite(x) && x == floor(x);
}

static double gcd(double a, double b)
{
    a = fabs(a);
    b = fabs(b);
    while (b > 0) {
        double t = fmod(a, b);
        a = b;
        b = t;
    }
    return a;
}

static double call_builtin(int id, const double *a, int n, int degrees)
{
    double k = degrees ? M_PI / 180 : 1;   /* converts the angle unit to radians */
    switch (id) {
    case B_SQRT:
        return sqrt(a[0]);
    case B_CBRT:
        return cbrt(a[0]);
    case B_ROOT:
        if (!is_int(a[1]) || a[1] < 1)
            return NAN;
        if (a[0] < 0 && fmod(a[1], 2) == 1)
            return -pow(-a[0], 1 / a[1]);   /* odd roots of negatives are real */
        return pow(a[0], 1 / a[1]);
    case B_EXP:
        return exp(a[0]);
    case B_LN:
        return log(a[0]);
    case B_LOG:
        return n == 2 ? log(a[0]) / log(a[1]) : log10(a[0]);
    case B_LOG10:
        return log10(a[0]);
    case B_LOG2:
        return log2(a[0]);
    case B_SIN:
        return sin(a[0] * k);
    case B_COS:
        return cos(a[0] * k);
    case B_TAN:
        return tan(a[0] * k);
    case B_ASIN:
        return asin(a[0]) / k;
    case B_ACOS:
        return acos(a[0]) / k;
    case B_ATAN:
        return atan(a[0]) / k;
    case B_SINH:
        return sinh(a[0]);
    case B_COSH:
        return cosh(a[0]);
    case B_TANH:
        return tanh(a[0]);
    case B_ABS:
        return fabs(a[0]);
    case B_ARG:
        return isnan(a[0]) ? NAN : a[0] < 0 ? M_PI / k : 0;
    case B_RE:
    case B_CONJ:
        return a[0];
    case B_IM:
        return isnan(a[0]) ? NAN : 0;
    case B_FLOOR:
        return floor(a[0]);
    case B_CEIL:
        return ceil(a[0]);
    case B_ROUND:
        return round(a[0]);
    case B_GCD:
    case B_LCM: {
        if (!is_int(a[0]) || !is_int(a[1]))
            return NAN;
        double g = gcd(a[0], a[1]);
        if (id == B_GCD)
            return g;
        return g == 0 ? 0 : fabs(a[0] / g * a[1]);
    }
    case B_CHOOSE: {
        double nn = a[0], kk = a[1];
        if (!is_int(nn) || !is_int(kk))
            return NAN;
        if (kk < 0 || (nn >= 0 && kk > nn))
            return 0;
        if (nn >= 0 && kk > nn - kk)
            kk = nn - kk;
        if (kk > 1000)
            return round(exp(lgamma(nn + 1) - lgamma(kk + 1) - lgamma(nn - kk + 1)));
        double r = 1;
        for (double i = 1; i <= kk; i++)
            r = r * (nn - kk + i) / i;
        return round(r);
    }
    case B_PERM: {
        double nn = a[0], kk = a[1];
        if (!is_int(nn) || !is_int(kk) || nn < 0 || kk < 0)
            return NAN;
        if (kk > nn)
            return 0;
        if (kk > 1000)
            return round(exp(lgamma(nn + 1) - lgamma(nn - kk + 1)));
        double r = 1;
        for (double i = 0; i < kk; i++)
            r *= nn - i;
        return r;
    }
    }
    return NAN;
}

static double ev(const FNode *n, double *s, int depth, int degrees);

/*
 * Tanh-sinh quadrature in doubles, for graphs (see calculus.c for the idea).
 * Steps t = k/64 for 0 <= t <= 4, each level halving the step, until two
 * levels agree. Points where f isn't a number (1/sqrt(x) right at 0) count
 * as 0; their weight is negligible. A range that doesn't settle (a corner or
 * a jump inside) is split in two, which closes in on the trouble.
 */
#define TS_DIV 64
#define TS_STEPS (4 * TS_DIV)
static double ts_c[TS_STEPS + 1], ts_w[TS_STEPS + 1];

static void ts_init(void)
{
    if (ts_w[0] != 0)
        return;
    for (int k = 0; k <= TS_STEPS; k++) {
        double t = (double)k / TS_DIV;
        double g = exp(M_PI * sinh(t));
        ts_c[k] = 1 / (g + 1);
        ts_w[k] = 2 * M_PI * cosh(t) * ts_c[k] * (1 - ts_c[k]);
    }
}

static double integrate(const FNode *n, double lo, double hi, double *s, int depth, int degrees, int splits,
                        int *budget)
{
    ts_init();
    double width = hi - lo, sum = 0, sabs = 0, prev = NAN;
    for (int level = 0, step = TS_DIV; step >= 1; level++, step /= 2) {
        for (int k = 0; k <= TS_STEPS; k += step) {
            if (level > 0 && (k / step) % 2 == 0)
                continue;   /* already added at a coarser level */
            for (int side = 0; side < (k ? 2 : 1); side++) {
                s[n->slot] = side ? hi - width * ts_c[k] : lo + width * ts_c[k];
                double y = ev(n->a[2], s, depth, degrees);
                --*budget;
                if (isfinite(y)) {
                    sum += ts_w[k] * y;
                    sabs += ts_w[k] * fabs(y);
                }
            }
        }
        double h = (double)step / TS_DIV, value = width / 2 * h * sum;
        double scale = fmax(fabs(value), fabs(width / 2 * h * sabs));
        if (level >= 3 && fabs(value - prev) <= 1e-11 * scale)
            return value;
        if (level == 4 && splits < 10 && *budget > 0) {
            double mid = lo + width / 2;
            double left = integrate(n, lo, mid, s, depth, degrees, splits + 1, budget);
            return left + integrate(n, mid, hi, s, depth, degrees, splits + 1, budget);
        }
        prev = value;
    }
    return prev;
}

static double ev(const FNode *n, double *s, int depth, int degrees)
{
    switch (n->kind) {
    case F_NUM:
        return n->v;
    case F_SLOT:
        return s[n->slot];
    case F_NEG:
        return -ev(n->a[0], s, depth, degrees);
    case F_FACT: {
        double x = ev(n->a[0], s, depth, degrees);
        return x < 0 && is_int(x) ? NAN : tgamma(x + 1);   /* x! = Gamma(x+1) between integers */
    }
    case F_BIN: {
        double a = ev(n->a[0], s, depth, degrees), b = ev(n->a[1], s, depth, degrees);
        switch (n->op) {
        case '+':
            return a + b;
        case '-':
            return a - b;
        case '*':
            return a * b;
        case '/':
            return b == 0 ? NAN : a / b;
        case '%':
            return b == 0 ? NAN : a - b * floor(a / b);
        case '^':
            if (a < 0 && n->odd_root) {
                double r = pow(-a, (double)n->p / (double)n->q);
                return n->p % 2 ? -r : r;
            }
            return pow(a, b);
        }
        return NAN;
    }
    case F_CALL: {
        double args[2] = {0, 0};
        for (int i = 0; i < n->n && i < 2; i++)
            args[i] = ev(n->a[i], s, depth, degrees);
        return call_builtin(n->id, args, n->n, degrees);
    }
    case F_USER: {
        if (depth >= MAX_DEPTH || !n->fn->code)
            return NAN;
        double frame[MAX_SLOTS];
        for (int i = 0; i < n->n && i < MAX_SLOTS; i++)
            frame[i] = ev(n->a[i], s, depth, degrees);
        return ev(n->fn->code, frame, depth + 1, degrees);
    }
    case F_SUM:
    case F_PROD: {
        double lo = ev(n->a[0], s, depth, degrees), hi = ev(n->a[1], s, depth, degrees);
        if (!is_int(lo) || !is_int(hi) || hi - lo > MAX_TERMS)
            return NAN;
        double acc = n->kind == F_SUM ? 0 : 1;
        for (double k = lo; k <= hi; k++) {
            s[n->slot] = k;
            double t = ev(n->a[2], s, depth, degrees);
            acc = n->kind == F_SUM ? acc + t : acc * t;
        }
        return acc;
    }
    case F_LET:
        s[n->slot] = ev(n->a[0], s, depth, degrees);
        return ev(n->a[1], s, depth, degrees);
    case F_INT: {
        double lo = ev(n->a[0], s, depth, degrees), hi = ev(n->a[1], s, depth, degrees);
        if (!isfinite(lo) || !isfinite(hi))
            return NAN;
        int budget = 5000;   /* evaluations, so a graph never stalls on one point */
        return lo == hi ? 0 : integrate(n, lo, hi, s, depth, degrees, 0, &budget);
    }
    }
    return NAN;
}

double fexpr_eval(const FExpr *f, const double *args)
{
    double frame[MAX_SLOTS];
    for (int i = 0; i < f->fn.nparams && i < MAX_SLOTS; i++)
        frame[i] = args[i];
    double y = ev(f->fn.code, frame, 0, f->env->degrees);
    return isfinite(y) ? y : NAN;
}
