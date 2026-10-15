#include "calculus.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "approx.h"

#define MAX_BOUND 64        /* nested sums, integrals and parameters */
#define MAX_EXPANSIONS 40   /* user functions inside user functions */
#define MAX_DEGREE 60

/* ---- Building trees ------------------------------------------------------------ */

static char *str_dup(const char *s)
{
    char *d = malloc(strlen(s) + 1);
    if (!d) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    return strcpy(d, s);
}

static Node *num(int64_t v)
{
    Node *n = node_new(NODE_NUM, 0);
    rat_set_i64(&n->value, v);
    return n;
}

static int is_num(const Node *n, int64_t v)
{
    int64_t x;
    return n->kind == NODE_NUM && rat_to_i64(&n->value, &x) == 0 && x == v;
}

static Node *var(const char *name)
{
    Node *n = node_new(NODE_VAR, 0);
    n->name = str_dup(name);
    return n;
}

static Node *binop(char op, Node *a, Node *b)
{
    Node *n = node_new(NODE_BINOP, 2);
    n->op = op;
    n->args[0] = a;
    n->args[1] = b;
    return n;
}

/* The constructors below take ownership of their arguments and skip work
 * that makes no difference (x + 0, x * 1, ...), so derivatives of constant
 * parts come out as a plain 0. */

static Node *neg(Node *a)
{
    if (is_num(a, 0))
        return a;
    if (a->kind == NODE_NEG) {
        Node *inner = a->args[0];
        a->args[0] = NULL;
        node_free(a);
        return inner;
    }
    Node *n = node_new(NODE_NEG, 1);
    n->args[0] = a;
    return n;
}

static Node *add(Node *a, Node *b)
{
    if (is_num(a, 0)) {
        node_free(a);
        return b;
    }
    if (is_num(b, 0)) {
        node_free(b);
        return a;
    }
    return binop('+', a, b);
}

static Node *sub(Node *a, Node *b)
{
    if (is_num(b, 0)) {
        node_free(b);
        return a;
    }
    if (is_num(a, 0)) {
        node_free(a);
        return neg(b);
    }
    return binop('-', a, b);
}

static Node *mul(Node *a, Node *b)
{
    if (is_num(a, 0) || is_num(b, 0)) {
        node_free(a);
        node_free(b);
        return num(0);
    }
    if (is_num(a, 1)) {
        node_free(a);
        return b;
    }
    if (is_num(b, 1)) {
        node_free(b);
        return a;
    }
    return binop('*', a, b);
}

static Node *dvd(Node *a, Node *b)
{
    if (is_num(a, 0)) {
        node_free(b);
        return a;
    }
    if (is_num(b, 1)) {
        node_free(b);
        return a;
    }
    return binop('/', a, b);
}

static Node *power(Node *a, Node *b)
{
    if (is_num(b, 1)) {
        node_free(b);
        return a;
    }
    return binop('^', a, b);
}

static Node *call1(const char *name, Node *a)
{
    Node *n = node_new(NODE_CALL, 1);
    n->name = str_dup(name);
    n->args[0] = a;
    return n;
}

/* name = value in body */
static Node *let(const char *name, Node *value, Node *body)
{
    if (is_num(body, 0)) {
        node_free(value);
        return body;
    }
    Node *n = node_new(NODE_LET, 2);
    n->name = str_dup(name);
    n->args[0] = value;
    n->args[1] = body;
    return n;
}

/* A user function's body, applied to (copies of) the arguments of `call`. */
static Node *apply(char *const *params, int nparams, Node *const *args, Node *body)
{
    if (is_num(body, 0))
        return body;
    Node *n = node_new(NODE_APPLY, (size_t)nparams + 1);
    n->params = malloc((size_t)nparams * sizeof *n->params + 1);
    if (!n->params) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    for (int i = 0; i < nparams; i++) {
        n->params[i] = str_dup(params[i]);
        n->args[i] = node_clone(args[i]);
    }
    n->nparams = (size_t)nparams;
    n->args[nparams] = body;
    return n;
}

/* ---- Names ------------------------------------------------------------------- */

