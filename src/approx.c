/*
 * Fixed-point arithmetic on BigInts: an integer v stands for v / 10^W, so
 * with W = 40, the number 1.5 is stored as 15 followed by 39 zeros. Adding
 * fixed-point numbers is plain integer addition; multiplying needs one
 * division by 10^W afterwards.
 *
 * Each function picks W a bit larger than the precision asked for, runs a
 * series in fixed point, then rounds the result back to a Rational.
 */
#include "approx.h"

#include <math.h>

#define GUARD 10                              /* extra digits for rounding errors */
#define LN10_D 2.302585092994045684

typedef struct {
    BigInt one;   /* 10^W, the fixed-point representation of 1 */
} Fx;

static void fx_init(Fx *f, int W)
{
    bi_init(&f->one);
    bi_pow10(&f->one, (size_t)W);
}

static void fx_free(Fx *f)
{
    bi_free(&f->one);
}

static void mul_int(BigInt *r, const BigInt *a, int64_t m)
{
    BigInt t;
    bi_init(&t);
    bi_set_i64(&t, m);
    bi_mul(r, a, &t);
    bi_free(&t);
}

static void div_int(BigInt *r, const BigInt *a, int64_t d)
{
    BigInt t;
    bi_init(&t);
    bi_set_i64(&t, d);
    bi_divmod(r, NULL, a, &t);
    bi_free(&t);
}

static void fx_from_rat(const Fx *f, BigInt *r, const Rational *x)
{
    bi_mul(r, &x->num, &f->one);
    bi_divmod(r, NULL, r, &x->den);
}

static void fx_from_frac(const Fx *f, BigInt *r, int64_t n, int64_t d)
{
    mul_int(r, &f->one, n);
    div_int(r, r, d);
}

static void fx_to_rat(const Fx *f, Rational *r, const BigInt *v, int prec)
{
    rat_set_frac(r, v, &f->one);
    rat_round_sig(r, r, prec);
}

static void fx_mul(const Fx *f, BigInt *r, const BigInt *a, const BigInt *b)
{
    bi_mul(r, a, b);
    bi_divmod(r, NULL, r, &f->one);
}

static void fx_div(const Fx *f, BigInt *r, const BigInt *a, const BigInt *b)
{
    BigInt t;
    bi_init(&t);
    bi_mul(&t, a, &f->one);
    bi_divmod(r, NULL, &t, b);
    bi_free(&t);
}

static void fx_sqrt(const Fx *f, BigInt *r, const BigInt *a)
{
    /* sqrt(a / 10^W) * 10^W = sqrt(a * 10^W) */
    BigInt t;
    bi_init(&t);
    bi_mul(&t, a, &f->one);
    bi_root(r, &t, 2);
    bi_free(&t);
}

/*
 * z + z^3/3 + z^5/5 + ...  = atanh(z)
 * z - z^3/3 + z^5/5 - ...  = atan(z)   (when alternate is set)
 * Converges quickly when |z| is small.
 */
static void fx_atan_series(const Fx *f, BigInt *r, const BigInt *z, int alternate)
{
    BigInt z2, power, term, sum;
    bi_init(&z2);
    bi_init(&power);
    bi_init(&term);
    bi_init(&sum);
    fx_mul(f, &z2, z, z);
    bi_copy(&power, z);
    for (int64_t j = 1; !bi_is_zero(&power); j += 2) {
        div_int(&term, &power, j);
        if (alternate && (j / 2) % 2 == 1)
            bi_sub(&sum, &sum, &term);
        else
            bi_add(&sum, &sum, &term);
        fx_mul(f, &power, &power, &z2);
    }
    bi_swap(r, &sum);
    bi_free(&z2);
    bi_free(&power);
    bi_free(&term);
    bi_free(&sum);
}

/* ln 2 = 2 atanh(1/3) */
static void fx_ln2(const Fx *f, BigInt *r)
{
    BigInt z;
    bi_init(&z);
    fx_from_frac(f, &z, 1, 3);
    fx_atan_series(f, r, &z, 0);
    mul_int(r, r, 2);
    bi_free(&z);
}

/* ln 10 = ln 8 + ln(5/4) = 3 ln 2 + 2 atanh(1/9) */
static void fx_ln10(const Fx *f, BigInt *r)
{
    BigInt z, t;
    bi_init(&z);
    bi_init(&t);
    fx_ln2(f, &t);
    mul_int(&t, &t, 3);
    fx_from_frac(f, &z, 1, 9);
    fx_atan_series(f, r, &z, 0);
    mul_int(r, r, 2);
    bi_add(r, r, &t);
    bi_free(&z);
    bi_free(&t);
}

/* Machin's formula: pi = 16 atan(1/5) - 4 atan(1/239) */
static void fx_pi(const Fx *f, BigInt *r)
{
    BigInt z, t;
    bi_init(&z);
    bi_init(&t);
    fx_from_frac(f, &z, 1, 5);
    fx_atan_series(f, &t, &z, 1);
    mul_int(&t, &t, 16);
    fx_from_frac(f, &z, 1, 239);
    fx_atan_series(f, r, &z, 1);
    mul_int(r, r, 4);
    bi_sub(r, &t, r);
    bi_free(&z);
    bi_free(&t);
}

