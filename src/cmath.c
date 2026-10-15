#include "cmath.h"

#include <math.h>

#include "approx.h"

#define GUARD 5   /* extra digits for intermediate steps */

void cx_init(Complex *z)
{
    rat_init(&z->re);
    rat_init(&z->im);
}

void cx_free(Complex *z)
{
    rat_free(&z->re);
    rat_free(&z->im);
}

void cx_copy(Complex *dst, const Complex *src)
{
    rat_copy(&dst->re, &src->re);
    rat_copy(&dst->im, &src->im);
}

void cx_swap(Complex *a, Complex *b)
{
    rat_swap(&a->re, &b->re);
    rat_swap(&a->im, &b->im);
}

int cx_is_zero(const Complex *z)
{
    return rat_is_zero(&z->re) && rat_is_zero(&z->im);
}

void cx_add(Complex *r, const Complex *a, const Complex *b)
{
    rat_add(&r->re, &a->re, &b->re);
    rat_add(&r->im, &a->im, &b->im);
}

void cx_sub(Complex *r, const Complex *a, const Complex *b)
{
    rat_sub(&r->re, &a->re, &b->re);
    rat_sub(&r->im, &a->im, &b->im);
}

void cx_mul(Complex *r, const Complex *a, const Complex *b)
{
    /* (a + bi)(c + di) = (ac - bd) + (ad + bc)i */
    Complex t;
    Rational x;
    cx_init(&t);
    rat_init(&x);
    rat_mul(&t.re, &a->re, &b->re);
    rat_mul(&x, &a->im, &b->im);
    rat_sub(&t.re, &t.re, &x);
    rat_mul(&t.im, &a->re, &b->im);
    rat_mul(&x, &a->im, &b->re);
    rat_add(&t.im, &t.im, &x);
    cx_swap(r, &t);
    cx_free(&t);
    rat_free(&x);
}

void cx_norm(Rational *r, const Complex *z)
{
    Rational x;
    rat_init(&x);
    rat_mul(&x, &z->im, &z->im);
    rat_mul(r, &z->re, &z->re);
    rat_add(r, r, &x);
    rat_free(&x);
}

int cx_div(Complex *r, const Complex *a, const Complex *b)
{
    /* a / b = a * conj(b) / |b|^2 */
    if (cx_is_zero(b))
        return -1;
    Complex conj;
    Rational n;
    cx_init(&conj);
    rat_init(&n);
    cx_norm(&n, b);
    rat_copy(&conj.re, &b->re);
    rat_copy(&conj.im, &b->im);
    rat_neg(&conj.im);
    cx_mul(r, a, &conj);
    rat_div(&r->re, &r->re, &n);
    rat_div(&r->im, &r->im, &n);
    cx_free(&conj);
    rat_free(&n);
    return 0;
}

void cx_pow_int(Complex *r, const Complex *z, int64_t n)
{
    Complex result, b;
    cx_init(&result);
    cx_init(&b);
    rat_set_i64(&result.re, 1);
    cx_copy(&b, z);
    if (n < 0) {
        Complex one;
        cx_init(&one);
        rat_set_i64(&one.re, 1);
        cx_div(&b, &one, &b);
        cx_free(&one);
    }
    uint64_t e = n < 0 ? (uint64_t)0 - (uint64_t)n : (uint64_t)n;
    while (e) {
        if (e & 1)
            cx_mul(&result, &result, &b);
        e >>= 1;
        if (e)
            cx_mul(&b, &b, &b);
    }
    cx_swap(r, &result);
    cx_free(&result);
    cx_free(&b);
}

void cx_round(Complex *z, int prec)
{
    double lr = rat_log10_abs(&z->re), li = rat_log10_abs(&z->im);
    double top = lr > li ? lr : li;
    rat_round_sig(&z->re, &z->re, prec);
    rat_round_sig(&z->im, &z->im, prec);
    if (!rat_is_zero(&z->re) && lr < top - (prec - 3))
        rat_set_i64(&z->re, 0);
    if (!rat_is_zero(&z->im) && li < top - (prec - 3))
        rat_set_i64(&z->im, 0);
}

/* r = pi / d, approximately */
static void pi_over(Rational *r, int64_t d, int prec)
{
    Rational t;
    rat_init(&t);
    ap_pi(r, prec);
    rat_set_i64(&t, d);
    rat_div(r, r, &t);
    rat_free(&t);
}

