#ifndef GRAPH_H
#define GRAPH_H

#include "calc.h"
#include "feval.h"

/*
 * The expression list of a Desmos-style graph. Each line is one of:
 *   y = x^2,  sin(x)            a curve (GR_PLOT)
 *   a = 2                       a slider (GR_SLIDER)
 *   b = 2a                      a variable worked out from others (GR_VAR)
 *   f(x) = x^2 + a              a function; drawn too if it has one parameter
 *   2 + 3                       just a value (GR_VALUE)
 * Definitions here are local to the graph, on top of the user's own.
 */
#define GRAPH_MAX_ROWS 16

typedef enum { GR_EMPTY, GR_PLOT, GR_SLIDER, GR_VAR, GR_FUNC, GR_VALUE, GR_ERROR } GraphKind;

typedef struct {
    GraphKind kind;
    char *name;        /* variable or function name */
    double value;      /* GR_SLIDER, GR_VAR, GR_VALUE */
    int plots;         /* draws a curve y = fx(x) */
    char *error;
    FExpr *fx;
    Node *tree;        /* the parsed line */
    char *text;
} GraphRow;

typedef struct {
    GraphRow rows[GRAPH_MAX_ROWS];
    int count;
    FEnv *env;
} Graph;

/* (Re)builds the graph from the lines' text. Cheap enough to do every frame. */
void graph_build(Graph *g, const Calc *c, char *const *texts, int n);
void graph_free(Graph *g);

/* The curve's height at x, or NaN where it isn't drawn. */
double graph_y(const GraphRow *r, double x);

/* Names a line uses that aren't defined anywhere: candidates for sliders. */
int graph_unknowns(const GraphRow *r, const char *const **names);

typedef enum { PT_ROOT, PT_MAX, PT_MIN, PT_CROSS, PT_YINT } PointKind;

/*
 * Pins down a key point of curve `row` with the exact engine, to the
 * calculator's full precision. For a root, or a crossing with curve `other`,
 * [lo, hi] must bracket the sign change; for a maximum or minimum it must
 * bracket the turning point. Roots that are simple numbers (2, 1/3) come out
 * exact. Writes x and y as text (malloc'd). Returns 0 on success.
 */
int graph_exact_point(Calc *c, const Graph *g, int row, int other, PointKind kind, double lo, double hi,
                      char **xs, char **ys);

#endif