typedef struct {
    const Deriver *d;
    const char *bound[MAX_BOUND];   /* names of enclosing sums, integrals and parameters */
    int nbound;
    int closed;                     /* inside a user function: only globals are visible */
    int depth;                      /* user functions expanded so far */
    int failed;
} State;

static Node *fail(State *s, const char *fmt, ...)
{
    if (!s->failed) {
        s->failed = 1;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s->d->err, s->d->errlen, fmt, ap);
        va_end(ap);
    }
    return NULL;
}

static int push(State *s, const char *name)
{
    if (s->nbound >= MAX_BOUND) {
        fail(s, "too many nested sums and integrals to differentiate");
        return 0;
    }
    s->bound[s->nbound++] = name;
    return 1;
}

enum { NAME_VAR, NAME_CONST, NAME_LETTERS };

/* What a name means here: the variable itself, something that doesn't
 * change with it, or letters multiplied together ("ax" is a*x). */
static int resolve(const State *s, const char *name, const char *v)
{
    if (strcmp(name, v) == 0)
        return NAME_VAR;
    for (int i = s->nbound - 1; i >= 0; i--)
        if (strcmp(s->bound[i], name) == 0)
            return NAME_CONST;
    if (s->d->known(s->d->ctx, name, s->closed))
        return NAME_CONST;
    size_t len = strlen(name);
    int letters = len > 1;
    for (size_t i = 0; i < len && letters; i++)
        letters = isalpha((unsigned char)name[i]);
    return letters ? NAME_LETTERS : NAME_CONST;
}

static Node *split_letters(const char *name)
{
    Node *prod = NULL;
    for (const char *p = name; *p; p++) {
        char one[2] = {*p, '\0'};
        prod = prod ? binop('*', prod, var(one)) : var(one);
    }
    return prod;
}

/* ---- Derivatives ------------------------------------------------------------- */

static Node *D(State *s, const Node *n, const char *v);

/* In degrees, trig functions are scaled: d/dx sin(x°) = cos(x°)·π/180. */
static Node *deg_factor(State *s)
{
    return s->d->degrees ? binop('/', var("pi"), num(180)) : num(1);
}

/* Derivative of a built-in function of u, given u' (du, owned). */
static Node *builtin(State *s, const Node *n, Node *du)
{
    const char *f = n->name;
    const Node *u = n->args[0];
#define U node_clone(u)
#define IS(name) (strcmp(f, (name)) == 0)
    if (IS("sqrt"))
        return dvd(du, mul(num(2), call1("sqrt", U)));
    if (IS("cbrt"))
        return dvd(du, mul(num(3), power(call1("cbrt", U), num(2))));
    if (IS("exp"))
        return mul(call1("exp", U), du);
    if (IS("ln"))
        return dvd(du, U);
    if ((IS("log") && n->nargs == 1) || IS("log10"))
        return dvd(du, mul(U, call1("ln", num(10))));
    if (IS("log2"))
        return dvd(du, mul(U, call1("ln", num(2))));
    if (IS("sin"))
        return mul(mul(call1("cos", U), du), deg_factor(s));
    if (IS("cos"))
        return neg(mul(mul(call1("sin", U), du), deg_factor(s)));
    if (IS("tan"))
        return dvd(mul(du, deg_factor(s)), power(call1("cos", U), num(2)));
    if (IS("asin") || IS("acos")) {
        Node *d = dvd(dvd(du, call1("sqrt", sub(num(1), power(U, num(2))))), deg_factor(s));
        return IS("acos") ? neg(d) : d;
    }
    if (IS("atan"))
        return dvd(dvd(du, add(num(1), power(U, num(2)))), deg_factor(s));
    if (IS("sinh"))
        return mul(call1("cosh", U), du);
    if (IS("cosh"))
        return mul(call1("sinh", U), du);
    if (IS("tanh"))
        return dvd(du, power(call1("cosh", U), num(2)));
    if (IS("abs"))   /* |u|' = u/|u| · u' */
        return mul(dvd(U, call1("abs", U)), du);
    node_free(du);
    if (IS("floor") || IS("ceil") || IS("round"))
        return fail(s, "%s jumps, so it has no derivative", f);
    return fail(s, "%s can't be differentiated", f);
#undef U
#undef IS
}

