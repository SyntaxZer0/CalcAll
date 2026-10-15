#include "eval.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "approx.h"
#include "cmath.h"

/* Safety limits so a typo like 9^9^9 fails fast instead of freezing. */
#define MAX_RESULT_DIGITS 1000000
#define MAX_FACTORIAL 50000
#define MAX_TERMS 1000000
#define MAX_EXACT_ROOT 100   /* n-th roots are only tried exactly up to this n */

#define EXTRA 5              /* extra digits for intermediate approximate steps */
#define LOG10_E 0.43429448190325182765

typedef struct {
    char *msg;
    size_t len;
    int prec;
    int degrees;
} Ctx;

void val_init(Value *v)
{
    rat_init(&v->r);
    rat_init(&v->im);
    v->exact = 1;
}

void val_free(Value *v)
{
    rat_free(&v->r);
    rat_free(&v->im);
}

void val_copy(Value *dst, const Value *src)
{
    rat_copy(&dst->r, &src->r);
    rat_copy(&dst->im, &src->im);
    dst->exact = src->exact;
}

void val_swap(Value *a, Value *b)
{
    rat_swap(&a->r, &b->r);
    rat_swap(&a->im, &b->im);
    int t = a->exact;
    a->exact = b->exact;
    b->exact = t;
}

static int fail(Ctx *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->msg, c->len, fmt, ap);
    va_end(ap);
    return -1;
}

static void set_int(Value *v, int64_t n)
{
    rat_set_i64(&v->r, n);
    rat_set_i64(&v->im, 0);
    v->exact = 1;
}

static int is_real(const Value *v)
{
    return rat_is_zero(&v->im);
}

static void get_cx(Complex *z, const Value *v)
{
    rat_copy(&z->re, &v->r);
    rat_copy(&z->im, &v->im);
}

/* Moves z into v (z is left holding v's old parts, to be freed). */
static void put_cx(Value *v, Complex *z, int exact)
{
    rat_swap(&v->r, &z->re);
    rat_swap(&v->im, &z->im);
    v->exact = exact;
}

/* Marks v as approximate and rounds it to the working precision. */
static void approx(Value *v, Ctx *c)
{
    v->exact = 0;
    if (is_real(v)) {
        rat_round_sig(&v->r, &v->r, c->prec);
        return;
    }
    Complex z;
    cx_init(&z);
    put_cx(v, &z, 0);   /* swap the parts out, round them, swap them back */
    cx_round(&z, c->prec);
    put_cx(v, &z, 0);
    cx_free(&z);
}

/*
 * An approximate result far smaller than the rounding error of the inputs
 * it came from is just noise: sqrt(2)^2 - 2 or sin(pi) should be 0, not
 * 1e-31. ref_log10 is the size (log10) of those inputs.
 */
static void clamp_noise(Value *v, double ref_log10, Ctx *c)
{
    if (!v->exact && !rat_is_zero(&v->r) && rat_log10_abs(&v->r) < ref_log10 - (c->prec - 3))
        rat_set_i64(&v->r, 0);
}

static int is_odd(const BigInt *x)
{
    return x->len > 0 && (x->limbs[0] & 1);
}

static int is_one(const Rational *r)
{
    return bi_is_one(&r->num) && bi_is_one(&r->den);
}

static int need_int(const Value *v, const char *what, int64_t *out, Ctx *c)
{
    if (!is_real(v))
        return fail(c, "%s must be a real number", what);
    if (!rat_is_int(&v->r))
        return fail(c, "%s must be a whole number", what);
    if (rat_to_i64(&v->r, out))
        return fail(c, "%s is too large", what);
    return 0;
}

static int check_size(const Value *v, Ctx *c)
{
    if (rat_size_log10(&v->r) > MAX_RESULT_DIGITS || rat_size_log10(&v->im) > MAX_RESULT_DIGITS)
        return fail(c, "result has more than %d digits", MAX_RESULT_DIGITS);
    return 0;
}

static int too_big(Ctx *c, double digits)
{
    return fail(c, "result would have about %.0f digits (limit is %d)", digits, MAX_RESULT_DIGITS);
}

static int eval_node(const Node *n, const Binding *env, Value *out, Ctx *c);
static int complex_power(Value *out, const Value *a, const Value *b, Ctx *c);
static int complex_root(Value *out, const Value *x, int64_t n, Ctx *c);
static int complex_log(Value *out, const Value *x, const Value *base, Ctx *c);
static int complex_func(Value *out, const Value *x, int f, Ctx *c);

/* ---- Powers and roots ---------------------------------------------------- */

/* If x (>= 0) has an exact n-th root, like 27/8 -> 3/2, store it and return 1. */
static int exact_root(Rational *out, const Rational *x, int64_t n)
{
    if (n < 1 || n > MAX_EXACT_ROOT)
        return 0;
    Rational root, check;
    rat_init(&root);
    rat_init(&check);
    bi_root(&root.num, &x->num, (unsigned)n);
    bi_root(&root.den, &x->den, (unsigned)n);
    rat_pow(&check, &root, n);
    int ok = bi_cmp(&check.num, &x->num) == 0 && bi_cmp(&check.den, &x->den) == 0;
    if (ok)
        rat_swap(out, &root);
    rat_free(&root);
    rat_free(&check);
    return ok;
}

/* out = a^y = e^(y ln a) for a > 0. */
static int pow_approx(Value *out, const Rational *a, const Rational *y, Ctx *c)
{
    double digits = rat_to_double(y) * rat_log10_abs(a);   /* log10 of the result */
    if (fabs(digits) > MAX_RESULT_DIGITS)
        return too_big(c, fabs(digits));
    /* y ln a must be accurate to prec digits *after* the decimal point, so
     * add as many digits as its integer part has. */
    double size = fabs(digits) / LOG10_E;
    int p2 = c->prec + EXTRA + (size > 1 ? (int)ceil(log10(size)) : 0);
    Rational l;
    rat_init(&l);
    ap_ln(&l, a, p2);
    rat_mul(&l, &l, y);
    rat_round_sig(&l, &l, p2);
    ap_exp(&out->r, &l, c->prec);
    out->exact = 0;
    rat_free(&l);
    return 0;
}

/* out = |x|^(1/n), approximately. */
static void root_approx(Rational *out, const Rational *ax, int64_t n, Ctx *c)
{
    if (n == 2) {
        ap_sqrt(out, ax, c->prec);
        return;
    }
    int p2 = c->prec + EXTRA;
    Rational nn;
    rat_init(&nn);
    rat_set_i64(&nn, n);
    ap_ln(out, ax, p2);
    rat_div(out, out, &nn);
    rat_round_sig(out, out, p2);
    ap_exp(out, out, c->prec);
    rat_free(&nn);
}

