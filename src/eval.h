#ifndef EVAL_H
#define EVAL_H

#include <stddef.h>

#include "parser.h"
#include "rational.h"

/*
 * A computed value: r + im*i, where im is 0 for a real number. Exact values
 * are true fractions. Approximate values (anything that went through
 * sqrt(2), pi, sin, ...) are fractions rounded to the working precision, and
 * are shown with a "≈".
 */
typedef struct {
    Rational r;    /* real part */
    Rational im;   /* imaginary part */
    int exact;
} Value;

void val_init(Value *v);   /* exact 0 */
void val_free(Value *v);
void val_copy(Value *dst, const Value *src);
void val_swap(Value *a, Value *b);

/* Formats a value for display, like rat_to_str() but with "3 + 4i" for
 * complex numbers (malloc'd). */
char *val_to_str(const Value *v, int digits, int sci);

/*
 * A chain of variable bindings. sum() and prod() push a binding for their
 * index on top of the caller's chain, so nested sums and outer variables like
 * `ans` all stay visible, and the innermost binding of a name wins.
 */
typedef struct Binding Binding;
struct Binding {
    const char *name;
    const Value *value;
    const Binding *next;
};

typedef struct {
    int prec;      /* significant digits kept in approximate values */
    int degrees;   /* nonzero: trig functions use degrees instead of radians */
} EvalOptions;

/* Returns 0 and stores the result in out, or -1 with a message in err. */
int eval(const Node *n, const Binding *env, const EvalOptions *opt, Value *out, char *err,
         size_t errlen);

#endif
