#include "rational.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DECIMAL_EXPONENT 100000

void rat_init(Rational *r)
{
    bi_init(&r->num);
    bi_init(&r->den);
    bi_set_i64(&r->den, 1);
}

void rat_free(Rational *r)
{
    bi_free(&r->num);
    bi_free(&r->den);
}

void rat_copy(Rational *dst, const Rational *src)
{
    bi_copy(&dst->num, &src->num);
    bi_copy(&dst->den, &src->den);
}

void rat_swap(Rational *a, Rational *b)
{
    bi_swap(&a->num, &b->num);
    bi_swap(&a->den, &b->den);
}

void rat_set_i64(Rational *r, int64_t v)
{
    bi_set_i64(&r->num, v);
    bi_set_i64(&r->den, 1);
}

void rat_set_bigint(Rational *r, const BigInt *v)
{
    bi_copy(&r->num, v);
    bi_set_i64(&r->den, 1);
}

/* Put r into lowest terms with a positive denominator. */
static void normalize(Rational *r)
{
    if (r->den.sign < 0) {
        bi_neg(&r->num);
        bi_neg(&r->den);
    }
    if (bi_is_one(&r->den))
        return;
    BigInt g;
    bi_init(&g);
    bi_gcd(&g, &r->num, &r->den);   /* gcd(0, den) = den, which turns 0/den into 0/1 */
    if (!bi_is_one(&g)) {
        bi_divmod(&r->num, NULL, &r->num, &g);
        bi_divmod(&r->den, NULL, &r->den, &g);
    }
    bi_free(&g);
}

int rat_set_decimal(Rational *r, const char *s, size_t n)
{
    char *digits = malloc(n + 1);
    size_t nd = 0, i = 0;
    long frac = 0, exp = 0;

    if (!digits)
        return -1;
    for (; i < n && isdigit((unsigned char)s[i]); i++)
        digits[nd++] = s[i];
    if (i < n && s[i] == '.') {
        for (i++; i < n && isdigit((unsigned char)s[i]); i++, frac++)
            digits[nd++] = s[i];
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        int neg = 0;
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-'))
            neg = s[i++] == '-';
        if (i == n || !isdigit((unsigned char)s[i])) {
            free(digits);
            return -1;
        }
        for (; i < n && isdigit((unsigned char)s[i]); i++) {
            exp = exp * 10 + (s[i] - '0');
            if (exp > MAX_DECIMAL_EXPONENT) {
                free(digits);
                return -1;
            }
        }
        if (neg)
            exp = -exp;
    }
    if (i != n || nd == 0) {
        free(digits);
        return -1;
    }

    /* "12.345e1" is 12345 * 10^(1 - 3). */
    bi_set_str(&r->num, digits, nd);
    free(digits);
    long scale = exp - frac;
    if (scale >= 0) {
        BigInt p;
        bi_init(&p);
        bi_pow10(&p, (size_t)scale);
        bi_mul(&r->num, &r->num, &p);
        bi_free(&p);
        bi_set_i64(&r->den, 1);
    } else {
        bi_pow10(&r->den, (size_t)-scale);
    }
    normalize(r);
    return 0;
}

int rat_is_zero(const Rational *a)
{
    return bi_is_zero(&a->num);
}

int rat_is_int(const Rational *a)
{
    return bi_is_one(&a->den);
}

int rat_sign(const Rational *a)
{
    return a->num.sign;
}

int rat_to_i64(const Rational *a, int64_t *out)
{
    if (!rat_is_int(a))
        return -1;
    return bi_to_i64(&a->num, out);
}

double rat_size_log10(const Rational *a)
{
    double n = bi_log10_abs(&a->num), d = bi_log10_abs(&a->den);
    return n > d ? n : d;
}

void rat_neg(Rational *r)
{
    bi_neg(&r->num);
}

void rat_add(Rational *r, const Rational *a, const Rational *b)
{
    /* a/b + c/d = (a*d + c*b) / (b*d) */
    Rational t;
    BigInt x;
    rat_init(&t);
    bi_init(&x);
    bi_mul(&t.num, &a->num, &b->den);
    bi_mul(&x, &b->num, &a->den);
    bi_add(&t.num, &t.num, &x);
    bi_mul(&t.den, &a->den, &b->den);
    normalize(&t);
    rat_swap(r, &t);
    rat_free(&t);
    bi_free(&x);
}