static int int_power(Value *out, const Value *a, int64_t n, int exact, Ctx *c)
{
    if (rat_is_zero(&a->r)) {
        if (n < 0)
            return fail(c, "division by zero (0 to a negative power)");
        rat_set_i64(&out->r, n == 0 ? 1 : 0);
        out->exact = exact;
        return 0;
    }
    if (n == 0) {   /* anything to the exact power 0 is exactly 1 */
        rat_set_i64(&out->r, 1);
        out->exact = 1;
        return 0;
    }
    double digits = fabs((double)n) * rat_size_log10(&a->r);
    if (a->exact) {
        if (digits > MAX_RESULT_DIGITS)
            return too_big(c, digits);
        rat_pow(&out->r, &a->r, n);
        out->exact = exact;
        if (!exact)
            approx(out, c);
        return 0;
    }
    if (digits <= 20000) {
        rat_pow(&out->r, &a->r, n);
        approx(out, c);
        return 0;
    }
    /* A huge power of an approximate number: go through e^(n ln|a|). */
    Rational abs_a, e;
    rat_init(&abs_a);
    rat_init(&e);
    rat_copy(&abs_a, &a->r);
    if (rat_sign(&abs_a) < 0)
        rat_neg(&abs_a);
    rat_set_i64(&e, n);
    int rc = pow_approx(out, &abs_a, &e, c);
    if (rc == 0 && rat_sign(&a->r) < 0 && (n & 1))
        rat_neg(&out->r);
    rat_free(&abs_a);
    rat_free(&e);
    return rc;
}

static int power(Value *out, const Value *a, const Value *b, Ctx *c)
{
    int exact = a->exact && b->exact;
    int64_t n;

    if (!is_real(a) || !is_real(b))
        return complex_power(out, a, b, c);
    if (rat_is_int(&b->r)) {
        if (rat_to_i64(&b->r, &n))
            return fail(c, "exponent is too large");
        return int_power(out, a, n, exact, c);
    }

    /* Fractional exponent. */
    if (rat_is_zero(&a->r)) {
        if (rat_sign(&b->r) < 0)
            return fail(c, "division by zero (0 to a negative power)");
        rat_set_i64(&out->r, 0);
        out->exact = exact;
        return 0;
    }

    int negate = 0;
    if (rat_sign(&a->r) < 0) {
        /* Odd roots of negative numbers stay real, (-8)^(1/3) = -2, as on a
         * Casio; anything else is complex: (-4)^(1/2) = 2i. */
        if (!b->exact || !is_odd(&b->r.den))
            return complex_power(out, a, b, c);
        negate = is_odd(&b->r.num);
    }

    Rational base, root;
    rat_init(&base);
    rat_init(&root);
    rat_copy(&base, &a->r);
    if (rat_sign(&base) < 0)
        rat_neg(&base);

    /* a^(p/q) = (q-th root of a)^p, exact when the root is, like 8^(2/3) = 4. */
    int rc;
    int64_t p, q;
    if (exact && bi_to_i64(&b->r.den, &q) == 0 && bi_to_i64(&b->r.num, &p) == 0 &&
        exact_root(&root, &base, q)) {
        Value rv;
        val_init(&rv);
        rat_swap(&rv.r, &root);
        rc = int_power(out, &rv, p, 1, c);
        val_free(&rv);
    } else {
        rc = pow_approx(out, &base, &b->r, c);
    }
    if (rc == 0 && negate)
        rat_neg(&out->r);
    rat_free(&base);
    rat_free(&root);
    return rc;
}

static int fn_sqrt_root(Value *out, const Value *x, int64_t n, Ctx *c)
{
    if (n < 1)
        return fail(c, "root degree must be at least 1");
    int neg = rat_sign(&x->r) < 0;
    if (!is_real(x) || (neg && n % 2 == 0))
        return complex_root(out, x, n, c);   /* sqrt(-4) = 2i */
    if (n == 1 || rat_is_zero(&x->r)) {
        val_copy(out, x);
        return 0;
    }
    Rational ax;
    rat_init(&ax);
    rat_copy(&ax, &x->r);
    if (neg)
        rat_neg(&ax);
    if (x->exact && exact_root(&out->r, &ax, n)) {
        out->exact = 1;
    } else {
        root_approx(&out->r, &ax, n, c);
        out->exact = 0;
    }
    if (neg)
        rat_neg(&out->r);
    rat_free(&ax);
    return 0;
}

/* ---- Exponentials and logarithms ----------------------------------------- */

static int fn_exp(Value *out, const Value *x, Ctx *c)
{
    if (!is_real(x)) {
        double digits = rat_to_double(&x->r) * LOG10_E;
        if (fabs(digits) > MAX_RESULT_DIGITS)
            return too_big(c, fabs(digits));
        Complex z, r;
        cx_init(&z);
        cx_init(&r);
        get_cx(&z, x);
        cx_exp(&r, &z, c->prec);
        put_cx(out, &r, 0);
        approx(out, c);
        cx_free(&z);
        cx_free(&r);
        return 0;
    }
    if (x->exact && rat_is_zero(&x->r)) {
        set_int(out, 1);
        return 0;
    }
    double digits = rat_to_double(&x->r) * LOG10_E;
    if (fabs(digits) > MAX_RESULT_DIGITS)
        return too_big(c, fabs(digits));
    ap_exp(&out->r, &x->r, c->prec);
    out->exact = 0;
    return 0;
}

/* If x == b^n for a whole number n, like log(1000) = 3, store n and return 1. */
static int exact_log(int64_t *n, const Rational *x, const Rational *b)
{
    double est = rat_log10_abs(x) / rat_log10_abs(b);
    double r = nearbyint(est);
    if (fabs(est - r) > 1e-9 * fmax(1, fabs(est)) || fabs(r) > 1e7 ||
        fabs(r) * rat_size_log10(b) > MAX_RESULT_DIGITS)
        return 0;
    Rational t;
    rat_init(&t);
    rat_pow(&t, b, (int64_t)r);
    int ok = bi_cmp(&t.num, &x->num) == 0 && bi_cmp(&t.den, &x->den) == 0;
    rat_free(&t);
    if (ok)
        *n = (int64_t)r;
    return ok;
}

/* Logarithm of x in the given base; natural log when base is NULL. */
static int fn_log(Value *out, const Value *x, const Value *base, Ctx *c)
{
    if (is_real(x) && rat_is_zero(&x->r))
        return fail(c, "log of zero is undefined");
    if (base && is_real(base) && (rat_is_zero(&base->r) || is_one(&base->r)))
        return fail(c, "log base can't be 0 or 1");
    if (!is_real(x) || rat_sign(&x->r) < 0 || (base && (!is_real(base) || rat_sign(&base->r) < 0)))
        return complex_log(out, x, base, c);   /* ln(-1) = pi*i */
    if (x->exact && is_one(&x->r)) {
        set_int(out, 0);
        return 0;
    }
    int64_t n;
    if (base && x->exact && base->exact && exact_log(&n, &x->r, &base->r)) {
        set_int(out, n);
        return 0;
    }
    int p2 = c->prec + EXTRA;
    ap_ln(&out->r, &x->r, p2);
    if (base) {
        Rational lb;
        rat_init(&lb);
        ap_ln(&lb, &base->r, p2);
        rat_div(&out->r, &out->r, &lb);
        rat_free(&lb);
    }
    approx(out, c);
    return 0;
}

