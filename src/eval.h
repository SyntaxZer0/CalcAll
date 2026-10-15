#ifndef EVAL_H
#define EVAL_H

#include <stddef.h>

#include "parser.h"
#include "rational.h"

/*
 * A chain of variable bindings. sum() and prod() push a binding for their
 * index on top of the caller's chain, so nested sums and outer variables like
 * `ans` all stay visible, and the innermost binding of a name wins.
 */
typedef struct Binding Binding;
struct Binding {
    const char *name;
    const Rational *value;
    const Binding *next;
};

/* Returns 0 and stores the result in out, or -1 with a message in err. */
int eval(const Node *n, const Binding *env, Rational *out, char *err, size_t errlen);

#endif
