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
    NODE_PROD,   /* product of args[2] for `name` from args[0] to args[1] */
    NODE_ASSIGN, /* name = args[0]  (only as a whole line) */
    NODE_DEFUN,  /* name(params...) = args[0]  (only as a whole line) */
    NODE_REL,    /* args[0] `op` args[1], op one of = < > l (<=) g (>=); graph lines only */
    NODE_POINTS  /* points (args[0], args[1]), (args[2], args[3]), ...; graph lines only */
} NodeKind;

#define MAX_PARAMS 8

typedef struct Node Node;
struct Node {
    NodeKind kind;
    char op;
    Rational value;
    char *name;
    Node **args;
    size_t nargs;
    char **params;   /* NODE_DEFUN: parameter names */
    size_t nparams;
    char *text;      /* NODE_DEFUN: the body as typed, for showing it later */
};

/*
 * Parses a line: an expression, "x = expression", or "f(x, y) = expression".
 * Returns NULL and writes a message to err on a syntax error.
 */
Node *parse(const char *src, char *err, size_t errlen);

/*
 * Like parse(), for a line of a graph, which can also be an equation or
 * inequality ("x^2 + y^2 = 1", "y < sin(x)", "<=", ">=", "≤", "≥") or points
 * ("(1, 2)", "(cos(t), sin(t))", "(1, 2), (3, 4)").
 */
Node *parse_graph_line(const char *src, char *err, size_t errlen);
void node_free(Node *n);

#endif