/* e^x = 1 + x + x^2/2! + x^3/3! + ...  (used for |x| <= ~1.2) */
static void fx_exp_series(const Fx *f, BigInt *r, const BigInt *x)
{
    BigInt term, sum;
    bi_init(&term);
    bi_init(&sum);
    bi_copy(&term, &f->one);
    for (int64_t n = 1; !bi_is_zero(&term); n++) {
        bi_add(&sum, &sum, &term);
        fx_mul(f, &term, &term, x);
        div_int(&term, &term, n);
    }
    bi_swap(r, &sum);
    bi_free(&term);
    bi_free(&sum);
}

/* sin x = x - x^3/3! + ...  or  cos x = 1 - x^2/2! + ...  (for |x| <= ~0.8) */
static void fx_sincos_series(const Fx *f, BigInt *r, const BigInt *x, int sine)
{
    BigInt x2, term, sum;
    bi_init(&x2);
    bi_init(&term);
    bi_init(&sum);
    fx_mul(f, &x2, x, x);
    bi_copy(&term, sine ? x : &f->one);
    for (int64_t n = sine ? 1 : 0; !bi_is_zero(&term); n += 2) {
        bi_add(&sum, &sum, &term);
        fx_mul(f, &term, &term, &x2);
        div_int(&term, &term, (n + 1) * (n + 2));
        bi_neg(&term);
    }
    bi_swap(r, &sum);
    bi_free(&x2);
    bi_free(&term);
    bi_free(&sum);
}

/* Extra digits so a fixed-point copy of a tiny |x| (like 1e-30) keeps its precision. */
static int small_extra(const Rational *x)
{
    double l = rat_log10_abs(x);
    return rat_is_zero(x) || l >= 0 ? 0 : (int)ceil(-l);
}

/* Extra digits for a large |x|, whose integer part uses up digits too. */
static int big_extra(const Rational *x)
{
    double l = rat_log10_abs(x);
    return rat_is_zero(x) || l <= 0 ? 0 : (int)ceil(l) + 1;
}

static int digits_of(int64_t n)
{
    int d = 1;
    while (n >= 10 || n <= -10) {
        n /= 10;
        d++;
    }
    return d;
}

void ap_pi(Rational *r, int prec)
{
    Fx f;
    BigInt p;
    fx_init(&f, prec + GUARD);
    bi_init(&p);
    fx_pi(&f, &p);
    fx_to_rat(&f, r, &p, prec);
    bi_free(&p);
    fx_free(&f);
}

void ap_sqrt(Rational *r, const Rational *x, int prec)
{
    /* sqrt(n/d) = sqrt(n*d) / d. Scale n*d by 10^(2s) first so that its
     * integer square root has at least prec + 5 digits. */
    BigInt nd, root, p;
    bi_init(&nd);
    bi_init(&root);
    bi_init(&p);
    bi_mul(&nd, &x->num, &x->den);
    long s = (long)prec + 6 - (long)bi_num_digits(&nd) / 2;
    if (s < 0)
        s = 0;
    bi_pow10(&p, (size_t)(2 * s));
    bi_mul(&nd, &nd, &p);
    bi_root(&root, &nd, 2);
    bi_pow10(&p, (size_t)s);
    bi_mul(&p, &p, &x->den);
    rat_set_frac(r, &root, &p);
    rat_round_sig(r, r, prec);
    bi_free(&nd);
    bi_free(&root);
    bi_free(&p);
}

void ap_exp(Rational *r, const Rational *x, int prec)
{
    /* e^x = 10^n * e^(x - n ln 10), with n picked so the leftover is at most
     * ~1.15 in size, where the series converges fast. Multiplying by 10^n is
     * exact, so huge and tiny results keep full precision. */
    int64_t n = llround(rat_to_double(x) / LN10_D);
    Fx f;
    BigInt X, L, E;
    fx_init(&f, prec + GUARD + big_extra(x));
    bi_init(&X);
    bi_init(&L);
    bi_init(&E);

    fx_from_rat(&f, &X, x);
    fx_ln10(&f, &L);
    mul_int(&L, &L, n);
    bi_sub(&X, &X, &L);
    fx_exp_series(&f, &E, &X);

    bi_pow10(&L, (size_t)(n >= 0 ? n : -n));
    if (n >= 0) {
        bi_mul(&E, &E, &L);
        rat_set_frac(r, &E, &f.one);
    } else {
        bi_mul(&L, &L, &f.one);
        rat_set_frac(r, &E, &L);
    }
    rat_round_sig(r, r, prec);

    bi_free(&X);
    bi_free(&L);
    bi_free(&E);
    fx_free(&f);
}