void cx_arg(Rational *r, const Complex *z, int prec)
{
    int sa = rat_sign(&z->re), sb = rat_sign(&z->im);
    if (sb == 0) {
        if (sa >= 0)
            rat_set_i64(r, 0);
        else
            ap_pi(r, prec);
        return;
    }
    if (sa == 0) {
        pi_over(r, sb > 0 ? 2 : -2, prec);
        return;
    }
    /* atan(b/a), moved into the right half of the plane when a < 0 */
    int p2 = prec + GUARD;
    Rational t, pi;
    rat_init(&t);
    rat_init(&pi);
    rat_div(&t, &z->im, &z->re);
    ap_atan(&t, &t, p2);
    if (sa < 0) {
        ap_pi(&pi, p2);
        if (sb > 0)
            rat_add(&t, &t, &pi);
        else
            rat_sub(&t, &t, &pi);
    }
    rat_round_sig(r, &t, prec);
    rat_free(&t);
    rat_free(&pi);
}

/* Like ap_exp, ap_sin, ... but leaving zero alone. */
static void real_exp(Rational *r, const Rational *x, int prec)
{
    if (rat_is_zero(x))
        rat_set_i64(r, 1);
    else
        ap_exp(r, x, prec);
}

void cx_exp(Complex *r, const Complex *z, int prec)
{
    /* e^(a + bi) = e^a (cos b + i sin b) */
    int p2 = prec + GUARD;
    Rational ea, c, s;
    rat_init(&ea);
    rat_init(&c);
    rat_init(&s);
    real_exp(&ea, &z->re, p2);
    if (rat_is_zero(&z->im)) {
        rat_set_i64(&c, 1);
    } else {
        ap_cos(&c, &z->im, p2);
        ap_sin(&s, &z->im, p2);
    }
    rat_mul(&r->re, &ea, &c);
    rat_mul(&r->im, &ea, &s);
    cx_round(r, prec);
    rat_free(&ea);
    rat_free(&c);
    rat_free(&s);
}

void cx_ln(Complex *r, const Complex *z, int prec)
{
    /* ln z = ln|z| + i arg z, with ln|z| = ln(|z|^2) / 2 */
    int p2 = prec + GUARD;
    Rational n, two;
    rat_init(&n);
    rat_init(&two);
    cx_norm(&n, z);
    Rational arg;
    rat_init(&arg);
    cx_arg(&arg, z, p2);
    ap_ln(&n, &n, p2);
    rat_set_i64(&two, 2);
    rat_div(&r->re, &n, &two);
    rat_swap(&r->im, &arg);
    cx_round(r, prec);
    rat_free(&n);
    rat_free(&two);
    rat_free(&arg);
}

void cx_sqrt(Complex *r, const Complex *z, int prec)
{
    /* With m = |z|: the larger of the two parts is sqrt((m + |a|) / 2), and
     * the other is b / (2 * that), which avoids subtracting close numbers. */
    if (cx_is_zero(z)) {
        rat_set_i64(&r->re, 0);
        rat_set_i64(&r->im, 0);
        return;
    }
    int p2 = prec + GUARD;
    Rational m, t, big, other;
    rat_init(&m);
    rat_init(&t);
    rat_init(&big);
    rat_init(&other);
    cx_norm(&m, z);
    ap_sqrt(&m, &m, p2);
    rat_copy(&t, &z->re);
    if (rat_sign(&t) < 0)
        rat_neg(&t);
    rat_add(&t, &m, &t);
    rat_set_i64(&big, 2);
    rat_div(&t, &t, &big);
    ap_sqrt(&big, &t, p2);
    rat_add(&t, &big, &big);
    rat_div(&other, &z->im, &t);
    if (rat_sign(&z->re) >= 0) {
        rat_swap(&r->re, &big);
        rat_swap(&r->im, &other);
    } else {
        /* sqrt of a number left of the axis: the imaginary part is the big one,
         * with the sign of b (+ for a negative real number) */
        if (rat_sign(&z->im) < 0) {
            rat_neg(&big);
            rat_neg(&other);
        }
        rat_swap(&r->re, &other);
        rat_swap(&r->im, &big);
    }
    cx_round(r, prec);
    rat_free(&m);
    rat_free(&t);
    rat_free(&big);
    rat_free(&other);
}

