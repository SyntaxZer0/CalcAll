/* The table of user-defined variables and functions (see Symbols in eval.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    return q;
}

static char *dup(const char *s)
{
    return strcpy(xrealloc(NULL, strlen(s) + 1), s);
}

void sym_init(Symbols *s)
{
    memset(s, 0, sizeof *s);
}

static void func_free(UserFunc *f)
{
    free(f->name);
    for (int i = 0; i < f->nparams; i++)
        free(f->params[i]);
    free(f->params);
    node_free(f->body);
    free(f->text);
}

void sym_free(Symbols *s)
{
    for (int i = 0; i < s->nvars; i++) {
        free(s->vars[i].name);
        val_free(&s->vars[i].value);
    }
    for (int i = 0; i < s->nfuncs; i++)
        func_free(&s->funcs[i]);
    free(s->vars);
    free(s->funcs);
    sym_init(s);
}

static int var_index(const Symbols *s, const char *name)
{
    for (int i = 0; i < s->nvars; i++)
        if (strcmp(s->vars[i].name, name) == 0)
            return i;
    return -1;
}

static int func_index(const Symbols *s, const char *name)
{
    for (int i = 0; i < s->nfuncs; i++)
        if (strcmp(s->funcs[i].name, name) == 0)
            return i;
    return -1;
}

const Value *sym_var(const Symbols *s, const char *name)
{
    int i = var_index(s, name);
    return i < 0 ? NULL : &s->vars[i].value;
}

const UserFunc *sym_func(const Symbols *s, const char *name)
{
    int i = func_index(s, name);
    return i < 0 ? NULL : &s->funcs[i];
}

void sym_set_var(Symbols *s, const char *name, const Value *v)
{
    int i = var_index(s, name);
    if (i < 0) {
        s->vars = xrealloc(s->vars, (size_t)(s->nvars + 1) * sizeof *s->vars);
        i = s->nvars++;
        s->vars[i].name = dup(name);
        val_init(&s->vars[i].value);
    }
    val_copy(&s->vars[i].value, v);
}

void sym_set_func(Symbols *s, const char *name, char **params, int nparams, Node *body,
                  const char *text)
{
    int i = func_index(s, name);
    if (i >= 0) {
        func_free(&s->funcs[i]);   /* redefining replaces it */
    } else {
        s->funcs = xrealloc(s->funcs, (size_t)(s->nfuncs + 1) * sizeof *s->funcs);
        i = s->nfuncs++;
    }
    UserFunc *f = &s->funcs[i];
    f->name = dup(name);
    f->params = params;
    f->nparams = nparams;
    f->body = body;
    f->text = dup(text);
}

int sym_remove(Symbols *s, const char *name)
{
    int removed = 0, i;
    if ((i = var_index(s, name)) >= 0) {
        free(s->vars[i].name);
        val_free(&s->vars[i].value);
        memmove(s->vars + i, s->vars + i + 1, (size_t)(--s->nvars - i) * sizeof *s->vars);
        removed++;
    }
    if ((i = func_index(s, name)) >= 0) {
        func_free(&s->funcs[i]);
        memmove(s->funcs + i, s->funcs + i + 1, (size_t)(--s->nfuncs - i) * sizeof *s->funcs);
        removed++;
    }
    return removed;
}
