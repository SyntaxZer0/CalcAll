#ifndef RATIONAL_H
#define RATIONAL_H

#include "bigint.h"

/*
 * Exact fraction num/den. Always kept in lowest terms with den > 0, so every
 * value has exactly one representation (zero is 0/1).
 *
 * Like BigInt, the output argument may alias any input.
 */
typedef struct {
    BigInt num;
    BigInt den;
} Rational;

void rat_init(Rational *r);                    /* r = 0 */
void rat_free(Rational *r);
void rat_copy(Rational *dst, const Rational *src);
void rat_swap(Rational *a, Rational *b);
void rat_set_i64(Rational *r, int64_t v);
void rat_set_bigint(Rational *r, const BigInt *v);
void rat_set_frac(Rational *r, const BigInt *num, const BigInt *den);   /* den != 0 */

/* Parses "42", "3.14", ".5", "6.02e23", "1.5e-3". Returns 0 on success. */
int  rat_set_decimal(Rational *r, const char *s, size_t n);

int  rat_is_zero(const Rational *a);
int  rat_is_int(const Rational *a);
int  rat_sign(const Rational *a);
int  rat_to_i64(const Rational *a, int64_t *out);   /* 0 if a is an integer that fits */
double rat_size_log10(const Rational *a);           /* ~log10 of max(|num|, den) */
double rat_log10_abs(const Rational *a);            /* ~log10|a|, -HUGE_VAL for zero */
double rat_to_double(const Rational *a);            /* for estimates only */

void rat_neg(Rational *r);
void rat_add(Rational *r, const Rational *a, const Rational *b);
void rat_sub(Rational *r, const Rational *a, const Rational *b);
void rat_mul(Rational *r, const Rational *a, const Rational *b);
int  rat_div(Rational *r, const Rational *a, const Rational *b);   /* -1 if b == 0 */
int  rat_mod(Rational *r, const Rational *a, const Rational *b);   /* a - b*floor(a/b); -1 if b == 0 */
void rat_floor(Rational *r, const Rational *a);
void rat_ceil(Rational *r, const Rational *a);
int  rat_pow(Rational *r, const Rational *base, int64_t e);       /* -1 for 0^negative */

/*
 * Writes |a| to `digits` significant digits: |a| ~= q * 10^(exp10 - digits + 1),
 * so exp10 is the power of ten of the leading digit. With `round`, q is
 * rounded half up, otherwise truncated. Returns 1 if nothing was cut off.
 * a must be nonzero.
 */
int  rat_sig_digits(const Rational *a, int digits, int round, BigInt *q, long *exp10);

/* r = a rounded to `digits` significant digits. */
void rat_round_sig(Rational *r, const Rational *a, int digits);

/*
 * Formats a for display (malloc'd).
 *   exact, sci == 0: whole numbers print every digit; fractions print up to
 *                    `digits` decimal places, ending in "..." if cut off.
 *   exact, sci != 0: scientific notation with `digits` significant digits.
 *   not exact:       "≈ " and `digits` significant digits, rounded.
 */
char *rat_to_str(const Rational *a, int digits, int sci, int exact);

#endif