void rat_sub(Rational *r, const Rational *a, const Rational *b)
{
    Rational nb = *b; /* a view of b with the numerator's sign flipped; not freed */
    nb.num.sign = -nb.num.sign;
    rat_add(r, a, &nb);
}

/* Multiply without reducing; only correct when the result is already in lowest terms. */
static void mul_raw(Rational *r, const Rational *a, const Rational *b)
{
    bi_mul(&r->num, &a->num, &b->num);
    bi_mul(&r->den, &a->den, &b->den);
}

void rat_mul(Rational *r, const Rational *a, const Rational *b)
{
    Rational t;
    rat_init(&t);
    mul_raw(&t, a, b);
    normalize(&t);
    rat_swap(r, &t);
    rat_free(&t);
}

int rat_div(Rational *r, const Rational *a, const Rational *b)
{
    if (rat_is_zero(b))
        return -1;
    Rational t;
    rat_init(&t);
    bi_mul(&t.num, &a->num, &b->den);
    bi_mul(&t.den, &a->den, &b->num);
    normalize(&t);
    rat_swap(r, &t);
    rat_free(&t);
    return 0;
}

void rat_floor(Rational *r, const Rational *a)
{
    BigInt q, rem;
    bi_init(&q);
    bi_init(&rem);
    bi_divmod(&q, &rem, &a->num, &a->den);   /* rounds toward zero */
    if (rem.sign < 0) {
        BigInt one;
        bi_init(&one);
        bi_set_i64(&one, 1);
        bi_sub(&q, &q, &one);
        bi_free(&one);
    }
    rat_set_bigint(r, &q);
    bi_free(&q);
    bi_free(&rem);
}

void rat_ceil(Rational *r, const Rational *a)
{
    /* ceil(x) = -floor(-x) */
    Rational t;
    rat_init(&t);
    rat_copy(&t, a);
    rat_neg(&t);
    rat_floor(&t, &t);
    rat_neg(&t);
    rat_swap(r, &t);
    rat_free(&t);
}

int rat_mod(Rational *r, const Rational *a, const Rational *b)
{
    Rational t;
    rat_init(&t);
    if (rat_div(&t, a, b)) {
        rat_free(&t);
        return -1;
    }
    rat_floor(&t, &t);
    rat_mul(&t, &t, b);
    rat_sub(r, a, &t);
    rat_free(&t);
    return 0;
}

int rat_pow(Rational *r, const Rational *base, int64_t e)
{
    Rational result, b;
    rat_init(&result);
    rat_init(&b);
    rat_set_i64(&result, 1);
    rat_copy(&b, base);

    if (e < 0) {
        if (rat_is_zero(&b)) {
            rat_free(&result);
            rat_free(&b);
            return -1;
        }
        bi_swap(&b.num, &b.den);
        if (b.den.sign < 0) {
            bi_neg(&b.num);
            bi_neg(&b.den);
        }
    }

    /* Exponentiation by squaring: x^13 = x^8 * x^4 * x^1. Powers of a fraction
     * in lowest terms stay in lowest terms, so no gcd is needed. */
    uint64_t n = e < 0 ? (uint64_t)0 - (uint64_t)e : (uint64_t)e;
    while (n) {
        if (n & 1)
            mul_raw(&result, &result, &b);
        n >>= 1;
        if (n)
            mul_raw(&b, &b, &b);
    }
    rat_swap(r, &result);
    rat_free(&result);
    rat_free(&b);
    return 0;
}

/* ---- Formatting ---------------------------------------------------------- */

typedef struct {
    char *s;
    size_t len, cap;
} Buf;

