#include "graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUARD 10   /* extra digits for the exact refinement */

static char x_name[] = "x";
static char *const X_PARAM[] = {x_name};

static char *dup(const char *s)
{
    char *d = malloc(strlen(s) + 1);
    if (!d)
        exit(1);
    return strcpy(d, s);
}

static void row_clear(GraphRow *r)
{
    free(r->name);
    free(r->error);
    fexpr_free(r->fx);
    node_free(r->tree);
    free(r->text);
    memset(r, 0, sizeof *r);
}

void graph_free(Graph *g)
{
    for (int i = 0; i < g->count; i++)
        row_clear(&g->rows[i]);
    fenv_free(g->env);
    g->env = NULL;
    g->count = 0;
}

static void set_error(GraphRow *r, const char *msg)
{
    r->kind = GR_ERROR;
    r->plots = 0;
    free(r->error);
    r->error = dup(msg);
}

/* Is n a plain number like 2 or -0.5 (so the line can be a slider)? */
static int plain_number(const Node *n)
{
    return n->kind == NODE_NUM || (n->kind == NODE_NEG && n->args[0]->kind == NODE_NUM);
}

void graph_build(Graph *g, const Calc *c, char *const *texts, int n)
{
    graph_free(g);
    g->env = fenv_new(&c->syms, c->degrees);
    g->count = n < GRAPH_MAX_ROWS ? n : GRAPH_MAX_ROWS;

    /* 1. Parse every line and register local functions. */
    for (int i = 0; i < g->count; i++) {
        GraphRow *r = &g->rows[i];
        char err[256];
        r->text = dup(texts[i]);
        if (!texts[i][0]) {
            r->kind = GR_EMPTY;
            continue;
        }
        r->tree = parse(texts[i], err, sizeof err);
        if (!r->tree) {
            set_error(r, err);
            continue;
        }
        Node *t = r->tree;
        if (t->kind == NODE_ASSIGN && strcmp(t->name, "y") == 0) {
            r->kind = GR_PLOT;
        } else if (t->kind == NODE_ASSIGN && strcmp(t->name, "x") == 0) {
            set_error(r, "x = ... lines aren't supported yet");
        } else if (t->kind == NODE_ASSIGN || t->kind == NODE_DEFUN) {
            r->name = dup(t->name);
            if (eval_is_reserved(t->name)) {
                snprintf(err, sizeof err, "'%s' is built in", t->name);
                set_error(r, err);
            } else if (t->kind == NODE_DEFUN) {
                r->kind = GR_FUNC;
                fenv_set_func(g->env, t->name, t->params, (int)t->nparams, t->args[0]);
            } else {
                r->kind = plain_number(t->args[0]) ? GR_SLIDER : GR_VAR;
            }
        } else {
            r->kind = GR_PLOT;
        }
    }

    /* 2. Variables, in order; twice, so one can use another defined below it. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < g->count; i++) {
            GraphRow *r = &g->rows[i];
            if (r->kind != GR_SLIDER && r->kind != GR_VAR)
                continue;
            FExpr *f = fexpr_compile(g->env, r->tree->args[0], NULL, 0);
            r->value = fexpr_eval(f, NULL);
            fexpr_free(f);
            fenv_set_var(g->env, r->name, r->value);
        }
    }

    /* 3. Curves and values, now that every name is known. */
    for (int i = 0; i < g->count; i++) {
        GraphRow *r = &g->rows[i];
        if (r->kind == GR_PLOT) {
            const Node *e = r->tree->kind == NODE_ASSIGN ? r->tree->args[0] : r->tree;
            r->fx = fexpr_compile(g->env, e, X_PARAM, 1);
            /* "2 + 3" is a value to show; "y = 5" is still a line. */
            if (!fexpr_uses_params(r->fx) && r->tree->kind != NODE_ASSIGN) {
                r->kind = GR_VALUE;
                r->value = fexpr_eval(r->fx, (double[]){0});
            } else {
                r->plots = 1;
            }
        } else if (r->kind == GR_FUNC) {
            r->fx = fexpr_compile(g->env, r->tree->args[0], r->tree->params, (int)r->tree->nparams);
            r->plots = r->tree->nparams == 1;
        } else if (r->kind == GR_VAR || r->kind == GR_SLIDER) {
            /* compiled only to find names that need sliders */
            r->fx = fexpr_compile(g->env, r->tree->args[0], NULL, 0);
            if (isnan(r->value) && !graph_unknowns(r, &(const char *const *){0}))
                set_error(r, "undefined");
        }
    }
}