static int fn_log_int_base(Value *out, const Value *x, int64_t b, Ctx *c)
{
    Value base;
    val_init(&base);
    set_int(&base, b);
    int rc = fn_log(out, x, &base, c);
    val_free(&base);
    return rc;
}

/* ---- Trigonometry --------------------------------------------------------- */

enum { F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_SINH, F_COSH, F_TANH };

static void set_frac(Rational *r, int64_t n, int64_t d)
{
    rat_set_i64(r, n);
    bi_set_i64(&r->den, d);
}

/* sin of a whole number of degrees in [0, 360), when the answer is rational. */
static int sin_deg_exact(Rational *r, int64_t d)
{
    switch (d) {
    case 0:
    case 180:
        rat_set_i64(r, 0);
        return 1;
    case 90:
        rat_set_i64(r, 1);
        return 1;
    case 270:
        rat_set_i64(r, -1);
        return 1;
    case 30:
    case 150:
        set_frac(r, 1, 2);
        return 1;
    case 210:
    case 330:
        set_frac(r, -1, 2);
        return 1;
    }
    return 0;
}

static void to_radians(Rational *r, const Rational *x, Ctx *c)
{
    rat_copy(r, x);
    if (!c->degrees)
        return;
    Rational t;
    rat_init(&t);
    ap_pi(&t, c->prec + EXTRA);
    rat_mul(r, r, &t);
    rat_set_i64(&t, 180);
    rat_div(r, r, &t);
    rat_round_sig(r, r, c->prec + EXTRA);
    rat_free(&t);
}

static void from_radians(Rational *r, const Rational *x, Ctx *c)
{
    rat_copy(r, x);
    if (!c->degrees)
        return;
    Rational t;
    rat_init(&t);
    ap_pi(&t, c->prec + EXTRA);
    rat_div(r, r, &t);
    rat_set_i64(&t, 180);
    rat_mul(r, r, &t);
    rat_round_sig(r, r, c->prec + EXTRA);
    rat_free(&t);
}

static int fn_trig(Value *out, const Value *x, int f, Ctx *c)
{
    if (!is_real(x))
        return complex_func(out, x, f, c);
    Rational a, s, co;
    rat_init(&a);
    rat_init(&s);
    rat_init(&co);
    int rc = 0;

    if (c->degrees) {
        /* Reduce exactly to [0, 360) first; then common angles are exact. */
        rat_set_i64(&s, 360);
        rat_mod(&a, &x->r, &s);
        int64_t d;
        if (x->exact && rat_to_i64(&a, &d) == 0) {
            int ok = 0;
            if (f == F_SIN) {
                ok = sin_deg_exact(&out->r, d);
            } else if (f == F_COS) {
                ok = sin_deg_exact(&out->r, (d + 90) % 360);
            } else if (d % 180 == 90) {
                rc = fail(c, "tan is undefined at %lld degrees", (long long)d);
                goto done;
            } else if (d % 45 == 0) {
                rat_set_i64(&out->r, d % 180 == 0 ? 0 : d % 180 == 45 ? 1 : -1);
                ok = 1;
            }
            if (ok) {
                out->exact = 1;
                goto done;
            }
        }
        to_radians(&a, &a, c);
    } else {
        if (x->exact && rat_is_zero(&x->r)) {
            set_int(out, f == F_COS ? 1 : 0);
            goto done;
        }
        rat_copy(&a, &x->r);
    }

    /* The angle is inexact if it was typed that way or converted from degrees. */
    int noisy = !x->exact || c->degrees;
    double ref = rat_log10_abs(&a);
    out->exact = 0;
    if (f == F_SIN) {
        ap_sin(&out->r, &a, c->prec);
    } else if (f == F_COS) {
        ap_cos(&out->r, &a, c->prec);
    } else {
        ap_sin(&s, &a, c->prec + EXTRA);
        ap_cos(&co, &a, c->prec + EXTRA);
        if (noisy && !rat_is_zero(&co) && rat_log10_abs(&co) < ref - (c->prec - 3))
            rat_set_i64(&co, 0);
        if (rat_is_zero(&co)) {
            rc = fail(c, "tan is undefined here (cos is 0)");
            goto done;
        }
        rat_div(&out->r, &s, &co);
        approx(out, c);
    }
    if (noisy)
        clamp_noise(out, ref, c);

done:
    rat_free(&a);
    rat_free(&s);
    rat_free(&co);
    return rc;
}

static int fn_inverse_trig(Value *out, const Value *x, int f, Ctx *c)
{
    const Rational *v = &x->r;
    if (!is_real(x) || (f != F_ATAN && bi_cmp_abs(&v->num, &v->den) > 0))
        return complex_func(out, x, f, c);   /* asin(2) is complex */

    /* Exact answers for 0, +-1/2 and +-1 (in degrees; only 0 in radians). */
    int64_t n, d;
    if (x->exact && bi_to_i64(&v->num, &n) == 0 && bi_to_i64(&v->den, &d) == 0) {
        int hit = 1;
        int64_t deg = 0;
        if (f == F_ATAN) {
            if (n == 0)
                deg = 0;
            else if (d == 1 && (n == 1 || n == -1))
                deg = 45 * n;
            else
                hit = 0;
        } else {
            int64_t as = 0;
            if (n == 0)
                as = 0;
            else if (d == 2 && (n == 1 || n == -1))
                as = 30 * n;
            else if (d == 1)
                as = 90 * n;
            else
                hit = 0;
            deg = f == F_ASIN ? as : 90 - as;
        }
        if (hit && (c->degrees || deg == 0)) {
            set_int(out, deg);
            return 0;
        }
    }

    int p2 = c->prec + EXTRA;
    Rational t, u, one;
    rat_init(&t);
    rat_init(&u);
    rat_init(&one);
    rat_set_i64(&one, 1);
    int unit = bi_cmp_abs(&v->num, &v->den) == 0;   /* |v| == 1 */

    if (f == F_ATAN) {
        ap_atan(&t, v, p2);
    } else if (f == F_ASIN) {
        if (unit) {
            /* asin(+-1) = +-pi/2 */
            ap_pi(&t, p2);
            rat_set_i64(&u, 2 * rat_sign(v));
            rat_div(&t, &t, &u);
        } else {
            /* asin(v) = atan(v / sqrt(1 - v^2)) */
            rat_mul(&u, v, v);
            rat_sub(&u, &one, &u);
            ap_sqrt(&u, &u, p2);
            rat_div(&u, v, &u);
            ap_atan(&t, &u, p2);
        }
    } else {
        if (unit && rat_sign(v) < 0) {
            ap_pi(&t, p2);   /* acos(-1) = pi */
        } else {
            /* acos(v) = 2 atan(sqrt((1 - v) / (1 + v))), accurate near v = 1 */
            rat_add(&t, &one, v);
            rat_sub(&u, &one, v);
            rat_div(&u, &u, &t);
            ap_sqrt(&u, &u, p2);
            ap_atan(&t, &u, p2);
            rat_set_i64(&u, 2);
            rat_mul(&t, &t, &u);
        }
    }
    from_radians(&out->r, &t, c);
    approx(out, c);
    rat_free(&t);
    rat_free(&u);
    rat_free(&one);
    return 0;
}

