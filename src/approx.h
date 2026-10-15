#ifndef APPROX_H
#define APPROX_H

#include "rational.h"

/*
 * Approximate math for results that usually can't be written as a fraction
 * (square roots, pi, logarithms, trig...).
 *
 * Every function takes exact Rationals and returns a Rational rounded to
 * `prec` significant digits. Internally the work is done with extra "guard"
 * digits so that the digits returned are correct. Callers check the domain
 * (no ln of a negative number, etc.) before calling.
 */
void ap_pi(Rational *r, int prec);
void ap_sqrt(Rational *r, const Rational *x, int prec);   /* x >= 0 */
void ap_exp(Rational *r, const Rational *x, int prec);
void ap_ln(Rational *r, const Rational *x, int prec);     /* x > 0 */
void ap_sin(Rational *r, const Rational *x, int prec);    /* x in radians */
void ap_cos(Rational *r, const Rational *x, int prec);    /* x in radians */
void ap_atan(Rational *r, const Rational *x, int prec);   /* result in radians */

#endif
