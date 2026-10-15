#include "bigint.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q && n) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    return q;
}

static void reserve(BigInt *a, size_t n)
{
    if (n <= a->cap)
        return;
    size_t cap = a->cap ? a->cap : 4;
    while (cap < n)
        cap *= 2;
    a->limbs = xrealloc(a->limbs, cap * sizeof *a->limbs);
    a->cap = cap;
}

/* Drop leading zero limbs so that len is minimal, and fix the sign of zero. */
static void trim(BigInt *a)
{
    while (a->len > 0 && a->limbs[a->len - 1] == 0)
        a->len--;
    if (a->len == 0)
        a->sign = 0;
}

static void set_zero(BigInt *a)
{
    a->len = 0;
    a->sign = 0;
}

void bi_init(BigInt *a)
{
    a->sign = 0;
    a->len = 0;
    a->cap = 0;
    a->limbs = NULL;
}

void bi_free(BigInt *a)
{
    free(a->limbs);
    bi_init(a);
}

void bi_copy(BigInt *dst, const BigInt *src)
{
    if (dst == src)
        return;
    reserve(dst, src->len);
    if (src->len)
        memmove(dst->limbs, src->limbs, src->len * sizeof *src->limbs);
    dst->len = src->len;
    dst->sign = src->sign;
}

void bi_swap(BigInt *a, BigInt *b)
{
    BigInt t = *a;
    *a = *b;
    *b = t;
}

void bi_set_i64(BigInt *a, int64_t v)
{
    /* Negate in unsigned arithmetic so INT64_MIN works. */
    uint64_t mag = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    reserve(a, 3);
    a->len = 0;
    while (mag) {
        a->limbs[a->len++] = (uint32_t)(mag % BI_BASE);
        mag /= BI_BASE;
    }
    a->sign = v < 0 ? -1 : v > 0;
}

int bi_set_str(BigInt *a, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9')
            return -1;

    reserve(a, n / BI_BASE_DIGITS + 1);
    a->len = 0;
    /* Walk 9-digit chunks from the least significant (right) end. */
    size_t end = n;
    while (end > 0) {
        size_t start = end >= BI_BASE_DIGITS ? end - BI_BASE_DIGITS : 0;
        uint32_t limb = 0;
        for (size_t i = start; i < end; i++)
            limb = limb * 10 + (uint32_t)(s[i] - '0');
        a->limbs[a->len++] = limb;
        end = start;
    }
    a->sign = 1;
    trim(a);
    return 0;
}

char *bi_to_str(const BigInt *a)
{
    char *s = xrealloc(NULL, a->len * BI_BASE_DIGITS + 2);
    char *p = s;
    if (a->len == 0) {
        strcpy(s, "0");
        return s;
    }
    if (a->sign < 0)
        *p++ = '-';
    p += sprintf(p, "%u", (unsigned)a->limbs[a->len - 1]);
    for (size_t i = a->len - 1; i-- > 0;)
        p += sprintf(p, "%09u", (unsigned)a->limbs[i]);
    return s;
}

int bi_to_i64(const BigInt *a, int64_t *out)
{
    uint64_t mag = 0;
    for (size_t i = a->len; i-- > 0;) {
        if (mag > (UINT64_MAX - a->limbs[i]) / BI_BASE)
            return -1;
        mag = mag * BI_BASE + a->limbs[i];
    }
    if (a->sign >= 0) {
        if (mag > (uint64_t)INT64_MAX)
            return -1;
        *out = (int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX + 1)
            return -1;
        *out = mag == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)mag;
    }
    return 0;
}

size_t bi_num_digits(const BigInt *a)
{
    if (a->len == 0)
        return 1;
    size_t d = (a->len - 1) * BI_BASE_DIGITS;
    for (uint32_t top = a->limbs[a->len - 1]; top; top /= 10)
        d++;
    return d;
}

double bi_log10_abs(const BigInt *a)
{
    if (a->len == 0)
        return 0;
    /* The top two limbs give ~18 significant digits, plenty for an estimate. */
    double top = a->limbs[a->len - 1];
    size_t rest = 0;
    if (a->len >= 2) {
        top = top * BI_BASE + a->limbs[a->len - 2];
        rest = a->len - 2;
    }
    return log10(top) + (double)BI_BASE_DIGITS * (double)rest;
}

int bi_is_zero(const BigInt *a)
{
    return a->sign == 0;
}

int bi_is_one(const BigInt *a)
{
    return a->sign == 1 && a->len == 1 && a->limbs[0] == 1;
}

int bi_cmp_abs(const BigInt *a, const BigInt *b)
{
    if (a->len != b->len)
        return a->len < b->len ? -1 : 1;
    for (size_t i = a->len; i-- > 0;)
        if (a->limbs[i] != b->limbs[i])
            return a->limbs[i] < b->limbs[i] ? -1 : 1;
    return 0;
}

