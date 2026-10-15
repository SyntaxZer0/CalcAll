#ifndef FEVAL_H
#define FEVAL_H

#include "eval.h"
#include "parser.h"

/*
 * Fast evaluation in ordinary floating point, for drawing graphs.
 *
 * The exact engine (eval.c) is far too slow to evaluate an expression at
 * every pixel of a graph while it's being dragged. This compiles an
 * expression once, resolving every name and function ahead of time, into a
 * tree that evaluates with C doubles. Results are real numbers only: an
 * answer that would be complex (sqrt(-1)) or undefined (1/0) is NaN, which
 * the graph draws as a gap.
 */
typedef struct FEnv FEnv;
typedef struct FExpr FExpr;

/* What names can refer to: the user's definitions, plus local ones. */
FEnv *fenv_new(const Symbols *globals, int degrees);
void fenv_free(FEnv *env);
void fenv_set_var(FEnv *env, const char *name, double value);
/* A local function; body and params are borrowed and must outlive env. */
void fenv_set_func(FEnv *env, const char *name, char *const *params, int nparams, const Node *body);

/*
 * Compiles `expr` as a function of the named parameters (like {"x"}).
 * Names that can't be resolved evaluate to NaN and are listed by
 * fexpr_unknowns(), so the graph can offer a slider for them.
 */
FExpr *fexpr_compile(FEnv *env, const Node *expr, char *const *params, int nparams);
int fexpr_unknowns(const FExpr *f, const char *const **names);
int fexpr_uses_params(const FExpr *f);   /* does it depend on its parameters at all? */
double fexpr_eval(const FExpr *f, const double *args);
void fexpr_free(FExpr *f);

#endif