static int fn_hyperbolic(Value *out, const Value *x, int f, Ctx *c)
{
    if (!is_real(x))
        return complex_func(out, x, f, c);
    if (x->exact && rat_is_zero(&x->r)) {
        set_int(out, f == F_COSH ? 1 : 0);
        return 0;
    }
    double digits = fabs(rat_to_double(&x->r)) * LOG10_E;   /* size of e^|x| */
    if (f == F_TANH && 2 * digits > c->prec + EXTRA) {
        /* tanh is +-1 to every digit we keep */
        rat_set_i64(&out->r, rat_sign(&x->r));
        out->exact = 0;
        return 0;
    }
    if (digits > MAX_RESULT_DIGITS)
        return too_big(c, digits);

    /* sinh and tanh of tiny x subtract nearly equal numbers; add digits. */
    double lx = rat_log10_abs(&x->r);
    int p2 = c->prec + EXTRA + (lx < 0 ? (int)ceil(-lx) : 0);
    Rational e, inv, t;
    rat_init(&e);
    rat_init(&inv);
    rat_init(&t);
    ap_exp(&e, &x->r, p2);
    rat_set_i64(&t, 1);
    rat_div(&inv, &t, &e);
    if (f == F_SINH || f == F_COSH) {
        if (f == F_SINH)
            rat_sub(&out->r, &e, &inv);
        else
            rat_add(&out->r, &e, &inv);
        rat_set_i64(&t, 2);
        rat_div(&out->r, &out->r, &t);
    } else {
        rat_sub(&t, &e, &inv);
        rat_add(&e, &e, &inv);
        rat_div(&out->r, &t, &e);
    }
    approx(out, c);
    rat_free(&e);
    rat_free(&inv);
    rat_free(&t);
    return 0;
}

/* ---- Complex numbers ------------------------------------------------------ */

/* If z has an exact square root, like 3+4i -> 2+i or -4 -> 2i, store it. */
static int exact_csqrt(Complex *r, const Complex *z)
{
    /* sqrt(a + bi) = sqrt((m + a)/2) + sign(b) sqrt((m - a)/2) i, m = |z| */
    Rational n, m, t, re, im, two;
    rat_init(&n);
    rat_init(&m);
    rat_init(&t);
    rat_init(&re);
    rat_init(&im);
    rat_init(&two);
    rat_set_i64(&two, 2);
    cx_norm(&n, z);
    int ok = exact_root(&m, &n, 2);
    if (ok) {
        rat_add(&t, &m, &z->re);
        rat_div(&t, &t, &two);
        ok = exact_root(&re, &t, 2);
    }
    if (ok) {
        rat_sub(&t, &m, &z->re);
        rat_div(&t, &t, &two);
        ok = exact_root(&im, &t, 2);
    }
    if (ok) {
        if (rat_sign(&z->im) < 0)
            rat_neg(&im);
        rat_swap(&r->re, &re);
        rat_swap(&r->im, &im);
    }
    rat_free(&n);
    rat_free(&m);
    rat_free(&t);
    rat_free(&re);
    rat_free(&im);
    rat_free(&two);
    return ok;
}

/* z * k for a real k */
static void cx_scale(Complex *z, const Rational *k)
{
    rat_mul(&z->re, &z->re, k);
    rat_mul(&z->im, &z->im, k);
}

static int complex_root(Value *out, const Value *x, int64_t n, Ctx *c)
{
    Complex z, r;
    cx_init(&z);
    cx_init(&r);
    get_cx(&z, x);
    if (n == 2 && x->exact && exact_csqrt(&r, &z)) {
        put_cx(out, &r, 1);
    } else if (n == 2) {
        cx_sqrt(&r, &z, c->prec);
        put_cx(out, &r, 0);
        approx(out, c);
    } else {
        /* principal root: e^(ln(z) / n) */
        Rational k;
        rat_init(&k);
        rat_set_i64(&k, n);
        cx_ln(&r, &z, c->prec + EXTRA);
        rat_div(&r.re, &r.re, &k);
        rat_div(&r.im, &r.im, &k);
        cx_exp(&r, &r, c->prec);
        put_cx(out, &r, 0);
        approx(out, c);
        rat_free(&k);
    }
    cx_free(&z);
    cx_free(&r);
    return 0;
}

static int complex_power(Value *out, const Value *a, const Value *b, Ctx *c)
{
    int exact = a->exact && b->exact;
    int rc = 0;
    int64_t n, d;
    Complex z, w, r;
    cx_init(&z);
    cx_init(&w);
    cx_init(&r);
    get_cx(&z, a);
    get_cx(&w, b);

    if (is_real(b) && rat_is_int(&b->r)) {
        if (rat_to_i64(&b->r, &n)) {
            rc = fail(c, "exponent is too large");
            goto done;
        }
        if (cx_is_zero(&z)) {
            rc = n < 0 ? fail(c, "division by zero (0 to a negative power)") : 0;
            set_int(out, n == 0 ? 1 : 0);
            out->exact = exact;
            goto done;
        }
        double sr = rat_size_log10(&z.re), si = rat_size_log10(&z.im);
        double digits = fabs((double)n) * (sr > si ? sr : si);
        if (exact && digits > MAX_RESULT_DIGITS) {
            rc = too_big(c, digits);
            goto done;
        }
        if (exact || digits <= 20000) {
            cx_pow_int(&r, &z, n);   /* (1+i)^2 = 2i, exactly */
            put_cx(out, &r, exact);
            if (!exact)
                approx(out, c);
            goto done;
        }
        /* a huge power of an approximate number: use e^(n ln z) below */
    } else if (cx_is_zero(&z)) {
        if (is_real(b) && rat_sign(&b->r) > 0)
            set_int(out, 0);
        else
            rc = fail(c, "0 to this power is undefined");
        out->exact = exact;
        goto done;
    } else if (is_real(a) && rat_sign(&a->r) < 0 && is_real(b) && b->exact &&
               bi_to_i64(&b->r.den, &d) == 0 && d == 2) {
        /* (-x)^(p/2) = x^(p/2) * i^p, exact when sqrt(x) is: (-4)^(1/2) = 2i */
        Value pos, t;
        val_init(&pos);
        val_init(&t);
        rat_copy(&pos.r, &a->r);
        rat_neg(&pos.r);
        pos.exact = a->exact;
        rc = power(&t, &pos, b, c);
        if (rc == 0) {
            BigInt four, q;
            int64_t p4;
            bi_init(&four);
            bi_init(&q);
            bi_set_i64(&four, 4);
            bi_divmod(NULL, &q, &b->r.num, &four);
            bi_to_i64(&q, &p4);
            p4 = ((p4 % 4) + 4) % 4;   /* i^p cycles 1, i, -1, -i */
            set_int(out, 0);
            rat_swap(p4 % 2 ? &out->im : &out->r, &t.r);
            if (p4 >= 2) {
                rat_neg(&out->r);
                rat_neg(&out->im);
            }
            out->exact = t.exact;
            bi_free(&four);
            bi_free(&q);
        }
        val_free(&pos);
        val_free(&t);
        goto done;
    }

    {
        /* z^w = e^(w ln z). Re(w ln z) = Re(w) ln|z| - Im(w) arg z sizes the result. */
        double lz = rat_log10_abs(&z.re) > rat_log10_abs(&z.im) ? rat_log10_abs(&z.re)
                                                                 : rat_log10_abs(&z.im);
        double size = fabs(rat_to_double(&w.re)) * fabs(lz) + fabs(rat_to_double(&w.im)) * 1.4;
        if (size > MAX_RESULT_DIGITS) {
            rc = too_big(c, size);
            goto done;
        }
        double ln_size = size / LOG10_E;
        int p2 = c->prec + EXTRA + (ln_size > 1 ? (int)ceil(log10(ln_size)) : 0);
        cx_ln(&r, &z, p2);
        cx_mul(&r, &w, &r);
        cx_round(&r, p2);
        cx_exp(&r, &r, c->prec);
        put_cx(out, &r, 0);
        approx(out, c);
    }

done:
    cx_free(&z);
    cx_free(&w);
    cx_free(&r);
    return rc;
}

