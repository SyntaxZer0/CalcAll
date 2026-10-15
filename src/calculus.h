#ifndef CALCULUS_H
#define CALCULUS_H

#include "parser.h"
#include "rational.h"

/*
 * Derivatives and integrals.
 *
 * A derivative is worked out by turning the expression's tree into the tree
 * of its derivative with the usual rules (product rule, chain rule, ...),
 * which the normal evaluator then works out. So d/dx(x^3) at 2 is 3*2^2 =
 * exactly 12, with no rounding: nothing is estimated from nearby points.
 *
 * Names are looked up the way the evaluator would, through these callbacks,
 * so that "ax" (a times x) and the user's own functions are understood.
 */
typedef struct {
    /*
     * Is `name` a variable or constant that's visible here? With `closed`,
     * the question is about the inside of a user function, which only sees
     * its parameters and the globals.
     */
    int (*known)(void *ctx, const char *name, int closed);
    /* The user's function `name`, or 0 if there's none. */
    int (*func)(void *ctx, const char *name, char *const **params, int *nparams, const Node **body);
    void *ctx;
    int degrees;   /* trig functions use degrees */
    char *err;
    size_t errlen;
} Deriver;

/* The derivative of n with respect to var, as a new tree; NULL (with a
 * message in d->err) for things like floor(x) that have none. */
Node *derive(const Node *n, const char *var, const Deriver *d);

/* The degree of n as a polynomial in var, or -1 if it isn't one. */
int poly_degree(const Node *n, const char *var, const Deriver *d);

/*
 * Weights w[0..deg] such that, for any polynomial p of degree <= deg,
 * the integral of p from a to b is (b - a) * sum of w[j] * p(a + j(b-a)/deg).
 * Exact fractions (Newton-Cotes). deg >= 1; w must hold deg + 1 Rationals.
 */
void newton_cotes(Rational *w, int deg);

/*
 * The integral of f from a to b to `digits` correct digits, by tanh-sinh
 * quadrature, working with `prec` digits. f returns -1 on error, with its
 * own message; `near_end` says x is very close to a or b, where it should
 * be used exactly (1 - x^2 would round to 0). Returns 0, -1 if f failed,
 * or -2 if the result didn't settle (the integral diverges, or f jumps or
 * has a corner inside the range). If it settled to fewer digits than asked,
 * *settled is how many (it's `digits` otherwise). The digits are relative to
 * the integral's size, or to 10^size_log10 if that's bigger: a piece of a
 * larger integral that's 0 only needs to be 0 to the larger one's digits.
 */
typedef int (*QuadFn)(void *ctx, const Rational *x, int near_end, Rational *re, Rational *im);
int quad_tanh_sinh(const Rational *a, const Rational *b, int digits, int prec, QuadFn f, void *ctx,
                   Rational *re, Rational *im, int *settled, double size_log10);

#endif