static Node *D_call(State *s, const Node *n, const char *v)
{
    char *const *params;
    int nparams;
    const Node *body;
    if (s->d->func(s->d->ctx, n->name, &params, &nparams, &body)) {
        if (nparams != (int)n->nargs)
            return fail(s, "%s takes %d argument%s", n->name, nparams, nparams == 1 ? "" : "s");
        if (s->depth >= MAX_EXPANSIONS)
            return fail(s, "%s calls itself too many times to differentiate", n->name);
        Node *a = apply(params, nparams, n->args, node_clone(body));
        s->depth++;
        Node *r = D(s, a, v);
        s->depth--;
        node_free(a);
        return r;
    }

    /* Built in: the chain rule, with each argument's derivative */
    Node *da[8] = {0};
    size_t nargs = n->nargs < 8 ? n->nargs : 8;
    int all_zero = 1;
    for (size_t i = 0; i < nargs; i++) {
        da[i] = D(s, n->args[i], v);
        if (!da[i]) {
            for (size_t k = 0; k < i; k++)
                node_free(da[k]);
            return NULL;
        }
        all_zero &= is_num(da[i], 0);
    }
    if (all_zero) {
        for (size_t i = 0; i < nargs; i++)
            node_free(da[i]);
        return num(0);
    }
    if (nargs == 1)
        return builtin(s, n, da[0]);

    const char *f = n->name;
    if (nargs == 2 && strcmp(f, "root") == 0 && is_num(da[1], 0)) {
        /* root(u, n)' = u' / (n · root(u, n)^(n-1)) */
        node_free(da[1]);
        Node *r = node_clone(n);
        return dvd(da[0], mul(node_clone(n->args[1]), power(r, sub(node_clone(n->args[1]), num(1)))));
    }
    if (nargs == 2 && strcmp(f, "log") == 0) {
        /* log(u, b) = ln(u) / ln(b) */
        node_free(da[0]);
        node_free(da[1]);
        Node *q = binop('/', call1("ln", node_clone(n->args[0])), call1("ln", node_clone(n->args[1])));
        Node *r = D(s, q, v);
        node_free(q);
        return r;
    }
    for (size_t i = 0; i < nargs; i++)
        node_free(da[i]);
    return fail(s, "%s can't be differentiated", f);
}

/* Derivative of u^w. */
static Node *D_power(State *s, const Node *n, const char *v)
{
    const Node *u = n->args[0], *w = n->args[1];
    Node *du = D(s, u, v);
    if (!du)
        return NULL;
    Node *dw = D(s, w, v);
    if (!dw) {
        node_free(du);
        return NULL;
    }
    if (is_num(dw, 0)) {
        /* w constant: w · u^(w-1) · u' */
        node_free(dw);
        if (is_num(du, 0))
            return du;
        Node *w1;
        if (w->kind == NODE_NUM) {
            w1 = node_new(NODE_NUM, 0);
            Rational one;
            rat_init(&one);
            rat_set_i64(&one, 1);
            rat_sub(&w1->value, &w->value, &one);
            rat_free(&one);
        } else {
            w1 = sub(node_clone(w), num(1));
        }
        Node *p = is_num(w1, 0) ? (node_free(w1), num(1)) : power(node_clone(u), w1);
        return mul(mul(node_clone(w), p), du);
    }
    int base_e = u->kind == NODE_VAR && strcmp(u->name, "e") == 0 && resolve(s, "e", v) == NAME_CONST;
    if (is_num(du, 0)) {
        /* u constant: u^w · ln(u) · w' (and e^w just · w') */
        node_free(du);
        Node *p = node_clone(n);
        if (!base_e)
            p = mul(p, call1("ln", node_clone(u)));
        return mul(p, dw);
    }
    /* both vary: u^w · (w' ln u + w u'/u) */
    Node *ln = base_e ? num(1) : call1("ln", node_clone(u));
    return mul(node_clone(n), add(mul(dw, ln), mul(node_clone(w), dvd(du, node_clone(u)))));
}

