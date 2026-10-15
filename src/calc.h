#ifndef CALC_H
#define CALC_H

#include <stddef.h>

#include "eval.h"

/*
 * The calculator engine. Front ends (the terminal REPL in main.c today, a GUI
 * later) only need this header: create a Calc, feed it lines, show the text
 * it returns.
 */
typedef struct {
    int digits;    /* decimal places (fixed) or significant digits (sci) */
    int sci;       /* nonzero: show results in scientific notation */
    int degrees;   /* nonzero: trig functions use degrees */
    Value ans;     /* the last result, available as `ans` */
    Symbols syms;  /* the user's variables and functions */
    int result_is_value;   /* the last calc_eval() gave a number (else a message) */
    char *store;   /* file definitions are saved to, or NULL */
} Calc;

void calc_init(Calc *c);
void calc_free(Calc *c);

/*
 * Where definitions are kept between runs: $CALC_DEFINITIONS if set (empty
 * means don't save), else ~/.calc_definitions. NULL when there's nowhere.
 */
const char *calc_default_store(void);

/*
 * Loads the definitions in `path` (if it exists) and saves to it from now on,
 * after every change. NULL turns saving off. Returns how many were loaded,
 * or -1 if the file couldn't be read.
 */
int calc_use_store(Calc *c, const char *path);

/*
 * Evaluates one line: an expression like "prod(k=1, 10, k)", a definition
 * like "x = 5" or "f(x) = x^2", or a command like ":sci". Returns a malloc'd
 * string to display (empty for a blank line), or NULL with a message in err.
 */
char *calc_eval(Calc *c, const char *line, char *err, size_t errlen);

/* ---- Tables, like a Casio's TABLE mode ---------------------------------------- */

#define CALC_TABLE_MAX 1000   /* most rows a table can have */

typedef struct {
    Value x, f, g;
    char *f_error, *g_error;   /* why a cell couldn't be computed, else NULL */
} TableRow;

typedef struct {
    TableRow *rows;
    int count;
    int has_g;
} Table;

/*
 * Fills `out` with f(x) (and g(x) unless g is NULL or empty) for x = start,
 * start + step, ... up to end. f and g are expressions in x, like "x^2 + 1";
 * start, end and step are expressions too. x goes up exactly, so a step of
 * 0.1 never drifts. Returns 0, or -1 with a message in err.
 */
int calc_table(Calc *c, const char *f, const char *g, const char *start, const char *end,
               const char *step, Table *out, char *err, size_t errlen);
void table_free(Table *t);

/* A value shortened to fit `width` characters, for a table cell (malloc'd). */
char *calc_cell(const Calc *c, const Value *v, int width);

#endif
