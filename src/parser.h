#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include "rational.h"

/*
 * The parser turns text into a tree (an "abstract syntax tree"). For example
 * "2 + 3 * k" becomes:
 *
 *        +
 *       / \
 *      2   *
 *         / \
 *        3   k
 *
 * Keeping the tree, instead of computing while parsing, is what lets
 * sum() and prod() evaluate their body once for every value of the index.
 */
typedef enum {
    NODE_NUM,    /* a literal number, in `value` */
    NODE_VAR,    /* a variable reference, `name` */
    NODE_NEG,    /* -args[0] */
    NODE_BINOP,  /* args[0] `op` args[1], op is one of + - * / % ^ */
    NODE_FACT,   /* args[0]! */
    NODE_CALL,   /* name(args...) */
    NODE_SUM,    /* sum of args[2] for `name` from args[0] to args[1] */
    NODE_PROD    /* product of args[2] for `name` from args[0] to args[1] */
} NodeKind;

typedef struct Node Node;
struct Node {
    NodeKind kind;
    char op;
    Rational value;
    char *name;
    Node **args;
    size_t nargs;
};

/* Returns NULL and writes a message to err on a syntax error. */
Node *parse(const char *src, char *err, size_t errlen);
void node_free(Node *n);

#endif
