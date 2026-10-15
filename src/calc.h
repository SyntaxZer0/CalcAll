#ifndef CALC_H
#define CALC_H

#include <stddef.h>

#include "rational.h"

/*
 * The calculator engine. Front ends (the terminal REPL in main.c today, a GUI
 * later) only need this header: create a Calc, feed it lines, show the text
 * it returns.
 */
typedef struct {
    int digits;    /* decimal places (fixed) or significant digits (sci) */
    int sci;       /* nonzero: show results in scientific notation */
    Rational ans;  /* the last result, available as `ans` */
} Calc;

void calc_init(Calc *c);
void calc_free(Calc *c);

/*
 * Evaluates one line: an expression like "prod(k=1, 10, k)" or a command
 * like ":sci". Returns a malloc'd string to display (empty for a blank line),
 * or NULL with a message in err.
 */
char *calc_eval(Calc *c, const char *line, char *err, size_t errlen);

#endif