double graph_y(const GraphRow *r, double x)
{
    return r->plots ? fexpr_eval(r->fx, &x) : NAN;
}

int graph_unknowns(const GraphRow *r, const char *const **names)
{
    if (!r->fx) {
        *names = NULL;
        return 0;
    }
    return fexpr_unknowns(r->fx, names);
}

/* ---- Exact refinement ---------------------------------------------------------- */

typedef struct {
    Calc *c;
    Symbols syms;     /* the user's definitions plus the graph's own */
    int prec;
} Exact;

/* Copies a function definition by re-reading its text. */
static void define_from_text(Symbols *s, const char *line)
{
    char err[256];
    Node *t = parse(line, err, sizeof err);
    if (t && t->kind == NODE_DEFUN) {
        sym_set_func(s, t->name, t->params, (int)t->nparams, t->args[0], t->text);
        t->params = NULL;
        t->nparams = 0;
        t->args[0] = NULL;
    }
    node_free(t);
}

static void func_line(char *buf, size_t n, const UserFunc *f)
{
    size_t len = (size_t)snprintf(buf, n, "%s(", f->name);
    for (int i = 0; i < f->nparams && len < n; i++)
        len += (size_t)snprintf(buf + len, n - len, "%s%s", i ? ", " : "", f->params[i]);
    if (len < n)
        snprintf(buf + len, n - len, ") = %s", f->text);
}

static void exact_init(Exact *x, Calc *c, const Graph *g, int prec)
{
    x->c = c;
    x->prec = prec;
    sym_init(&x->syms);
    char line[4096];
    for (int i = 0; i < c->syms.nvars; i++)
        sym_set_var(&x->syms, c->syms.vars[i].name, &c->syms.vars[i].value);
    for (int i = 0; i < c->syms.nfuncs; i++) {
        func_line(line, sizeof line, &c->syms.funcs[i]);
        define_from_text(&x->syms, line);
    }
    for (int i = 0; i < g->count; i++) {
        const GraphRow *r = &g->rows[i];
        if (r->kind == GR_FUNC) {
            define_from_text(&x->syms, r->text);
        } else if (r->kind == GR_SLIDER || r->kind == GR_VAR) {
            Value v;
            char err[256];
            val_init(&v);
            EvalOptions opt = {prec, c->degrees, &x->syms};
            if (eval(r->tree->args[0], NULL, &opt, &v, err, sizeof err) == 0)
                sym_set_var(&x->syms, r->name, &v);
            val_free(&v);
        }
    }
}

/* f(x) for curve `row`, evaluated exactly (or to x->prec digits). */
static int exact_at(Exact *x, const GraphRow *r, const Rational *at, Value *out)
{
    const Node *body;
    const char *param = "x";
    if (r->kind == GR_FUNC) {
        body = r->tree->args[0];
        param = r->tree->params[0];
    } else {
        body = r->tree->kind == NODE_ASSIGN ? r->tree->args[0] : r->tree;
    }
    Value xv;
    val_init(&xv);
    rat_copy(&xv.r, at);
    Binding b = {param, &xv, NULL};
    EvalOptions opt = {x->prec, x->c->degrees, &x->syms};
    char err[256];
    int rc = eval(body, &b, &opt, out, err, sizeof err);
    val_free(&xv);
    return rc == 0 && rat_is_zero(&out->im) ? 0 : -1;
}

/* f(at) - g(at) for a crossing, or just f(at) */
static int diff_at(Exact *x, const Graph *g, int row, int other, const Rational *at, Value *out)
{
    if (exact_at(x, &g->rows[row], at, out))
        return -1;
    if (other < 0)
        return 0;
    Value v;
    val_init(&v);
    int rc = exact_at(x, &g->rows[other], at, &v);
    if (rc == 0) {
        rat_sub(&out->r, &out->r, &v.r);
        out->exact = out->exact && v.exact;
    }
    val_free(&v);
    return rc;
}

static int rat_from_double(Rational *r, double d)
{
    char buf[64];
    if (!isfinite(d))
        return -1;
    snprintf(buf, sizeof buf, "%.17g", fabs(d));
    if (rat_set_decimal(r, buf, strlen(buf)))
        return -1;
    if (d < 0)
        rat_neg(r);
    return 0;
}

