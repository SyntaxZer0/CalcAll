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
        "x = 3",             /* 12 not supported */
        "sin = 2",           /* 13 reserved */
        "",                  /* 14 */
    };
    int n = sizeof lines / sizeof *lines;
    graph_build(&g, &c, lines, n);

    expect("kinds", g.rows[0].kind == GR_PLOT && g.rows[6].kind == GR_SLIDER && g.rows[8].kind == GR_FUNC &&
                        g.rows[9].kind == GR_VALUE && g.rows[11].plots && g.rows[12].kind == GR_ERROR &&
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
    calc_free(&c);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