int bi_cmp(const BigInt *a, const BigInt *b)
{
    if (a->sign != b->sign)
        return a->sign < b->sign ? -1 : 1;
    int c = bi_cmp_abs(a, b);
    return a->sign < 0 ? -c : c;
}

void bi_neg(BigInt *a)
{
    a->sign = -a->sign;
}

void bi_abs(BigInt *a)
{
    if (a->sign < 0)
        a->sign = 1;
}

/* r = |a| + |b|. The caller sets the sign. */
static void add_abs(BigInt *r, const BigInt *a, const BigInt *b)
{
    if (a->len < b->len) {
        const BigInt *t = a;
        a = b;
        b = t;
    }
    BigInt t;
    bi_init(&t);
    reserve(&t, a->len + 1);
    uint32_t carry = 0;
    for (size_t i = 0; i < a->len; i++) {
        uint32_t s = a->limbs[i] + (i < b->len ? b->limbs[i] : 0) + carry;
        carry = s >= BI_BASE;
        t.limbs[i] = carry ? s - BI_BASE : s;
    }
    t.limbs[a->len] = carry;
    t.len = a->len + 1;
    t.sign = 1;
    trim(&t);
    bi_swap(r, &t);
    bi_free(&t);
}

/* r = |a| - |b|, requires |a| >= |b|. The caller sets the sign. */
static void sub_abs(BigInt *r, const BigInt *a, const BigInt *b)
{
    BigInt t;
    bi_init(&t);
    reserve(&t, a->len);
    uint32_t borrow = 0;
    for (size_t i = 0; i < a->len; i++) {
        uint32_t sub = (i < b->len ? b->limbs[i] : 0) + borrow;
        if (a->limbs[i] >= sub) {
            t.limbs[i] = a->limbs[i] - sub;
            borrow = 0;
        } else {
            t.limbs[i] = a->limbs[i] + BI_BASE - sub;
            borrow = 1;
        }
    }
    t.len = a->len;
    t.sign = 1;
    trim(&t);
    bi_swap(r, &t);
    bi_free(&t);
}

void bi_add(BigInt *r, const BigInt *a, const BigInt *b)
{
    if (a->sign == 0) {
        bi_copy(r, b);
        return;
    }
    if (b->sign == 0) {
        bi_copy(r, a);
        return;
    }
    if (a->sign == b->sign) {
        int sign = a->sign;
        add_abs(r, a, b);
        r->sign = sign;
        return;
    }
    int c = bi_cmp_abs(a, b);
    if (c == 0) {
        set_zero(r);
        return;
    }
    int sign = c > 0 ? a->sign : b->sign;
    if (c > 0)
        sub_abs(r, a, b);
    else
        sub_abs(r, b, a);
    r->sign = sign;
}

void bi_sub(BigInt *r, const BigInt *a, const BigInt *b)
{
    BigInt nb = *b; /* a view of b with the sign flipped; not freed */
    nb.sign = -nb.sign;
    bi_add(r, a, &nb);
}

void bi_mul(BigInt *r, const BigInt *a, const BigInt *b)
{
    if (a->sign == 0 || b->sign == 0) {
        set_zero(r);
        return;
    }
    BigInt t;
    bi_init(&t);
    size_t n = a->len + b->len;
    reserve(&t, n);
    memset(t.limbs, 0, n * sizeof *t.limbs);

    /* Schoolbook multiplication, the same method as on paper. */
    for (size_t i = 0; i < a->len; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b->len; j++) {
            uint64_t cur = t.limbs[i + j] + (uint64_t)a->limbs[i] * b->limbs[j] + carry;
            t.limbs[i + j] = (uint32_t)(cur % BI_BASE);
            carry = cur / BI_BASE;
        }
        for (size_t k = i + b->len; carry; k++) {
            uint64_t cur = t.limbs[k] + carry;
            t.limbs[k] = (uint32_t)(cur % BI_BASE);
            carry = cur / BI_BASE;
        }
    }
    t.len = n;
    t.sign = a->sign * b->sign;
    trim(&t);
    bi_swap(r, &t);
    bi_free(&t);
}

void bi_mul_small(BigInt *r, const BigInt *a, uint32_t m)
{
    if (a->sign == 0 || m == 0) {
        set_zero(r);
        return;
    }
    int sign = a->sign;
    size_t len = a->len;
    reserve(r, len + 1);
    uint64_t carry = 0;
    for (size_t i = 0; i < len; i++) {
        uint64_t cur = (uint64_t)a->limbs[i] * m + carry;
        r->limbs[i] = (uint32_t)(cur % BI_BASE);
        carry = cur / BI_BASE;
    }
    r->limbs[len] = (uint32_t)carry;
    r->len = len + 1;
    r->sign = sign;
    trim(r);
}