/* d/dv of a sum or product over k from lo to hi. */
static Node *D_big(State *s, const Node *n, const char *v)
{
    for (int i = 0; i < 2; i++) {
        Node *dl = D(s, n->args[i], v);
        if (!dl)
            return NULL;
        int moves = !is_num(dl, 0);
        node_free(dl);
        if (moves)
            return fail(s, "the limits of a %s can't depend on %s", n->kind == NODE_SUM ? "sum" : "product", v);
    }
    if (strcmp(n->name, v) == 0)
        return num(0);   /* the index hides the variable */
    if (!push(s, n->name))
        return NULL;
    Node *db = D(s, n->args[2], v);
    s->nbound--;
    if (!db || is_num(db, 0))
        return db;
    Node *body = db;
    if (n->kind == NODE_PROD)   /* (Π f)' = Π f · Σ f'/f */
        body = dvd(db, node_clone(n->args[2]));
    Node *sum = node_new(NODE_SUM, 3);
    sum->name = str_dup(n->name);
    sum->args[0] = node_clone(n->args[0]);
    sum->args[1] = node_clone(n->args[1]);
    sum->args[2] = body;
    return n->kind == NODE_PROD ? mul(node_clone(n), sum) : sum;
}

/* d/dv of the integral of f(t) for t from lo to hi: f(hi)·hi' - f(lo)·lo'
 * plus the integral of df/dv (Leibniz's rule). */
static Node *D_integral(State *s, const Node *n, const char *v)
{
    const char *t = n->name;
    Node *ends[2];
    for (int i = 0; i < 2; i++) {
        Node *dl = D(s, n->args[i], v);
        if (!dl) {
            if (i)
                node_free(ends[0]);
            return NULL;
        }
        ends[i] = mul(let(t, node_clone(n->args[i]), node_clone(n->args[2])), dl);
    }
    Node *r = sub(ends[1], ends[0]);
    if (strcmp(t, v) == 0)
        return r;
    if (!push(s, t)) {
        node_free(r);
        return NULL;
    }
    Node *db = D(s, n->args[2], v);
    s->nbound--;
    if (!db) {
        node_free(r);
        return NULL;
    }
    if (is_num(db, 0))
        return add(r, db);
    Node *in = node_new(NODE_INT, 3);
    in->name = str_dup(t);
    in->args[0] = node_clone(n->args[0]);
    in->args[1] = node_clone(n->args[1]);
    in->args[2] = db;
    return add(r, in);
}

/* d/dv of body with t = value: through body directly, and through t. */
static Node *D_let(State *s, const char *t, const Node *value, const Node *body, const char *v)
{
    Node *dval = D(s, value, v);
    if (!dval)
        return NULL;
    if (!push(s, t)) {
        node_free(dval);
        return NULL;
    }
    Node *direct = strcmp(t, v) == 0 ? num(0) : D(s, body, v);
    Node *via = direct && !is_num(dval, 0) ? D(s, body, t) : num(0);
    s->nbound--;
    if (!direct || !via) {
        node_free(dval);
        node_free(direct);
        node_free(via);
        return NULL;
    }
    return add(let(t, node_clone(value), direct), mul(let(t, node_clone(value), via), dval));
}

/* d/dv of a user function's body applied to arguments: the chain rule over
 * each parameter. The body only sees its parameters and the globals. */
static Node *D_apply(State *s, const Node *n, const char *v)
{
    int np = (int)n->nparams;
    const Node *body = n->args[np];
    Node *total = num(0);
    for (int i = 0; i < np; i++) {
        Node *da = D(s, n->args[i], v);
        if (!da) {
            node_free(total);
            return NULL;
        }
        if (is_num(da, 0)) {
            node_free(da);
            continue;
        }
        State inner = {s->d, {0}, 0, 1, s->depth, 0};
        for (int k = 0; k < np && k < MAX_BOUND; k++)
            inner.bound[inner.nbound++] = n->params[k];
        Node *dp = D(&inner, body, n->params[i]);
        if (!dp) {
            s->failed = 1;
            node_free(da);
            node_free(total);
            return NULL;
        }
        total = add(total, mul(apply(n->params, np, n->args, dp), da));
    }
    return total;
}