/* sinh and cosh of a real number, accurate for tiny x too. */
static void real_sinh_cosh(Rational *sh, Rational *ch, const Rational *x, int prec)
{
    if (rat_is_zero(x)) {
        rat_set_i64(sh, 0);
        rat_set_i64(ch, 1);
        return;
    }
    double lx = rat_log10_abs(x);
    int p2 = prec + GUARD + (lx < 0 ? (int)ceil(-lx) : 0);
    Rational e, inv, two;
    rat_init(&e);
    rat_init(&inv);
    rat_init(&two);
    ap_exp(&e, x, p2);
    rat_set_i64(&two, 1);
    rat_div(&inv, &two, &e);
    rat_set_i64(&two, 2);
    rat_sub(sh, &e, &inv);
    rat_div(sh, sh, &two);
    rat_add(ch, &e, &inv);
    rat_div(ch, ch, &two);
    rat_round_sig(sh, sh, prec);
    rat_round_sig(ch, ch, prec);
    rat_free(&e);
    rat_free(&inv);
    rat_free(&two);
}

static void real_sin_cos(Rational *s, Rational *c, const Rational *x, int prec)
{
    if (rat_is_zero(x)) {
        rat_set_i64(s, 0);
        rat_set_i64(c, 1);
        return;
    }
    ap_sin(s, x, prec);
    ap_cos(c, x, prec);
}

void cx_sin(Complex *r, const Complex *z, int prec)
{
    /* sin(a + bi) = sin a cosh b + i cos a sinh b */
    int p2 = prec + GUARD;
    Rational s, c, sh, ch;
    rat_init(&s);
    rat_init(&c);
    rat_init(&sh);
    rat_init(&ch);
    real_sin_cos(&s, &c, &z->re, p2);
    real_sinh_cosh(&sh, &ch, &z->im, p2);
    rat_mul(&r->re, &s, &ch);
    rat_mul(&r->im, &c, &sh);
    cx_round(r, prec);
    rat_free(&s);
    rat_free(&c);
    rat_free(&sh);
    rat_free(&ch);
}

void cx_cos(Complex *r, const Complex *z, int prec)
{
    /* cos(a + bi) = cos a cosh b - i sin a sinh b */
    int p2 = prec + GUARD;
    Rational s, c, sh, ch;
    rat_init(&s);
    rat_init(&c);
    rat_init(&sh);
    rat_init(&ch);
    real_sin_cos(&s, &c, &z->re, p2);
    real_sinh_cosh(&sh, &ch, &z->im, p2);
    rat_mul(&r->re, &c, &ch);
    rat_mul(&r->im, &s, &sh);
    rat_neg(&r->im);
    cx_round(r, prec);
    rat_free(&s);
    rat_free(&c);
    rat_free(&sh);
    rat_free(&ch);
}

void cx_sinh(Complex *r, const Complex *z, int prec)
{
    /* sinh(a + bi) = sinh a cos b + i cosh a sin b */
    int p2 = prec + GUARD;
    Rational s, c, sh, ch;
    rat_init(&s);
    rat_init(&c);
    rat_init(&sh);
    rat_init(&ch);
    real_sinh_cosh(&sh, &ch, &z->re, p2);
    real_sin_cos(&s, &c, &z->im, p2);
    rat_mul(&r->re, &sh, &c);
    rat_mul(&r->im, &ch, &s);
    cx_round(r, prec);
    rat_free(&s);
    rat_free(&c);
    rat_free(&sh);
    rat_free(&ch);
}

void cx_cosh(Complex *r, const Complex *z, int prec)
{
    /* cosh(a + bi) = cosh a cos b + i sinh a sin b */
    int p2 = prec + GUARD;
    Rational s, c, sh, ch;
    rat_init(&s);
    rat_init(&c);
    rat_init(&sh);
    rat_init(&ch);
    real_sinh_cosh(&sh, &ch, &z->re, p2);
    real_sin_cos(&s, &c, &z->im, p2);
    rat_mul(&r->re, &ch, &c);
    rat_mul(&r->im, &sh, &s);
    cx_round(r, prec);
    rat_free(&s);
    rat_free(&c);
    rat_free(&sh);
    rat_free(&ch);
}