/* Logarithm in the given base (natural when NULL) of anything but 0. */
static int complex_log(Value *out, const Value *x, const Value *base, Ctx *c)
{
    int p2 = c->prec + EXTRA;
    Complex z, l, lb;
    cx_init(&z);
    cx_init(&l);
    cx_init(&lb);
    get_cx(&z, x);
    cx_ln(&l, &z, p2);
    if (base) {
        get_cx(&z, base);
        cx_ln(&lb, &z, p2);
        cx_div(&l, &l, &lb);
    }
    put_cx(out, &l, 0);
    approx(out, c);
    cx_free(&z);
    cx_free(&l);
    cx_free(&lb);
    return 0;
}

/* pi/180 for converting degrees, or its inverse. */
static void degree_factor(Rational *k, int to_radians, int prec)
{
    Rational t;
    rat_init(&t);
    ap_pi(k, prec);
    rat_set_i64(&t, 180);
    if (to_radians)
        rat_div(k, k, &t);
    else
        rat_div(k, &t, k);
    rat_free(&t);
}

/* sin cos tan, their inverses, and sinh cosh tanh of complex numbers (and of
 * real numbers outside asin/acos's -1..1). */
static int complex_func(Value *out, const Value *x, int f, Ctx *c)
{
    int p2 = c->prec + EXTRA, rc = 0;
    Complex z, r, s, t;
    Rational k;
    cx_init(&z);
    cx_init(&r);
    cx_init(&s);
    cx_init(&t);
    rat_init(&k);
    get_cx(&z, x);
    if (c->degrees && f <= F_TAN) {
        degree_factor(&k, 1, p2);
        cx_scale(&z, &k);
    }

    switch (f) {
    case F_SIN:
        cx_sin(&r, &z, p2);
        break;
    case F_COS:
        cx_cos(&r, &z, p2);
        break;
    case F_SINH:
        cx_sinh(&r, &z, p2);
        break;
    case F_COSH:
        cx_cosh(&r, &z, p2);
        break;
    case F_TAN:
    case F_TANH:
        if (f == F_TAN) {
            cx_sin(&s, &z, p2);
            cx_cos(&t, &z, p2);
        } else {
            cx_sinh(&s, &z, p2);
            cx_cosh(&t, &z, p2);
        }
        if (cx_div(&r, &s, &t))
            rc = fail(c, "%s is undefined here", f == F_TAN ? "tan" : "tanh");
        break;
    case F_ASIN:
    case F_ACOS:
        /* asin z = -i ln(iz + sqrt(1 - z^2)) */
        cx_mul(&s, &z, &z);
        rat_set_i64(&t.re, 1);
        rat_set_i64(&t.im, 0);
        cx_sub(&s, &t, &s);
        cx_sqrt(&s, &s, p2 + EXTRA);
        rat_set_i64(&t.re, 0);
        rat_set_i64(&t.im, 1);
        cx_mul(&t, &t, &z);
        cx_add(&s, &s, &t);
        cx_ln(&s, &s, p2);
        rat_set_i64(&t.re, 0);
        rat_set_i64(&t.im, -1);
        cx_mul(&r, &t, &s);
        if (f == F_ACOS) {
            /* acos z = pi/2 - asin z */
            Rational half_pi;
            rat_init(&half_pi);
            ap_pi(&half_pi, p2);
            rat_set_i64(&k, 2);
            rat_div(&half_pi, &half_pi, &k);
            rat_sub(&r.re, &half_pi, &r.re);
            rat_neg(&r.im);
            rat_free(&half_pi);
        }
        break;
    case F_ATAN:
        /* atan z = (i/2) ln((i + z) / (i - z)), undefined at +-i */
        rat_set_i64(&t.re, 0);
        rat_set_i64(&t.im, 1);
        cx_add(&s, &t, &z);
        cx_sub(&t, &t, &z);
        if (cx_is_zero(&s) || cx_is_zero(&t)) {
            rc = fail(c, "atan is undefined at i and -i");
            break;
        }
        cx_div(&s, &s, &t);
        cx_ln(&s, &s, p2);
        rat_set_i64(&t.re, 0);
        rat_set_i64(&t.im, 1);
        rat_set_i64(&k, 2);
        rat_div(&t.im, &t.im, &k);
        cx_mul(&r, &t, &s);
        break;
    }

    if (rc == 0) {
        if (c->degrees && f >= F_ASIN && f <= F_ATAN) {
            degree_factor(&k, 0, p2);
            cx_scale(&r, &k);
        }
        put_cx(out, &r, 0);
        approx(out, c);
    }
    cx_free(&z);
    cx_free(&r);
    cx_free(&s);
    cx_free(&t);
    rat_free(&k);
    return rc;
}

/* abs (the magnitude) and arg (the angle) of any number. */
static int fn_abs_arg(Value *out, const Value *x, int want_arg, Ctx *c)
{
    Complex z;
    cx_init(&z);
    get_cx(&z, x);
    if (!want_arg) {
        if (is_real(x)) {
            val_copy(out, x);
            if (rat_sign(&out->r) < 0)
                rat_neg(&out->r);
        } else {
            /* |3+4i| = 5 exactly; otherwise sqrt(a^2 + b^2) */
            Rational n;
            rat_init(&n);
            cx_norm(&n, &z);
            set_int(out, 0);
            if (x->exact && exact_root(&out->r, &n, 2)) {
                out->exact = 1;
            } else {
                ap_sqrt(&out->r, &n, c->prec);
                out->exact = 0;
            }
            rat_free(&n);
        }
        cx_free(&z);
        return 0;
    }

    if (cx_is_zero(&z)) {
        cx_free(&z);
        return fail(c, "arg of 0 is undefined");
    }
    /* Exact angles: positive reals are 0; in degrees, the axes are too. */
    int sr = rat_sign(&z.re), si = rat_sign(&z.im);
    int64_t deg = -1;
    if (si == 0 && sr > 0)
        deg = 0;
    else if (si == 0)
        deg = 180;
    else if (sr == 0)
        deg = si > 0 ? 90 : -90;
    else if (bi_cmp_abs(&z.re.num, &z.im.num) == 0 && bi_cmp(&z.re.den, &z.im.den) == 0)
        deg = sr > 0 ? (si > 0 ? 45 : -45) : (si > 0 ? 135 : -135);   /* the diagonals */
    if (x->exact && (deg == 0 || (c->degrees && deg != -1))) {
        set_int(out, deg);
    } else {
        set_int(out, 0);
        cx_arg(&out->r, &z, c->prec + EXTRA);
        from_radians(&out->r, &out->r, c);
        approx(out, c);
    }
    cx_free(&z);
    return 0;
}