static Node *D(State *s, const Node *n, const char *v)
{
    if (s->failed)
        return NULL;
    switch (n->kind) {
    case NODE_NUM:
        return num(0);

    case NODE_VAR:
        switch (resolve(s, n->name, v)) {
        case NAME_VAR:
            return num(1);
        case NAME_LETTERS: {
            Node *p = split_letters(n->name);
            Node *r = D(s, p, v);
            node_free(p);
            return r;
        }
        default:
            return num(0);
        }

    case NODE_NEG: {
        Node *d = D(s, n->args[0], v);
        return d ? neg(d) : NULL;
    }

    case NODE_FACT: {
        Node *d = D(s, n->args[0], v);
        if (d && !is_num(d, 0)) {
            node_free(d);
            return fail(s, "n! can't be differentiated");
        }
        return d;
    }

    case NODE_BINOP: {
        if (n->op == '^')
            return D_power(s, n, v);
        const Node *a = n->args[0], *b = n->args[1];
        Node *da = D(s, a, v);
        if (!da)
            return NULL;
        Node *db = D(s, b, v);
        if (!db) {
            node_free(da);
            return NULL;
        }
        switch (n->op) {
        case '+':
            return add(da, db);
        case '-':
            return sub(da, db);
        case '*':
            return add(mul(da, node_clone(b)), mul(node_clone(a), db));
        case '/':
            if (is_num(db, 0)) {
                node_free(db);
                return dvd(da, node_clone(b));
            }
            return dvd(sub(mul(da, node_clone(b)), mul(node_clone(a), db)), power(node_clone(b), num(2)));
        case '%':   /* a - b·floor(a/b) */
            return sub(da, mul(db, call1("floor", binop('/', node_clone(a), node_clone(b)))));
        }
        node_free(da);
        node_free(db);
        return fail(s, "can't differentiate '%c'", n->op);
    }

    case NODE_CALL:
        return D_call(s, n, v);

    case NODE_SUM:
    case NODE_PROD:
        return D_big(s, n, v);

    case NODE_INT:
        return D_integral(s, n, v);

    case NODE_DIFF: {
        /* d/dt(f) at p is f' with t = p; differentiate that */
        if (!push(s, n->name))
            return NULL;
        Node *inner = D(s, n->args[0], n->name);
        s->nbound--;
        if (!inner)
            return NULL;
        Node *at = let(n->name, n->nargs > 1 ? node_clone(n->args[1]) : var(n->name), inner);
        Node *r = D(s, at, v);
        node_free(at);
        return r;
    }

    case NODE_LET:
        return D_let(s, n->name, n->args[0], n->args[1], v);

    case NODE_APPLY:
        return D_apply(s, n, v);

    default:
        return fail(s, "that can't be differentiated");
    }
}

Node *derive(const Node *n, const char *v, const Deriver *d)
{
    State s = {d, {0}, 0, 0, 0, 0};
    return D(&s, n, v);
}

/* ---- Polynomials --------------------------------------------------------------- */

static int is_constant(State *s, const Node *n, const char *v)
{
    State copy = *s;
    Node *d = D(&copy, n, v);
    int c = d && is_num(d, 0);
    node_free(d);
    return c;
}

static int degree(State *s, const Node *n, const char *v)
{
    int a, b;
    switch (n->kind) {
    case NODE_NUM:
        return 0;
    case NODE_VAR:
        switch (resolve(s, n->name, v)) {
        case NAME_VAR:
            return 1;
        case NAME_LETTERS:
            a = 0;
            for (const char *p = n->name; *p; p++)
                a += *p == v[0] && v[1] == '\0';
            return a;
        default:
            return 0;
        }
    case NODE_NEG:
        return degree(s, n->args[0], v);
    case NODE_BINOP:
        a = degree(s, n->args[0], v);
        b = degree(s, n->args[1], v);
        if (a < 0 || b < 0)
            return -1;
        switch (n->op) {
        case '+':
        case '-':
            return a > b ? a : b;
        case '*':
            return a + b > MAX_DEGREE ? -1 : a + b;
        case '/':
            return b == 0 ? a : -1;
        case '^': {
            int64_t k;
            if (a == 0 && b == 0)
                return 0;
            if (b == 0 && n->args[1]->kind == NODE_NUM && rat_to_i64(&n->args[1]->value, &k) == 0 && k >= 0 &&
                k <= MAX_DEGREE && a * k <= MAX_DEGREE)
                return (int)(a * k);
            return -1;
        }
        default:
            return a == 0 && b == 0 ? 0 : -1;
        }
    case NODE_SUM:
        if (degree(s, n->args[0], v) != 0 || degree(s, n->args[1], v) != 0)
            return -1;
        if (strcmp(n->name, v) == 0)
            return 0;
        if (!push(s, n->name))
            return -1;
        a = degree(s, n->args[2], v);
        s->nbound--;
        return a;
    default:
        return is_constant(s, n, v) ? 0 : -1;
    }
}