static void buf_addn(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *p = realloc(b->s, cap);
        if (!p) {
            fputs("calc: out of memory\n", stderr);
            exit(1);
        }
        b->s = p;
        b->cap = cap;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

static void buf_add(Buf *b, const char *s)
{
    buf_addn(b, s, strlen(s));
}

static size_t strip_trailing_zeros(char *s, size_t len)
{
    while (len > 0 && s[len - 1] == '0')
        len--;
    s[len] = '\0';
    return len;
}

static char *format_fixed(const Rational *a, int digits)
{
    if (rat_is_int(a))
        return bi_to_str(&a->num);

    Buf out = {0};
    BigInt n, ip, rem, scale;
    bi_init(&n);
    bi_init(&ip);
    bi_init(&rem);
    bi_init(&scale);

    /* Integer part, then the first `digits` decimal places of the remainder:
     * frac = rem * 10^digits / den. */
    bi_copy(&n, &a->num);
    bi_abs(&n);
    bi_divmod(&ip, &rem, &n, &a->den);
    bi_pow10(&scale, (size_t)digits);
    bi_mul(&rem, &rem, &scale);
    bi_divmod(&n, &rem, &rem, &a->den);
    int exact = bi_is_zero(&rem);

    char *is = bi_to_str(&ip), *fs = bi_to_str(&n);
    size_t flen = strlen(fs);
    if (a->num.sign < 0)
        buf_add(&out, "-");
    buf_add(&out, is);
    buf_add(&out, ".");
    for (size_t i = flen; i < (size_t)digits; i++)
        buf_add(&out, "0");
    if (exact)
        flen = strip_trailing_zeros(fs, flen);
    buf_addn(&out, fs, flen);
    if (!exact) {
        buf_add(&out, "...");
        /* For a short fraction like 1/3, also show the exact value. */
        if (bi_num_digits(&a->num) + bi_num_digits(&a->den) <= 30) {
            char *ns = bi_to_str(&a->num), *ds = bi_to_str(&a->den);
            buf_add(&out, "  (= ");
            buf_add(&out, ns);
            buf_add(&out, "/");
            buf_add(&out, ds);
            buf_add(&out, ")");
            free(ns);
            free(ds);
        }
    }

    free(is);
    free(fs);
    bi_free(&n);
    bi_free(&ip);
    bi_free(&rem);
    bi_free(&scale);
    return out.s;
}

static char *format_sci(const Rational *a, int digits)
{
    if (rat_is_zero(a)) {
        char *s = malloc(2);
        if (s)
            strcpy(s, "0");
        return s;
    }

    BigInt A, D, p, q, rem;
    bi_init(&A);
    bi_init(&D);
    bi_init(&p);
    bi_init(&q);
    bi_init(&rem);

    /* Find exponent k and q = floor(|a| / 10^(k - digits + 1)) so that q has
     * exactly `digits` digits. The digit counts of num and den give k to
     * within one, so at most one retry is needed. */
    long k = (long)bi_num_digits(&a->num) - (long)bi_num_digits(&a->den);
    for (int attempt = 0;; attempt++) {
        long s = digits - 1 - k;
        bi_copy(&A, &a->num);
        bi_abs(&A);
        bi_copy(&D, &a->den);
        bi_pow10(&p, (size_t)(s >= 0 ? s : -s));
        if (s >= 0)
            bi_mul(&A, &A, &p);
        else
            bi_mul(&D, &D, &p);
        bi_divmod(&q, &rem, &A, &D);
        if (bi_num_digits(&q) >= (size_t)digits || attempt)
            break;
        k--;
    }
    int exact = bi_is_zero(&rem);

    Buf out = {0};
    char *m = bi_to_str(&q);
    size_t mlen = strlen(m);
    char exp[32];
    if (exact)
        mlen = strip_trailing_zeros(m, mlen);
    if (a->num.sign < 0)
        buf_add(&out, "-");
    buf_addn(&out, m, 1);
    if (mlen > 1) {
        buf_add(&out, ".");
        buf_addn(&out, m + 1, mlen - 1);
    }
    if (!exact)
        buf_add(&out, "...");
    snprintf(exp, sizeof exp, "e%+ld", k);
    buf_add(&out, exp);

    free(m);
    bi_free(&A);
    bi_free(&D);
    bi_free(&p);
    bi_free(&q);
    bi_free(&rem);
    return out.s;
}

char *rat_to_str(const Rational *a, int digits, int sci)
{
    if (digits < 1)
        digits = 1;
    return sci ? format_sci(a, digits) : format_fixed(a, digits);
}