void ap_ln(Rational *r, const Rational *x, int prec)
{
    /* Write x = 10^k * 2^j * m with m close to 1, then
     *   ln x = k ln 10 + j ln 2 + 2 atanh((m - 1) / (m + 1)).
     * k and j are chosen by rounding, so for x near 1 they're both 0 and
     * nothing cancels. */
    int64_t k = llround(rat_log10_abs(x));
    Rational m, t, one;
    rat_init(&m);
    rat_init(&t);
    rat_init(&one);
    rat_set_i64(&one, 1);

    bi_pow10(&t.num, (size_t)(k >= 0 ? k : -k));
    bi_set_i64(&t.den, 1);
    if (k >= 0)
        rat_div(&m, x, &t);
    else
        rat_mul(&m, x, &t);
    int64_t j = llround(log2(rat_to_double(&m)));
    rat_set_i64(&t, 2);
    rat_pow(&t, &t, j);
    rat_div(&m, &m, &t);

    /* z = (m - 1) / (m + 1), computed exactly */
    rat_add(&t, &m, &one);
    rat_sub(&m, &m, &one);
    rat_div(&m, &m, &t);

    Fx f;
    BigInt Z, A, L;
    fx_init(&f, prec + GUARD + small_extra(&m) + digits_of(k));
    bi_init(&Z);
    bi_init(&A);
    bi_init(&L);
    fx_from_rat(&f, &Z, &m);
    fx_atan_series(&f, &A, &Z, 0);
    mul_int(&A, &A, 2);
    fx_ln10(&f, &L);
    mul_int(&L, &L, k);
    bi_add(&A, &A, &L);
    fx_ln2(&f, &L);
    mul_int(&L, &L, j);
    bi_add(&A, &A, &L);
    fx_to_rat(&f, r, &A, prec);

    rat_free(&m);
    rat_free(&t);
    rat_free(&one);
    bi_free(&Z);
    bi_free(&A);
    bi_free(&L);
    fx_free(&f);
}

/* sin(x) when want_cos is 0, cos(x) otherwise. */
static void sincos(Rational *r, const Rational *x, int prec, int want_cos)
{
    /* Reduce: x = n * (pi/2) + rem with |rem| <= pi/4, then use the
     * quadrant n to pick +/- sin or cos of rem. */
    Fx f;
    BigInt X, H, N, R, four;
    fx_init(&f, prec + GUARD + big_extra(x) + small_extra(x));
    bi_init(&X);
    bi_init(&H);
    bi_init(&N);
    bi_init(&R);
    bi_init(&four);

    fx_from_rat(&f, &X, x);
    fx_pi(&f, &H);
    div_int(&H, &H, 2);
    bi_divmod(&N, &R, &X, &H);
    /* Round N to nearest: if |remainder| > H/2, step one further. */
    bi_add(&R, &R, &R);
    if (bi_cmp_abs(&R, &H) > 0) {
        bi_set_i64(&four, X.sign);
        bi_add(&N, &N, &four);
    }
    bi_mul(&R, &N, &H);
    bi_sub(&R, &X, &R);

    int64_t q;
    bi_set_i64(&four, 4);
    bi_divmod(NULL, &four, &N, &four);
    bi_to_i64(&four, &q);
    q = ((q % 4) + 4 + (want_cos ? 1 : 0)) % 4;   /* cos x = sin(x + pi/2) */

    /* quadrant 0: sin, 1: cos, 2: -sin, 3: -cos */
    fx_sincos_series(&f, &X, &R, q % 2 == 0);
    if (q >= 2)
        bi_neg(&X);
    fx_to_rat(&f, r, &X, prec);

    bi_free(&X);
    bi_free(&H);
    bi_free(&N);
    bi_free(&R);
    bi_free(&four);
    fx_free(&f);
}

void ap_sin(Rational *r, const Rational *x, int prec)
{
    sincos(r, x, prec, 0);
}

void ap_cos(Rational *r, const Rational *x, int prec)
{
    sincos(r, x, prec, 1);
}

void ap_atan(Rational *r, const Rational *x, int prec)
{
    /* For |x| > 1 use atan(x) = pi/2 - atan(1/x). Then shrink the argument
     * twice with atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))) so the series
     * converges quickly. */
    Rational a;
    rat_init(&a);
    rat_copy(&a, x);
    int neg = rat_sign(&a) < 0;
    if (neg)
        rat_neg(&a);
    int invert = bi_cmp(&a.num, &a.den) > 0;
    if (invert)
        bi_swap(&a.num, &a.den);

    Fx f;
    BigInt A, T;
    fx_init(&f, prec + GUARD + small_extra(&a));
    bi_init(&A);
    bi_init(&T);
    fx_from_rat(&f, &A, &a);
    for (int i = 0; i < 2; i++) {
        fx_mul(&f, &T, &A, &A);
        bi_add(&T, &T, &f.one);
        fx_sqrt(&f, &T, &T);
        bi_add(&T, &T, &f.one);
        fx_div(&f, &A, &A, &T);
    }
    fx_atan_series(&f, &A, &A, 1);
    mul_int(&A, &A, 4);
    if (invert) {
        fx_pi(&f, &T);
        div_int(&T, &T, 2);
        bi_sub(&A, &T, &A);
    }
    if (neg)
        bi_neg(&A);
    fx_to_rat(&f, r, &A, prec);

    rat_free(&a);
    bi_free(&A);
    bi_free(&T);
    fx_free(&f);
}