int poly_degree(const Node *n, const char *v, const Deriver *d)
{
    char err[200];
    Deriver quiet = *d;   /* "can't be differentiated" just means "not a polynomial" */
    quiet.err = err;
    quiet.errlen = sizeof err;
    State s = {&quiet, {0}, 0, 0, 0, 0};
    return degree(&s, n, v);
}

/* ---- Integrals ----------------------------------------------------------------- */

void newton_cotes(Rational *w, int deg)
{
    /* w[j] = (1/deg) · integral over [0, deg] of the Lagrange polynomial
     * that is 1 at j and 0 at the other whole numbers 0..deg. */
    Rational *poly = malloc((size_t)(deg + 1) * sizeof *poly);
    Rational t, acc, pw, scale;
    if (!poly) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    rat_init(&t);
    rat_init(&acc);
    rat_init(&pw);
    rat_init(&scale);
    for (int i = 0; i <= deg; i++)
        rat_init(&poly[i]);
    for (int j = 0; j <= deg; j++) {
        /* poly = prod over m != j of (s - m), lowest power first */
        int len = 1;
        rat_set_i64(&poly[0], 1);
        for (int m = 0; m <= deg; m++) {
            if (m == j)
                continue;
            rat_set_i64(&poly[len], 0);
            for (int i = len; i >= 1; i--) {
                rat_set_i64(&t, -m);
                rat_mul(&t, &t, &poly[i]);
                rat_add(&poly[i], &poly[i - 1], &t);
            }
            rat_set_i64(&t, -m);
            rat_mul(&poly[0], &poly[0], &t);
            len++;
        }
        /* integrate from 0 to deg: sum of c_i deg^(i+1) / (i+1) */
        rat_set_i64(&acc, 0);
        rat_set_i64(&pw, deg);
        for (int i = 0; i < len; i++) {
            rat_set_i64(&t, i + 1);
            rat_div(&t, &pw, &t);
            rat_mul(&t, &t, &poly[i]);
            rat_add(&acc, &acc, &t);
            rat_set_i64(&t, deg);
            rat_mul(&pw, &pw, &t);
        }
        /* divide by prod over m != j of (j - m), and by deg */
        rat_set_i64(&scale, deg);
        for (int m = 0; m <= deg; m++) {
            if (m == j)
                continue;
            rat_set_i64(&t, j - m);
            rat_mul(&scale, &scale, &t);
        }
        rat_div(&w[j], &acc, &scale);
    }
    for (int i = 0; i <= deg; i++)
        rat_free(&poly[i]);
    free(poly);
    rat_free(&t);
    rat_free(&acc);
    rat_free(&pw);
    rat_free(&scale);
}

/*
 * Tanh-sinh quadrature. Substituting x = tanh(π/2 · sinh t) turns the
 * integral over [-1, 1] into one over all t whose integrand dies off
 * doubly exponentially, so the trapezoid rule with step h converges very
 * fast: each halving of h roughly doubles the correct digits. Points crowd
 * towards the ends, which also copes with integrands that blow up there,
 * like 1/sqrt(x) at 0.
 *
 * For a step t, with G = e^(π sinh t) and c = 1/(G + 1), the points are
 * a + (b-a)c and b - (b-a)c, and both have weight 2π cosh(t) c (1 - c).
 * Working with c, the distance to the end, keeps points near the ends exact.
 */