static int between(const Rational *v, const Rational *lo, const Rational *hi)
{
    Rational t;
    rat_init(&t);
    rat_sub(&t, v, lo);
    int ok = rat_sign(&t) >= 0;
    rat_sub(&t, hi, v);
    ok = ok && rat_sign(&t) >= 0;
    rat_free(&t);
    return ok;
}

/*
 * Simple fractions near `estimate` (its continued-fraction convergents), so
 * a root at 2 or 1/3 can be found exactly instead of to 30 digits.
 */
static int candidates(double estimate, Rational *out, int max)
{
    int n = 0;
    double r = estimate;
    long long p0 = 1, q0 = 0, p1 = (long long)floor(r), q1 = 1;
    double frac = r - floor(r);
    for (int i = 0; i < 12 && n < max && q1 <= 1000000; i++) {
        Rational *c = &out[n++];
        rat_set_i64(c, p1);
        if (q1 != 1) {
            Rational d;
            rat_init(&d);
            rat_set_i64(&d, q1);
            rat_div(c, c, &d);
            rat_free(&d);
        }
        if (frac < 1e-12)
            break;
        double inv = 1 / frac;
        long long a = (long long)floor(inv);
        frac = inv - floor(inv);
        long long p2 = a * p1 + p0, q2 = a * q1 + q0;
        p0 = p1;
        q0 = q1;
        p1 = p2;
        q1 = q2;
    }
    return n;
}

static char *value_text(Calc *c, const Value *v)
{
    char *s = val_to_str(v, c->digits, c->sci);
    char *note = strstr(s, "  (=");
    if (note)
        *note = '\0';
    return s;
}

/* Halves [lo, hi] (whose ends have opposite signs of f) until it's tiny. */
static int bisect(Exact *x, const Graph *g, int row, int other, int derivative, Rational *lo, Rational *hi,
                  Value *root)
{
    int digits = x->c->digits;
    Rational mid, two, h, width, tol, tmp;
    Value v, w;
    rat_init(&mid);
    rat_init(&two);
    rat_init(&h);
    rat_init(&width);
    rat_init(&tol);
    rat_init(&tmp);
    val_init(&v);
    val_init(&w);
    rat_set_i64(&two, 2);
    char hbuf[32];
    snprintf(hbuf, sizeof hbuf, "1e-%d", digits + 6);
    rat_set_decimal(&h, hbuf, strlen(hbuf));
    snprintf(hbuf, sizeof hbuf, "1e-%d", digits + 4);
    rat_set_decimal(&tol, hbuf, strlen(hbuf));

    /* sign at the low end */
    int rc = -1, s_lo = 0;
    for (int end = 0; end < 2; end++) {
        Rational *at = end ? hi : lo;
        if (derivative) {
            rat_add(&tmp, at, &h);
            rat_sub(&mid, at, &h);
            if (diff_at(x, g, row, other, &tmp, &v) || diff_at(x, g, row, other, &mid, &w))
                goto done;
            rat_sub(&v.r, &v.r, &w.r);
        } else if (diff_at(x, g, row, other, at, &v)) {
            goto done;
        }
        int s = rat_sign(&v.r);
        if (end == 0)
            s_lo = s;
        else if (s == s_lo || s == 0 || s_lo == 0)
            goto done;   /* no sign change: not a root after all */
    }

    for (int i = 0; i < 400; i++) {
        rat_sub(&width, hi, lo);
        rat_copy(&tmp, lo);
        if (rat_sign(&tmp) < 0)
            rat_neg(&tmp);
        if (rat_size_log10(&tmp) > 0 && rat_log10_abs(&tmp) > 0)
            rat_mul(&tmp, &tmp, &tol);
        else
            rat_copy(&tmp, &tol);
        rat_sub(&tmp, &width, &tmp);
        if (rat_sign(&tmp) <= 0)
            break;
        rat_add(&mid, lo, hi);
        rat_div(&mid, &mid, &two);
        rat_round_sig(&mid, &mid, digits + 8);   /* keep the fractions from growing */
        int s;
        if (derivative) {
            Rational a, b;
            rat_init(&a);
            rat_init(&b);
            rat_add(&a, &mid, &h);
            rat_sub(&b, &mid, &h);
            int bad = diff_at(x, g, row, other, &a, &v) || diff_at(x, g, row, other, &b, &w);
            rat_free(&a);
            rat_free(&b);
            if (bad)
                goto done;
            rat_sub(&v.r, &v.r, &w.r);
        } else if (diff_at(x, g, row, other, &mid, &v)) {
            goto done;
        }
        s = rat_sign(&v.r);
        if (s == 0) {
            rat_copy(lo, &mid);
            rat_copy(hi, &mid);
            break;
        }
        if (s == s_lo)
            rat_copy(lo, &mid);
        else
            rat_copy(hi, &mid);
    }
    rat_add(&root->r, lo, hi);
    rat_div(&root->r, &root->r, &two);
    rat_round_sig(&root->r, &root->r, digits + 3);
    root->exact = 0;
    rc = 0;

done:
    rat_free(&mid);
    rat_free(&two);
    rat_free(&h);
    rat_free(&width);
    rat_free(&tol);
    rat_free(&tmp);
    val_free(&v);
    val_free(&w);
    return rc;
}

