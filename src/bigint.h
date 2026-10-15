#ifndef BIGINT_H
#define BIGINT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Arbitrary-precision signed integer.
 *
 * The magnitude is stored as an array of "limbs", least significant first,
 * each holding a value in [0, 10^9). Base 10^9 is used so that printing in
 * decimal is trivial (every limb is exactly 9 decimal digits) and so that the
 * product of two limbs still fits in a uint64_t.
 *
 * Every result-producing function writes into its first argument and is safe
 * to call when that argument is also one of the inputs (e.g. bi_add(&a, &a, &b)).
 */
typedef struct {
    int sign;          /* -1, 0 or +1; zero always has sign 0 and len 0 */
    size_t len;        /* limbs in use */
    size_t cap;        /* limbs allocated */
    uint32_t *limbs;
} BigInt;

#define BI_BASE 1000000000u
#define BI_BASE_DIGITS 9

void bi_init(BigInt *a);                       /* a = 0 */
void bi_free(BigInt *a);
void bi_copy(BigInt *dst, const BigInt *src);
void bi_swap(BigInt *a, BigInt *b);

void bi_set_i64(BigInt *a, int64_t v);
int  bi_set_str(BigInt *a, const char *s, size_t n);   /* decimal digits only; 0 on success */
char *bi_to_str(const BigInt *a);                      /* malloc'd decimal string */
int  bi_to_i64(const BigInt *a, int64_t *out);         /* 0 if the value fits */
size_t bi_num_digits(const BigInt *a);                 /* decimal digits in |a| (1 for zero) */
double bi_log10_abs(const BigInt *a);                  /* approximate log10|a|; 0 for zero */

int  bi_is_zero(const BigInt *a);
int  bi_is_one(const BigInt *a);
int  bi_cmp(const BigInt *a, const BigInt *b);
int  bi_cmp_abs(const BigInt *a, const BigInt *b);

void bi_neg(BigInt *a);
void bi_abs(BigInt *a);
void bi_add(BigInt *r, const BigInt *a, const BigInt *b);
void bi_sub(BigInt *r, const BigInt *a, const BigInt *b);
void bi_mul(BigInt *r, const BigInt *a, const BigInt *b);
void bi_mul_small(BigInt *r, const BigInt *a, uint32_t m);   /* requires m < BI_BASE */
void bi_pow10(BigInt *r, size_t e);
void bi_gcd(BigInt *r, const BigInt *a, const BigInt *b);    /* always >= 0 */
void bi_root(BigInt *r, const BigInt *a, unsigned k);        /* floor(|a|^(1/k)), k >= 1 */

/*
 * Truncating division, like C's / and %: q = a / b rounded toward zero and
 * rem = a - q*b (rem has the sign of a). Either q or rem may be NULL.
 * Returns -1 on division by zero, 0 otherwise.
 */
int  bi_divmod(BigInt *q, BigInt *rem, const BigInt *a, const BigInt *b);

#endif