typedef struct {
    QuadFn f;
    void *ctx;
    const Rational *a, *b;
    Rational width;   /* b - a */
    Rational pi;
    int prec;
    Rational sre, sim, sabs;   /* sums of weight · f, and of weight · |f| */
} Quad;

static void round_to(Rational *r, int prec)
{
    rat_round_sig(r, r, prec);
}

static void abs_add(Rational *acc, const Rational *w, const Rational *x)
{
    Rational t;
    rat_init(&t);
    rat_mul(&t, w, x);
    if (rat_sign(&t) < 0)
        rat_neg(&t);
    rat_add(acc, acc, &t);
    round_to(acc, 12);
    rat_free(&t);
}

/* Adds the points for step t; *weight gets their weight. */
static int quad_step(Quad *q, const Rational *t, Rational *weight)
{
    Rational e, ie, sh, ch, g, c, w, x, fre, fim, one;
    Rational *all[] = {&e, &ie, &sh, &ch, &g, &c, &w, &x, &fre, &fim, &one};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        rat_init(all[i]);
    int rc = 0, P = q->prec;
    rat_set_i64(&one, 1);

    if (rat_is_zero(t)) {   /* the middle: c = 1/2, weight π/2 */
        rat_set_i64(&c, 2);
        rat_div(&w, &q->pi, &c);
        rat_div(&c, &one, &c);
    } else {
        ap_exp(&e, t, P);
        rat_div(&ie, &one, &e);
        rat_sub(&sh, &e, &ie);
        rat_add(&ch, &e, &ie);   /* 2 sinh t and 2 cosh t */
        rat_mul(&g, &q->pi, &sh);
        rat_set_i64(&x, 2);
        rat_div(&g, &g, &x);
        round_to(&g, P);
        ap_exp(&g, &g, P);
        rat_add(&g, &g, &one);
        rat_div(&c, &one, &g);   /* c = 1 / (G + 1) */
        round_to(&c, P);
        rat_sub(&x, &one, &c);
        rat_mul(&w, &c, &x);
        rat_mul(&w, &w, &ch);
        rat_mul(&w, &w, &q->pi);   /* 2π cosh t · c(1-c), as π · (2 cosh t) · c(1-c) */
        round_to(&w, P);
    }
    rat_copy(weight, &w);

    int points = rat_is_zero(t) ? 1 : 2;
    for (int i = 0; i < points && rc == 0; i++) {
        rat_mul(&x, &q->width, &c);
        round_to(&x, P);
        if (i == 0)
            rat_add(&x, q->a, &x);
        else
            rat_sub(&x, q->b, &x);
        rat_set_i64(&fre, 0);
        rat_set_i64(&fim, 0);
        if (q->f(q->ctx, &x, rat_log10_abs(&c) < -4, &fre, &fim)) {
            rc = -1;
            break;
        }
        rat_mul(&fre, &fre, &w);
        rat_mul(&fim, &fim, &w);
        round_to(&fre, P);
        round_to(&fim, P);
        rat_add(&q->sre, &q->sre, &fre);
        rat_add(&q->sim, &q->sim, &fim);
        abs_add(&q->sabs, &one, &fre);
        abs_add(&q->sabs, &one, &fim);
    }
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        rat_free(all[i]);
    return rc;
}

/* log10 of |re| + |im|, roughly; -HUGE_VAL for 0 */
static double magnitude(const Rational *re, const Rational *im)
{
    double a = rat_log10_abs(re), b = rat_log10_abs(im);
    return a > b ? a : b;
}

