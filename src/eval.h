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

/* Variables and functions the user has defined: x = 5, f(x) = x^2. */
typedef struct {
    char *name;
    Value value;
} UserVar;

typedef struct {
    char *name;
    char **params;
    int nparams;
    Node *body;
    char *text;   /* the body as typed */
} UserFunc;

typedef struct {
    UserVar *vars;
    int nvars;
    UserFunc *funcs;
    int nfuncs;
} Symbols;

void sym_init(Symbols *s);
void sym_free(Symbols *s);
const Value *sym_var(const Symbols *s, const char *name);
const UserFunc *sym_func(const Symbols *s, const char *name);
void sym_set_var(Symbols *s, const char *name, const Value *v);
/* Takes ownership of params (and its strings) and body. */
void sym_set_func(Symbols *s, const char *name, char **params, int nparams, Node *body,
                  const char *text);
int  sym_remove(Symbols *s, const char *name);   /* returns how many were removed */

/* Is this name built in (a function, constant or ans), so it can't be redefined? */
int eval_is_reserved(const char *name);

typedef struct {
    int prec;      /* significant digits kept in approximate values */
    int degrees;   /* nonzero: trig functions use degrees instead of radians */
    const Symbols *syms;   /* user variables and functions, or NULL */
} EvalOptions;

/* Returns 0 and stores the result in out, or -1 with a message in err. */
int eval(const Node *n, const Binding *env, const EvalOptions *opt, Value *out, char *err,
         size_t errlen);

#endif
