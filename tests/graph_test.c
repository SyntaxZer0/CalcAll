/* Tests for graph.c: classifying lines, fast evaluation, exact key points. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/graph.h"

static int pass, fail;

static void expect(const char *name, int ok, const char *detail)
{
    if (ok) {
        pass++;
    } else {
        fail++;
        printf("FAIL: %s %s\n", name, detail ? detail : "");
    }
}

static void point(Calc *c, const Graph *g, int row, int other, PointKind k, double lo, double hi,
                  const char *want_x, const char *want_y)
{
    char *xs = NULL, *ys = NULL, detail[512];
    int rc = graph_exact_point(c, g, row, other, k, lo, hi, &xs, &ys);
    snprintf(detail, sizeof detail, "got (%s, %s), want (%s, %s)", xs ? xs : "-", ys ? ys : "-", want_x, want_y);
    expect(g->rows[row].text, rc == 0 && strcmp(xs, want_x) == 0 && strcmp(ys, want_y) == 0, detail);
    free(xs);
    free(ys);
}

int main(void)
{
    Calc c;
    Graph g = {0};
    calc_init(&c);
    char err[256];
    free(calc_eval(&c, "w(t) = t^2 + 1", err, sizeof err));   /* a user function from Calculate */

    char *lines[] = {
        "y = x^2 - 2",       /* 0 */
        "x^2 - 4",           /* 1 */
        "3x - 1",            /* 2 */
        "x^2 - 2x + 3",      /* 3 */
        "sin(x)",            /* 4 */
        "cos(x)",            /* 5 */
        "a = 2",             /* 6 slider */
        "y = ax - 1",        /* 7 uses the slider */
        "f(t) = t^3 - b",    /* 8 needs a slider for b */
        "2 + 3",             /* 9 a value */
        "w(x)",              /* 10 the user's function */
        "y = 5",             /* 11 a horizontal line */
        "x = 3",             /* 12 a vertical line */
        "sin = 2",           /* 13 reserved */
        "",                  /* 14 */
    };
    int n = sizeof lines / sizeof *lines;
    graph_build(&g, &c, lines, NULL, n);

    expect("kinds", g.rows[0].kind == GR_PLOT && g.rows[6].kind == GR_SLIDER && g.rows[8].kind == GR_FUNC &&
                        g.rows[9].kind == GR_VALUE && g.rows[11].plots && g.rows[12].kind == GR_XOFY &&
                        g.rows[13].kind == GR_ERROR && g.rows[14].kind == GR_EMPTY, NULL);
    expect("value", g.rows[9].value == 5, NULL);
    expect("slider value", g.rows[6].value == 2, NULL);
    expect("fast eval", fabs(graph_y(&g.rows[7], 3) - 5) < 1e-12 && fabs(graph_y(&g.rows[10], 2) - 5) < 1e-12, NULL);
    expect("gap", isnan(graph_y(&g.rows[4], 0)) == 0 && isnan(graph_y(&g.rows[8], 1)), NULL);
    const char *const *u;
    int nu = graph_unknowns(&g.rows[8], &u);
    expect("unknown b", nu == 1 && strcmp(u[0], "b") == 0, NULL);

    point(&c, &g, 0, -1, PT_ROOT, 1.4, 1.5, "\xE2\x89\x88 1.4142135623730950488", "\xE2\x89\x88 0");
    point(&c, &g, 1, -1, PT_ROOT, 1.9, 2.1, "2", "0");                       /* exact */
    point(&c, &g, 2, -1, PT_ROOT, 0.3, 0.4, "0.33333333333333333333...", "0");   /* exactly 1/3 */
    point(&c, &g, 3, -1, PT_MIN, 0.5, 1.6, "1", "2");                        /* exact turning point */
    point(&c, &g, 4, 5, PT_CROSS, 0.7, 0.9, "\xE2\x89\x88 0.78539816339744830962",
          "\xE2\x89\x88 0.7071067811865475244");
    point(&c, &g, 4, -1, PT_MAX, 1.5, 1.7, "\xE2\x89\x88 1.5707963267948966192", "\xE2\x89\x88 1");
    point(&c, &g, 7, -1, PT_ROOT, 0.4, 0.6, "0.5", "0");                     /* uses the slider exactly */
    point(&c, &g, 0, -1, PT_YINT, 0, 0, "0", "-2");

    graph_free(&g);

    /* Phase 2: equations, inequalities, parametric, polar, points */
    char *more[] = {
        "x^2 + y^2 = 1",          /* 0 implicit */
        "y < x",                  /* 1 inequality with an ordinary edge */
        "x^2 + y^2 >= 4",         /* 2 inequality, edge traced */
        "x = y^2",                /* 3 sideways */
        "(cos(t), sin(t))",       /* 4 parametric */
        "r = 2",                  /* 5 polar */
        "(1, 2), (3, 4)",         /* 6 points */
        "y = y^2 + x",            /* 7 implicit through y on both sides */
        "a = 2",                  /* 8 */
        "(1/3, a)",               /* 9 a point using the slider */
        "x + y",                  /* 10 needs an equation */
        "r < 2",                  /* 11 not supported */
        "y <= x^2",               /* 12 */
        "xy = 1",                 /* 13 the hyperbola, not a variable called xy */
    };
    char *ranges[2 * 14];
    for (int i = 0; i < 28; i++)
        ranges[i] = i == 9 ? "pi" : "";   /* line 4's t: 0 to pi */
    graph_build(&g, &c, more, ranges, 14);
    GraphRow *r = g.rows;
    expect("implicit", r[0].kind == GR_IMPLICIT && graph_d(&r[0], 1, 0) == 0 && graph_d(&r[0], 0, 0) == -1, NULL);
    expect("inequality", r[1].kind == GR_INEQ && r[1].fy && graph_inside(&r[1], graph_d(&r[1], 0, -1)) &&
                             !graph_inside(&r[1], graph_d(&r[1], 0, 1)), NULL);
    expect("inequality traced", r[2].kind == GR_INEQ && !r[2].fy && graph_inside(&r[2], graph_d(&r[2], 3, 0)) &&
                                    !graph_inside(&r[2], graph_d(&r[2], 0, 0)), NULL);
    expect("sideways", r[3].kind == GR_XOFY && graph_x_of_y(&r[3], 2) == 4, NULL);
    double px, py;
    expect("parametric", r[4].kind == GR_PARAM && graph_param(&r[4], 3.14159265358979 / 2, &px, &py) &&
                             fabs(px) < 1e-12 && fabs(py - 1) < 1e-12 && fabs(r[4].tmax - 3.14159265358979) < 1e-12,
           NULL);
    expect("polar", r[5].kind == GR_POLAR && graph_param(&r[5], 0, &px, &py) && px == 2 && py == 0, NULL);
    expect("points", r[6].kind == GR_POINTS && r[6].npoints == 2 && r[6].px[1] == 3 && r[6].py[1] == 4, NULL);
    expect("implicit y", r[7].kind == GR_IMPLICIT && graph_d(&r[7], 0, 1) == 0, NULL);
    expect("needs y", r[10].kind == GR_ERROR, NULL);
    expect("polar inequality", r[11].kind == GR_ERROR, NULL);
    expect("<=", r[12].kind == GR_INEQ && graph_inside(&r[12], graph_d(&r[12], 2, 4)), NULL);
    expect("xy = 1", r[13].kind == GR_IMPLICIT && graph_d(&r[13], 2, 0.5) == 0, NULL);
    char *xs = NULL, *ys = NULL;
    int rc = graph_exact_coords(&c, &g, 9, 0, &xs, &ys);
    expect("exact point", rc == 0 && strcmp(xs, "0.33333333333333333333...") == 0 && strcmp(ys, "2") == 0, xs);
    free(xs);
    free(ys);
    graph_free(&g);

    /* Derivatives and integrals as curves */
    char *calculus[] = {
        "y = d/dx(x^3)",                /* 0: 3x^2 */
        "y = int(t=0, x, cos(t))",      /* 1: sin x */
        "f(x) = x^2 + w(x)",            /* 2: a graph function using the user's w */
        "y = d/dx(f(x))",               /* 3: 4x */
        "a = 3",                        /* 4 */
        "y = d/dx(a x^2)",              /* 5: 6x, through the slider */
        "y = int(t=0, x, 1/sqrt(t))",   /* 6: 2 sqrt x, blowing up at 0 */
        "y = d/dx(x^2 - 2x)",           /* 7: root exactly at 1 */
        "y = d/dx(c x^2)",              /* 8: needs a slider for c */
        "y = int(t=0, x, abs(t - 1))",  /* 9: a corner at 1 */
    };
    graph_build(&g, &c, calculus, NULL, 10);
    r = g.rows;
    expect("derivative curve", fabs(graph_y(&r[0], 2) - 12) < 1e-9, NULL);
    expect("integral curve", fabs(graph_y(&r[1], 1) - sin(1)) < 1e-10, NULL);
    expect("derivative of a function", fabs(graph_y(&r[3], 1.5) - 6) < 1e-9, NULL);
    expect("derivative with a slider", fabs(graph_y(&r[5], 2) - 12) < 1e-9, NULL);
    expect("integral blowing up", fabs(graph_y(&r[6], 4) - 4) < 1e-8, NULL);
    nu = graph_unknowns(&r[8], &u);
    expect("derivative's slider", nu == 1 && strcmp(u[0], "c") == 0, NULL);
    expect("integral with a corner", fabs(graph_y(&r[9], 2) - 1) < 1e-4, NULL);
    point(&c, &g, 7, -1, PT_ROOT, 0.9, 1.1, "1", "0");
    graph_free(&g);
    calc_free(&c);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