int quad_tanh_sinh(const Rational *a, const Rational *b, int digits, int prec, QuadFn f, void *ctx,
                   Rational *re, Rational *im, int *settled, double size_log10)
{
    enum { MAX_LEVEL = 9 };
    Quad q;
    memset(&q, 0, sizeof q);
    q.f = f;
    q.ctx = ctx;
    q.a = a;
    q.b = b;
    q.prec = prec + 5;
    Rational t, w, h, half, ire, iim, pre, pim, d;
    Rational *all[] = {&q.width, &q.pi, &q.sre, &q.sim, &q.sabs, &t, &w, &h, &half, &ire, &iim, &pre, &pim, &d};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        rat_init(all[i]);
    rat_sub(&q.width, b, a);
    ap_pi(&q.pi, q.prec);
    rat_set_i64(&t, 2);
    rat_div(&half, &q.width, &t);
    int rc = -2;

    /* Level 0: steps t = 0, 1, 2, ... until the weights (and the terms) are
     * negligible. That also sets how far the finer levels go. */
    int tmax = 0;
    for (int k = 0;; k++) {
        Rational before_re, before_im;
        rat_init(&before_re);
        rat_init(&before_im);
        rat_copy(&before_re, &q.sre);
        rat_copy(&before_im, &q.sim);
        rat_set_i64(&t, k);
        int frc = quad_step(&q, &t, &w);
        double term = 0;
        if (frc == 0) {
            rat_sub(&before_re, &q.sre, &before_re);
            rat_sub(&before_im, &q.sim, &before_im);
            term = magnitude(&before_re, &before_im);
        }
        rat_free(&before_re);
        rat_free(&before_im);
        if (frc) {
            rc = -1;
            goto done;
        }
        tmax = k;
        double wl = rat_log10_abs(&w), total = magnitude(&q.sre, &q.sim);
        if (k > 0 && ((wl < -(q.prec + 3) && term < total - (q.prec + 3)) || wl < -2 * q.prec || k >= 12))
            break;
    }

    /* Finer levels add the steps halfway between the ones so far. */
    rat_set_i64(&h, 1);
    rat_mul(&pre, &q.sre, &half);
    rat_mul(&pim, &q.sim, &half);
    for (int level = 1; level <= MAX_LEVEL; level++) {
        rat_set_i64(&t, 2);
        rat_div(&h, &h, &t);   /* h = 2^-level */
        int64_t steps = (int64_t)tmax << level;
        for (int64_t j = 1; j < steps; j += 2) {
            Rational jj;
            rat_init(&jj);
            rat_set_i64(&jj, j);
            rat_mul(&t, &jj, &h);
            rat_free(&jj);
            if (quad_step(&q, &t, &w)) {
                rc = -1;
                goto done;
            }
        }
        /* integral ≈ (b-a)/2 · h · sum */
        rat_mul(&ire, &q.sre, &half);
        rat_mul(&ire, &ire, &h);
        rat_mul(&iim, &q.sim, &half);
        rat_mul(&iim, &iim, &h);
        round_to(&ire, q.prec);
        round_to(&iim, q.prec);

        rat_sub(&d, &ire, &pre);
        double diff = rat_log10_abs(&d);
        rat_sub(&d, &iim, &pim);
        double diff_im = rat_log10_abs(&d);
        if (diff_im > diff)
            diff = diff_im;
        rat_mul(&d, &q.sabs, &half);
        rat_mul(&d, &d, &h);
        double scale = rat_log10_abs(&d);   /* size of the integral of |f| */
        double size = magnitude(&ire, &iim);
        if (size > scale)
            scale = size;
        if (size_log10 > scale)
            scale = size_log10;
        int agree = diff == -HUGE_VAL ? digits + 2 : (int)floor(scale - diff);
        if (level >= 3 && (agree >= digits + 2 || (level == MAX_LEVEL && agree >= 8))) {
            /*
             * Settled. Each level roughly doubles the correct digits for
             * smooth functions, so this level is right to more digits than
             * agree with the last one. With a corner or a jump inside, it
             * only creeps closer: then keep just the digits that agree.
             */
            *settled = agree >= digits + 2 ? digits : agree - 2;
            if (*settled < digits) {
                round_to(&ire, *settled);
                round_to(&iim, *settled);
            }
            /* A result far below the scale is zero to the digits we have. */
            if (rat_log10_abs(&ire) < scale - (*settled + 1))
                rat_set_i64(&ire, 0);
            if (rat_log10_abs(&iim) < scale - (*settled + 1))
                rat_set_i64(&iim, 0);
            rat_swap(re, &ire);
            rat_swap(im, &iim);
            rc = 0;
            goto done;
        }
        rat_swap(&pre, &ire);
        rat_swap(&pim, &iim);
    }

done:
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        rat_free(all[i]);
    return rc;
}