void bi_pow10(BigInt *r, size_t e)
{
    size_t zeros = e / BI_BASE_DIGITS;
    uint32_t top = 1;
    for (size_t i = 0; i < e % BI_BASE_DIGITS; i++)
        top *= 10;
    reserve(r, zeros + 1);
    memset(r->limbs, 0, zeros * sizeof *r->limbs);
    r->limbs[zeros] = top;
    r->len = zeros + 1;
    r->sign = 1;
}

/* q = |a| / d, returns |a| % d. Requires 0 < d < BI_BASE. q may alias a. */
static uint32_t divmod_small(BigInt *q, const BigInt *a, uint32_t d)
{
    reserve(q, a->len);
    uint64_t rem = 0;
    for (size_t i = a->len; i-- > 0;) {
        uint64_t cur = rem * BI_BASE + a->limbs[i];
        q->limbs[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    q->len = a->len;
    q->sign = 1;
    trim(q);
    return (uint32_t)rem;
}

/*
 * Long division of magnitudes, one base-10^9 "digit" of the quotient at a
 * time. Requires |a| >= |b| and b->len >= 2.
 */
static void divmod_long(BigInt *q, BigInt *r, const BigInt *a, const BigInt *b)
{
    BigInt prod;
    bi_init(&prod);
    size_t top = b->len - 1;

    /* The top (b->len - 1) limbs of a are smaller than b, so they can go
     * straight into the running remainder without producing quotient digits. */
    reserve(r, b->len + 1);
    memcpy(r->limbs, a->limbs + (a->len - top), top * sizeof *r->limbs);
    r->len = top;
    r->sign = 1;
    trim(r);

    size_t qlen = a->len - top;
    reserve(q, qlen);
    q->len = qlen;

    for (size_t i = qlen; i-- > 0;) {
        /* r = r * BASE + a[i] */
        reserve(r, r->len + 1);
        memmove(r->limbs + 1, r->limbs, r->len * sizeof *r->limbs);
        r->limbs[0] = a->limbs[i];
        r->len++;
        r->sign = 1;
        trim(r);

        /* Bound the quotient digit using the leading limbs of r and b, then
         * binary search for the largest digit d with b*d <= r. */
        uint64_t rt = 0;
        if (r->len > top)
            rt = r->limbs[top];
        if (r->len > top + 1)
            rt += (uint64_t)r->limbs[top + 1] * BI_BASE;
        uint64_t bt = b->limbs[top];
        uint64_t lo = rt / (bt + 1), hi = rt / bt;
        if (hi > BI_BASE - 1)
            hi = BI_BASE - 1;
        while (lo < hi) {
            uint64_t mid = lo + (hi - lo + 1) / 2;
            bi_mul_small(&prod, b, (uint32_t)mid);
            if (bi_cmp_abs(&prod, r) <= 0)
                lo = mid;
            else
                hi = mid - 1;
        }
        if (lo) {
            bi_mul_small(&prod, b, (uint32_t)lo);
            sub_abs(r, r, &prod);
        }
        q->limbs[i] = (uint32_t)lo;
    }
    q->sign = 1;
    trim(q);
    bi_free(&prod);
}

int bi_divmod(BigInt *q, BigInt *rem, const BigInt *a, const BigInt *b)
{
    if (b->sign == 0)
        return -1;
    int qsign = a->sign * b->sign, rsign = a->sign;
    BigInt Q, R;
    bi_init(&Q);
    bi_init(&R);

    if (bi_cmp_abs(a, b) < 0) {
        bi_copy(&R, a);
    } else if (b->len == 1) {
        bi_set_i64(&R, divmod_small(&Q, a, b->limbs[0]));
    } else {
        divmod_long(&Q, &R, a, b);
    }

    if (Q.len)
        Q.sign = qsign;
    if (R.len)
        R.sign = rsign;
    if (q)
        bi_swap(q, &Q);
    if (rem)
        bi_swap(rem, &R);
    bi_free(&Q);
    bi_free(&R);
    return 0;
}

void bi_gcd(BigInt *r, const BigInt *a, const BigInt *b)
{
    /* Euclid's algorithm: gcd(x, y) = gcd(y, x mod y). */
    BigInt x, y, t;
    bi_init(&x);
    bi_init(&y);
    bi_init(&t);
    bi_copy(&x, a);
    bi_copy(&y, b);
    bi_abs(&x);
    bi_abs(&y);
    while (y.sign != 0) {
        bi_divmod(NULL, &t, &x, &y);
        bi_swap(&x, &y);
        bi_swap(&y, &t);
    }
    bi_swap(r, &x);
    bi_free(&x);
    bi_free(&y);
    bi_free(&t);
}
