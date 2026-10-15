#ifndef CMATH_H
#define CMATH_H

#include "rational.h"

/*
 * Complex numbers re + im*i with Rational parts.
 *
 * The arithmetic (add, sub, mul, div) is exact. The cx_exp, cx_ln, ...
 * functions are approximations like those in approx.h: they return parts
 * rounded to `prec` significant digits, measured against the larger part
 * (see cx_round). Outputs may alias inputs.
 */
typedef struct {
    Rational re, im;
} Complex;

void cx_init(Complex *z);   /* 0 */
void cx_free(Complex *z);
void cx_copy(Complex *dst, const Complex *src);
void cx_swap(Complex *a, Complex *b);
int  cx_is_zero(const Complex *z);

void cx_add(Complex *r, const Complex *a, const Complex *b);
void cx_sub(Complex *r, const Complex *a, const Complex *b);
void cx_mul(Complex *r, const Complex *a, const Complex *b);
int  cx_div(Complex *r, const Complex *a, const Complex *b);   /* -1 if b == 0 */
void cx_norm(Rational *r, const Complex *z);                   /* re^2 + im^2, exact */
void cx_pow_int(Complex *r, const Complex *z, int64_t n);      /* exact; z != 0 if n < 0 */

/*
 * Rounds both parts to `prec` significant digits of the larger one, so a
 * part that is only rounding noise next to the other (like the 1e-40i in
 * e^(i*pi) = -1) becomes exactly 0.
 */
void cx_round(Complex *z, int prec);

void cx_arg(Rational *r, const Complex *z, int prec);   /* angle in radians, (-pi, pi]; z != 0 */
void cx_exp(Complex *r, const Complex *z, int prec);
void cx_ln(Complex *r, const Complex *z, int prec);     /* principal value; z != 0 */
void cx_sqrt(Complex *r, const Complex *z, int prec);   /* principal value */
void cx_sin(Complex *r, const Complex *z, int prec);
void cx_cos(Complex *r, const Complex *z, int prec);
void cx_sinh(Complex *r, const Complex *z, int prec);
void cx_cosh(Complex *r, const Complex *z, int prec);

#endif