/* +, -, *, /, ^ when either side is complex */
static int complex_binary(Value *out, char op, const Value *a, const Value *b, Ctx *c)
{
    int exact = a->exact && b->exact, rc = 0;
    Complex x, y, r;
    cx_init(&x);
    cx_init(&y);
    cx_init(&r);
    get_cx(&x, a);
    get_cx(&y, b);
    switch (op) {
    case '+':
        cx_add(&r, &x, &y);
        break;
    case '-':
        cx_sub(&r, &x, &y);
        break;
    case '*':
        cx_mul(&r, &x, &y);
        break;
    case '/':
        if (cx_div(&r, &x, &y))
            rc = fail(c, "division by zero");
        break;
    case '%':
        rc = fail(c, "mod needs real numbers");
        break;
    case '^':
        rc = complex_power(out, a, b, c);
        goto done;
    }
    if (rc == 0) {
        if (!exact && (op == '+' || op == '-')) {
            /* Parts that cancel down to rounding noise are 0: i*i + 1 */
            double ref = -HUGE_VAL;
            const Rational *parts[] = {&x.re, &x.im, &y.re, &y.im};
            for (int i = 0; i < 4; i++)
                if (rat_log10_abs(parts[i]) > ref)
                    ref = rat_log10_abs(parts[i]);
            if (rat_log10_abs(&r.re) < ref - (c->prec - 3))
                rat_set_i64(&r.re, 0);
            if (rat_log10_abs(&r.im) < ref - (c->prec - 3))
                rat_set_i64(&r.im, 0);
        }
        put_cx(out, &r, exact);
        if (!exact)
            approx(out, c);
    }
done:
    cx_free(&x);
    cx_free(&y);
    cx_free(&r);
    return rc;
}

/* ---- Whole-number functions ---------------------------------------------- */

static int factorial(Value *out, const Value *v, Ctx *c)
{
    int64_t n;
    if (need_int(v, "factorial argument", &n, c))
        return -1;
    if (n < 0)
        return fail(c, "factorial of a negative number is undefined");
    if (n > MAX_FACTORIAL)
        return fail(c, "%lld! is too large (limit is %d!)", (long long)n, MAX_FACTORIAL);
    BigInt acc;
    bi_init(&acc);
    bi_set_i64(&acc, 1);
    for (int64_t i = 2; i <= n; i++)
        bi_mul_small(&acc, &acc, (uint32_t)i);
    rat_set_bigint(&out->r, &acc);
    out->exact = v->exact;
    bi_free(&acc);
    return 0;
}

/* choose(n, k): ways to pick k items from n, = n! / (k! (n-k)!). */
static int choose(Value *out, const Value *nv, const Value *kv, Ctx *c)
{
    int64_t n, k;
    if (need_int(nv, "n", &n, c) || need_int(kv, "k", &k, c))
        return -1;
    out->exact = nv->exact && kv->exact;
    if (k < 0 || (n >= 0 && k > n)) {
        rat_set_i64(&out->r, 0);
        return 0;
    }
    if (n >= 0 && k > n - k)
        k = n - k;   /* C(n, k) = C(n, n-k); fewer steps */
    if (k > MAX_TERMS)
        return fail(c, "k is too large (limit is %d)", MAX_TERMS);
    if (n < INT64_MIN + MAX_TERMS)
        return fail(c, "n is too large");

    /* Multiply in (n-k+i)/i one step at a time; each partial product is
     * itself a binomial coefficient, so the division is always exact. */
    BigInt acc, f;
    bi_init(&acc);
    bi_init(&f);
    bi_set_i64(&acc, 1);
    for (int64_t i = 1; i <= k; i++) {
        bi_set_i64(&f, n - k + i);
        bi_mul(&acc, &acc, &f);
        bi_set_i64(&f, i);
        bi_divmod(&acc, NULL, &acc, &f);
    }
    rat_set_bigint(&out->r, &acc);
    bi_free(&acc);
    bi_free(&f);
    return check_size(out, c);
}

/* perm(n, k): ordered arrangements of k items from n, = n! / (n-k)!. */
static int perm(Value *out, const Value *nv, const Value *kv, Ctx *c)
{
    int64_t n, k;
    if (need_int(nv, "n", &n, c) || need_int(kv, "k", &k, c))
        return -1;
    if (n < 0 || k < 0)
        return fail(c, "perm needs n >= 0 and k >= 0");
    out->exact = nv->exact && kv->exact;
    if (k > n) {
        rat_set_i64(&out->r, 0);
        return 0;
    }
    if (k > MAX_FACTORIAL)
        return fail(c, "k is too large (limit is %d)", MAX_FACTORIAL);
    BigInt acc, f;
    bi_init(&acc);
    bi_init(&f);
    bi_set_i64(&acc, 1);
    for (int64_t i = n - k + 1; i <= n; i++) {
        bi_set_i64(&f, i);
        bi_mul(&acc, &acc, &f);
    }
    rat_set_bigint(&out->r, &acc);
    bi_free(&acc);
    bi_free(&f);
    return check_size(out, c);
}

static int gcd_lcm(Value *out, const Value *a, const Value *b, int lcm, Ctx *c)
{
    if (!rat_is_int(&a->r) || !rat_is_int(&b->r))
        return fail(c, "%s needs whole numbers", lcm ? "lcm" : "gcd");
    BigInt g;
    bi_init(&g);
    bi_gcd(&g, &a->r.num, &b->r.num);
    if (!lcm) {
        rat_set_bigint(&out->r, &g);
    } else if (bi_is_zero(&g)) {
        rat_set_i64(&out->r, 0);   /* lcm(0, 0) = 0 */
    } else {
        /* lcm(a, b) = |a / gcd * b| */
        BigInt l;
        bi_init(&l);
        bi_divmod(&l, NULL, &a->r.num, &g);
        bi_mul(&l, &l, &b->r.num);
        bi_abs(&l);
        rat_set_bigint(&out->r, &l);
        bi_free(&l);
    }
    out->exact = a->exact && b->exact;
    bi_free(&g);
    return 0;
}

static void fn_round(Value *out, const Value *x, int mode)
{
    /* mode: -1 floor, +1 ceil, 0 round half away from zero */
    if (mode < 0) {
        rat_floor(&out->r, &x->r);
    } else if (mode > 0) {
        rat_ceil(&out->r, &x->r);
    } else {
        Rational half;
        rat_init(&half);
        set_frac(&half, 1, 2);
        int neg = rat_sign(&x->r) < 0;
        rat_copy(&out->r, &x->r);
        if (neg)
            rat_neg(&out->r);
        rat_add(&out->r, &out->r, &half);
        rat_floor(&out->r, &out->r);
        if (neg)
            rat_neg(&out->r);
        rat_free(&half);
    }
    out->exact = 1;
}