int graph_exact_point(Calc *c, const Graph *g, int row, int other, PointKind kind, double lo, double hi,
                      char **xs, char **ys)
{
    int derivative = kind == PT_MAX || kind == PT_MIN;
    int prec = c->digits + GUARD + (derivative ? c->digits + 10 : 0);
    Exact x;
    Rational a, b, cand[16];
    Value root, v, w;
    int rc = -1, found = 0;
    exact_init(&x, c, g, prec);
    rat_init(&a);
    rat_init(&b);
    for (int i = 0; i < 16; i++)
        rat_init(&cand[i]);
    val_init(&root);
    val_init(&v);
    val_init(&w);

    if (kind == PT_YINT) {
        found = 1;   /* x = 0 exactly */
    } else if (rat_from_double(&a, lo) == 0 && rat_from_double(&b, hi) == 0) {
        /* Try simple fractions first: a root or turning point at 1/3 is exact. */
        int n = candidates((lo + hi) / 2, cand, 16);
        for (int i = 0; i < n && !found; i++) {
            if (!between(&cand[i], &a, &b))
                continue;
            if (derivative) {
                /* symmetric differences of 0 at two step sizes: f is flat there */
                found = 1;
                for (int k = 1; k <= 2 && found; k++) {
                    Rational h, p, m;
                    rat_init(&h);
                    rat_init(&p);
                    rat_init(&m);
                    rat_set_i64(&h, k);
                    rat_set_i64(&p, 1000);
                    rat_div(&h, &h, &p);
                    rat_add(&p, &cand[i], &h);
                    rat_sub(&m, &cand[i], &h);
                    found = diff_at(&x, g, row, other, &p, &v) == 0 && diff_at(&x, g, row, other, &m, &w) == 0 &&
                            v.exact && w.exact && rat_sign(&v.r) == rat_sign(&w.r) &&
                            bi_cmp(&v.r.num, &w.r.num) == 0 && bi_cmp(&v.r.den, &w.r.den) == 0;
                    rat_free(&h);
                    rat_free(&p);
                    rat_free(&m);
                }
            } else {
                found = diff_at(&x, g, row, other, &cand[i], &v) == 0 && v.exact && rat_is_zero(&v.r);
            }
            if (found) {
                rat_copy(&root.r, &cand[i]);
                root.exact = 1;
            }
        }
        if (!found && bisect(&x, g, row, other, derivative, &a, &b, &root) == 0)
            found = 1;
    }

    if (found && exact_at(&x, &g->rows[row], &root.r, &v) == 0) {
        x.prec = c->digits + GUARD;
        exact_at(&x, &g->rows[row], &root.r, &v);
        if (!root.exact) {
            /* the x found is approximate, so is y; clear rounding noise in roots */
            v.exact = 0;
            if (kind == PT_ROOT && rat_log10_abs(&v.r) < -(c->digits + 2))
                rat_set_i64(&v.r, 0);
            rat_round_sig(&v.r, &v.r, c->digits + 3);
        }
        *xs = value_text(c, &root);
        *ys = value_text(c, &v);
        rc = 0;
    }

    sym_free(&x.syms);
    rat_free(&a);
    rat_free(&b);
    for (int i = 0; i < 16; i++)
        rat_free(&cand[i]);
    val_free(&root);
    val_free(&v);
    val_free(&w);
    return rc;
}
