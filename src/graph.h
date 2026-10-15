#ifndef GRAPH_H
#define GRAPH_H

#include "calc.h"
#include "feval.h"

/*
 * The expression list of a Desmos-style graph. Each line is one of:
 *   y = x^2,  sin(x)            a curve (GR_PLOT)
 *   x = y^2                     a sideways curve (GR_XOFY)
 *   x^2 + y^2 = 1               an equation in x and y (GR_IMPLICIT)
 *   y < sin(x),  x^2+y^2 <= 4   a shaded region (GR_INEQ)
 *   (cos(t), sin(t))            a parametric curve (GR_PARAM)
 *   r = 1 + cos(θ)              a polar curve (GR_POLAR)
 *   (1, 2),  (1, 2), (3, 4)     points (GR_POINTS)
 *   a = 2                       a slider (GR_SLIDER)
 *   b = 2a                      a variable worked out from others (GR_VAR)
 *   f(x) = x^2 + a              a function; drawn too if it has one parameter
 *   2 + 3                       just a value (GR_VALUE)
 * Definitions here are local to the graph, on top of the user's own.
 */
#define GRAPH_MAX_ROWS 16
#define GRAPH_MAX_UNKNOWNS 16

typedef enum {
    GR_EMPTY, GR_PLOT, GR_SLIDER, GR_VAR, GR_FUNC, GR_VALUE, GR_ERROR,
    GR_XOFY, GR_IMPLICIT, GR_INEQ, GR_PARAM, GR_POLAR, GR_POINTS
} GraphKind;

typedef struct {
    GraphKind kind;
    char *name;        /* variable or function name */
    double value;      /* GR_SLIDER, GR_VAR, GR_VALUE */
    int plots;         /* draws a curve y = f(x): GR_PLOT, and GR_FUNC with one parameter */
    char *error;
    FExpr *fx;         /* f(x); g(y) for GR_XOFY; x(t); r(θ); the left side for equations */
    FExpr *fy;         /* y(t) for GR_PARAM; for "y < f(x)", the boundary f(x) */
    int dmode;         /* equations: 0 lhs - rhs, 1 y - f(x, y), 2 x - f(x, y) */
    char op;           /* GR_INEQ: < > l (<=) g (>=) */
    int boundary_flip; /* GR_INEQ with fy: the region is above the boundary */
    double tmin, tmax; /* GR_PARAM, GR_POLAR */
    int npoints;       /* GR_POINTS */
    double *px, *py;
    char *unknowns[GRAPH_MAX_UNKNOWNS];
    int nunknowns;
    Node *tree;        /* the parsed line */
    char *text;
} GraphRow;

typedef struct {
    GraphRow rows[GRAPH_MAX_ROWS];
    int count;
    FEnv *env;
} Graph;

/*
 * (Re)builds the graph from the lines' text, and each line's t or θ range
 * (`ranges` holds two texts per line, or is NULL for 0 to 2π). Cheap
 * enough to do every frame.
 */
void graph_build(Graph *g, const Calc *c, char *const *texts, char *const *ranges, int n);
void graph_free(Graph *g);

/* The curve's height at x, or NaN where it isn't drawn. */
double graph_y(const GraphRow *r, double x);

/* GR_XOFY: x at height y. */
double graph_x_of_y(const GraphRow *r, double y);

/* Equations and inequalities: a value that is 0 on the curve, and whose
 * sign tells the two sides apart. */
double graph_d(const GraphRow *r, double x, double y);

/* Is a point with graph_d() = d inside an inequality's region? */
int graph_inside(const GraphRow *r, double d);

/* GR_PARAM and GR_POLAR: the point at t (or θ). Returns 0 if undefined. */
int graph_param(const GraphRow *r, double t, double *x, double *y);

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

/* GR_POINTS: point `index` worked out exactly. Returns 0 on success. */
int graph_exact_coords(Calc *c, const Graph *g, int row, int index, char **xs, char **ys);

#endif