/* ---- Dispatch ------------------------------------------------------------ */

static int binary(Value *out, char op, const Value *a, const Value *b, Ctx *c)
{
    if (!is_real(a) || !is_real(b))
        return complex_binary(out, op, a, b, c) || check_size(out, c);
    int exact = a->exact && b->exact;
    int a_zero = a->exact && rat_is_zero(&a->r), b_zero = b->exact && rat_is_zero(&b->r);

    switch (op) {
    case '+':
    case '-': {
        double la = rat_log10_abs(&a->r), lb = rat_log10_abs(&b->r);
        if (op == '+')
            rat_add(&out->r, &a->r, &b->r);
        else
            rat_sub(&out->r, &a->r, &b->r);
        out->exact = exact;
        if (!exact) {
            approx(out, c);
            clamp_noise(out, la > lb ? la : lb, c);
        }
        break;
    }
    case '*':
        rat_mul(&out->r, &a->r, &b->r);
        out->exact = exact || a_zero || b_zero;   /* exactly 0 times anything is 0 */
        if (!out->exact)
            approx(out, c);
        break;
    case '/':
        if (rat_div(&out->r, &a->r, &b->r))
            return fail(c, "division by zero");
        out->exact = exact || a_zero;
        if (!out->exact)
            approx(out, c);
        break;
    case '%':
        if (rat_mod(&out->r, &a->r, &b->r))
            return fail(c, "modulo by zero");
        out->exact = exact;
        if (!exact)
            approx(out, c);
        break;
    case '^':
        if (power(out, a, b, c))
            return -1;
        break;
    default:
        return fail(c, "unknown operator '%c'", op);
    }
    return check_size(out, c);
}

/* sum(k=a, b, body) or prod(k=a, b, body). */
static int big_op(const Node *n, const Binding *env, Value *out, Ctx *c)
{
    int is_sum = n->kind == NODE_SUM;
    const char *opname = is_sum ? "sum" : "prod";
    Value lo, hi, idx, term, acc;
    int64_t a, b;
    int rc = -1;

    val_init(&lo);
    val_init(&hi);
    val_init(&idx);
    val_init(&term);
    val_init(&acc);

    if (eval_node(n->args[0], env, &lo, c) || eval_node(n->args[1], env, &hi, c))
        goto done;
    if (need_int(&lo, "lower bound", &a, c) || need_int(&hi, "upper bound", &b, c))
        goto done;

    /* An empty range gives the identity: 0 for a sum, 1 for a product. */
    uint64_t count = b >= a ? (uint64_t)b - (uint64_t)a + 1 : 0;
    if (count > MAX_TERMS) {
        fail(c, "%s has too many terms (limit is %d)", opname, MAX_TERMS);
        goto done;
    }

    set_int(&acc, is_sum ? 0 : 1);
    for (uint64_t i = 0; i < count; i++) {
        set_int(&idx, a + (int64_t)i);
        Binding bind = {n->name, &idx, env};
        if (eval_node(n->args[2], &bind, &term, c))
            goto done;
        if (is_real(&acc) && is_real(&term)) {
            if (is_sum)
                rat_add(&acc.r, &acc.r, &term.r);
            else
                rat_mul(&acc.r, &acc.r, &term.r);
        } else {
            Complex x, y;
            cx_init(&x);
            cx_init(&y);
            get_cx(&x, &acc);
            get_cx(&y, &term);
            if (is_sum)
                cx_add(&x, &x, &y);
            else
                cx_mul(&x, &x, &y);
            put_cx(&acc, &x, acc.exact);
            cx_free(&x);
            cx_free(&y);
        }
        acc.exact = acc.exact && term.exact;
        if (!acc.exact)
            approx(&acc, c);
        if (check_size(&acc, c))
            goto done;
    }
    val_swap(out, &acc);
    rc = 0;

done:
    val_free(&lo);
    val_free(&hi);
    val_free(&idx);
    val_free(&term);
    val_free(&acc);
    return rc;
}

static int call(const Node *n, Value *args, Value *out, Ctx *c)
{
    const char *name = n->name;
    size_t argc = n->nargs;
    int64_t k;
#define IS(s) (strcmp(name, (s)) == 0)
#define ARITY(k)                                                                      \
    do {                                                                              \
        if (argc != (k))                                                              \
            return fail(c, "%s takes %d argument%s", name, (k), (k) == 1 ? "" : "s"); \
    } while (0)

    /* Roots and powers */
    if (IS("sqrt")) {
        ARITY(1);
        return fn_sqrt_root(out, &args[0], 2, c);
    }
    if (IS("cbrt")) {
        ARITY(1);
        return fn_sqrt_root(out, &args[0], 3, c);
    }
    if (IS("root")) {
        ARITY(2);
        if (need_int(&args[1], "root degree", &k, c))
            return -1;
        return fn_sqrt_root(out, &args[0], k, c);
    }
    if (IS("exp")) {
        ARITY(1);
        return fn_exp(out, &args[0], c);
    }

    /* Logarithms */
    if (IS("ln")) {
        ARITY(1);
        return fn_log(out, &args[0], NULL, c);
    }
    if (IS("log")) {
        if (argc == 2)
            return fn_log(out, &args[0], &args[1], c);
        ARITY(1);
        return fn_log_int_base(out, &args[0], 10, c);
    }
    if (IS("log10")) {
        ARITY(1);
        return fn_log_int_base(out, &args[0], 10, c);
    }
    if (IS("log2")) {
        ARITY(1);
        return fn_log_int_base(out, &args[0], 2, c);
    }

    /* Trigonometry */
    static const struct {
        const char *name;
        int f;
    } trig[] = {
        {"sin", F_SIN},   {"cos", F_COS},   {"tan", F_TAN},   {"asin", F_ASIN}, {"acos", F_ACOS},
        {"atan", F_ATAN}, {"sinh", F_SINH}, {"cosh", F_COSH}, {"tanh", F_TANH},
    };
    for (size_t i = 0; i < sizeof trig / sizeof *trig; i++) {
        if (IS(trig[i].name)) {
            int f = trig[i].f;
            ARITY(1);
            if (f <= F_TAN)
                return fn_trig(out, &args[0], f, c);
            if (f <= F_ATAN)
                return fn_inverse_trig(out, &args[0], f, c);
            return fn_hyperbolic(out, &args[0], f, c);
        }
    }

    /* Rounding */
    if (IS("abs") || IS("arg")) {
        ARITY(1);
        return fn_abs_arg(out, &args[0], IS("arg"), c);
    }

    /* Complex numbers */
    if (IS("re") || IS("im") || IS("conj")) {
        ARITY(1);
        val_copy(out, &args[0]);
        if (IS("re"))
            rat_set_i64(&out->im, 0);
        else if (IS("im"))
            rat_swap(&out->r, &out->im), rat_set_i64(&out->im, 0);
        else
            rat_neg(&out->im);
        return 0;
    }
    for (size_t i = 0; i < argc; i++)
        if (!is_real(&args[i]) && (IS("floor") || IS("ceil") || IS("round") || IS("gcd") || IS("lcm")))
            return fail(c, "%s needs real numbers", name);

    if (IS("floor") || IS("ceil") || IS("round")) {
        ARITY(1);
        fn_round(out, &args[0], IS("floor") ? -1 : IS("ceil") ? 1 : 0);
        return 0;
    }

    /* Whole numbers */
    if (IS("gcd") || IS("lcm")) {
        ARITY(2);
        return gcd_lcm(out, &args[0], &args[1], IS("lcm"), c);
    }
    if (IS("choose") || IS("nCr") || IS("binomial")) {
        ARITY(2);
        return choose(out, &args[0], &args[1], c);
    }
    if (IS("perm") || IS("nPr")) {
        ARITY(2);
        return perm(out, &args[0], &args[1], c);
    }
    return fail(c, "unknown function '%s'", name);
#undef IS
#undef ARITY
}

static int constant(const char *name, Value *out, Ctx *c)
{
    rat_set_i64(&out->im, 0);
    if (strcmp(name, "i") == 0) {   /* the imaginary unit */
        set_int(out, 0);
        rat_set_i64(&out->im, 1);
        return 1;
    }
    if (strcmp(name, "pi") == 0 || strcmp(name, "\xCF\x80") == 0) {   /* π */
        ap_pi(&out->r, c->prec);
        out->exact = 0;
        return 1;
    }
    if (strcmp(name, "e") == 0) {
        Rational one;
        rat_init(&one);
        rat_set_i64(&one, 1);
        ap_exp(&out->r, &one, c->prec);
        out->exact = 0;
        rat_free(&one);
        return 1;
    }
    return 0;
}

static int lookup(const char *name, const Binding *env, Value *out, Ctx *c)
{
    for (const Binding *b = env; b; b = b->next) {
        if (strcmp(b->name, name) == 0) {
            val_copy(out, b->value);
            return 1;
        }
    }
    return constant(name, out, c);
}

/*
 * An unknown name made of single-letter variables and constants multiplies
 * them, as typed on a calculator: "ki" in a sum is k*i.
 */
static int split_name(const char *name, const Binding *env, Value *out, Ctx *c)
{
    size_t len = strlen(name);
    int ok = len > 1;
    for (size_t i = 0; i < len && ok; i++)
        ok = isalpha((unsigned char)name[i]);
    Value acc, v;
    val_init(&acc);
    val_init(&v);
    set_int(&acc, 1);
    for (size_t i = 0; i < len && ok; i++) {
        char letter[2] = {name[i], '\0'};
        ok = lookup(letter, env, &v, c) && binary(&acc, '*', &acc, &v, c) == 0;
    }
    if (ok)
        val_swap(out, &acc);
    val_free(&acc);
    val_free(&v);
    return ok ? 0 : fail(c, "unknown variable '%s'", name);
}

static int eval_node(const Node *n, const Binding *env, Value *out, Ctx *c)
{
    /* Real results only set out->r; start with no imaginary part. */
    rat_set_i64(&out->im, 0);
    switch (n->kind) {
    case NODE_NUM:
        rat_copy(&out->r, &n->value);
        out->exact = 1;
        return 0;

    case NODE_VAR:
        for (const Binding *b = env; b; b = b->next) {
            if (strcmp(b->name, n->name) == 0) {
                val_copy(out, b->value);
                return 0;
            }
        }
        if (constant(n->name, out, c))
            return 0;
        return split_name(n->name, env, out, c);

    case NODE_NEG:
        if (eval_node(n->args[0], env, out, c))
            return -1;
        rat_neg(&out->r);
        rat_neg(&out->im);
        return 0;

    case NODE_FACT: {
        Value v;
        val_init(&v);
        int rc = eval_node(n->args[0], env, &v, c) || factorial(out, &v, c) ? -1 : 0;
        val_free(&v);
        return rc;
    }

    case NODE_BINOP: {
        Value a, b;
        val_init(&a);
        val_init(&b);
        int rc = eval_node(n->args[0], env, &a, c) || eval_node(n->args[1], env, &b, c) ||
                         binary(out, n->op, &a, &b, c)
                     ? -1
                     : 0;
        val_free(&a);
        val_free(&b);
        return rc;
    }

    case NODE_CALL: {
        Value args[8];
        size_t argc = n->nargs;
        if (argc > sizeof args / sizeof *args)
            return fail(c, "too many arguments to %s", n->name);
        for (size_t i = 0; i < argc; i++)
            val_init(&args[i]);
        int rc = 0;
        for (size_t i = 0; i < argc && rc == 0; i++)
            rc = eval_node(n->args[i], env, &args[i], c);
        if (rc == 0)
            rc = call(n, args, out, c);
        for (size_t i = 0; i < argc; i++)
            val_free(&args[i]);
        return rc;
    }

    case NODE_SUM:
    case NODE_PROD:
        return big_op(n, env, out, c);
    }
    return fail(c, "internal error: unknown node");
}

int eval(const Node *n, const Binding *env, const EvalOptions *opt, Value *out, char *err,
         size_t errlen)
{
    Ctx c = {err, errlen, opt->prec, opt->degrees};
    return eval_node(n, env, out, &c);
}

/* One part of a complex number, without the "≈ " or "  (= 1/3)" decorations. */
static char *part_str(const Rational *r, int digits, int sci, int exact)
{
    char *s = rat_to_str(r, digits, sci, exact);
    if (!s)
        exit(1);
    char *note = strstr(s, "  (=");
    if (note)
        *note = '\0';
    if (strncmp(s, "\xE2\x89\x88 ", 4) == 0)
        memmove(s, s + 4, strlen(s + 4) + 1);
    return s;
}

char *val_to_str(const Value *v, int digits, int sci)
{
    if (is_real(v))
        return rat_to_str(&v->r, digits, sci, v->exact);

    /* "≈ 3 + 4i", "-2.5i", "1 - i" */
    Rational im;
    rat_init(&im);
    rat_copy(&im, &v->im);
    int neg = rat_sign(&im) < 0;
    if (neg)
        rat_neg(&im);
    char *re = rat_is_zero(&v->r) ? NULL : part_str(&v->r, digits, sci, v->exact);
    char *ims = part_str(&im, digits, sci, v->exact);
    if (strcmp(ims, "1") == 0) {   /* "i", not "1i" */
        free(ims);
        ims = NULL;
    }
    rat_free(&im);

    size_t len = (re ? strlen(re) : 0) + (ims ? strlen(ims) : 0) + 16;
    char *out = malloc(len);
    if (!out)
        exit(1);
    snprintf(out, len, "%s%s%s%s%si", v->exact ? "" : "\xE2\x89\x88 ", re ? re : "",
             re ? (neg ? " - " : " + ") : (neg ? "-" : ""), ims ? ims : "",
             ims && sci && strchr(ims, 'e') ? " " : "");
    free(re);
    free(ims);
    return out;
}
