/*
 * Recursive-descent parser. Each grammar rule is one function, and rules for
 * operators that bind tighter are called from rules that bind looser:
 *
 *   expr    := term (('+' | '-') term)*
 *   term    := unary (('*' | '/' | '%')? unary)*   no operator: 2pi = 2*pi
 *   unary   := ('-' | '+') unary | power
 *   power   := postfix ('^' unary)?          right-associative: 2^3^2 = 2^9
 *   postfix := primary '!'*
 *   primary := NUMBER | NAME | NAME '(' args ')' | '(' expr ')'
 *
 * Because unary minus sits above power, -2^2 is -(2^2) = -4, as in math.
 */
#include "parser.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { T_END, T_NUM, T_NAME, T_OP } TokKind;

typedef struct {
    TokKind kind;
    const char *start;
    size_t len;
    char op;
} Token;

typedef struct {
    const char *src;
    const char *p;
    Token tok;
    char *err;
    size_t errlen;
    int failed;
} Parser;

/* Accepted spellings for the big operators, including Unicode Σ ∑ Π ∏. */
static const char *const SUM_NAMES[] = {"sum", "\xCE\xA3", "\xE2\x88\x91", NULL};
static const char *const PROD_NAMES[] = {"prod", "product", "\xCE\xA0", "\xE2\x88\x8F", NULL};

static void fail(Parser *P, const char *fmt, ...)
{
    if (P->failed)
        return;
    P->failed = 1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(P->err, P->errlen, fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n < P->errlen)
        snprintf(P->err + n, P->errlen - (size_t)n, " (at column %d)",
                 (int)(P->tok.start - P->src) + 1);
}

static int is_name_char(int c, int first)
{
    /* Bytes >= 0x80 are parts of UTF-8 characters such as Σ or π. */
    return isalpha(c) || c == '_' || c >= 0x80 || (!first && isdigit(c));
}

static void next(Parser *P)
{
    const char *p = P->p;
    while (isspace((unsigned char)*p))
        p++;

    Token t = {T_END, p, 0, 0};
    unsigned char c = (unsigned char)*p;
    if (c == '\0') {
        t.kind = T_END;
    } else if (isdigit(c) || (c == '.' && isdigit((unsigned char)p[1]))) {
        t.kind = T_NUM;
        while (isdigit((unsigned char)*p))
            p++;
        if (*p == '.')
            for (p++; isdigit((unsigned char)*p); p++)
                ;
        /* Exponent only if digits follow, so "2e" isn't swallowed. */
        if ((*p == 'e' || *p == 'E') &&
            (isdigit((unsigned char)p[1]) ||
             ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])))) {
            p += 2;
            while (isdigit((unsigned char)*p))
                p++;
        }
    } else if (c >= 0x80) {
        /* A symbol like π, Σ or Π is a name on its own, so "iπ" is i times π. */
        t.kind = T_NAME;
        p += c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    } else if (is_name_char(c, 1)) {
        t.kind = T_NAME;
        while (*p && (unsigned char)*p < 0x80 && is_name_char((unsigned char)*p, 0))
            p++;
    } else {
        t.kind = T_OP;
        t.op = *p++;
    }
    t.len = (size_t)(p - t.start);
    P->tok = t;
    P->p = p;
}

static int is_op(const Parser *P, char op)
{
    return P->tok.kind == T_OP && P->tok.op == op;
}

static int expect(Parser *P, char op)
{
    if (is_op(P, op)) {
        next(P);
        return 1;
    }
    if (P->tok.kind == T_END)
        fail(P, "expected '%c' but the input ended", op);
    else
        fail(P, "expected '%c' but found '%.*s'", op, (int)P->tok.len, P->tok.start);
    return 0;
}

static int tok_is(const Token *t, const char *const *names)
{
    for (; *names; names++)
        if (strlen(*names) == t->len && memcmp(*names, t->start, t->len) == 0)
            return 1;
    return 0;
}

static char *tok_dup(const Token *t)
{
    char *s = malloc(t->len + 1);
    if (!s) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    memcpy(s, t->start, t->len);
    s[t->len] = '\0';
    return s;
}

static Node *new_node(NodeKind kind, size_t nargs)
{
    Node *n = calloc(1, sizeof *n);
    if (!n || (nargs && !(n->args = calloc(nargs, sizeof *n->args)))) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    n->kind = kind;
    n->nargs = nargs;
    rat_init(&n->value);
    return n;
}

static void add_arg(Node *n, Node *arg)
{
    Node **args = realloc(n->args, (n->nargs + 1) * sizeof *args);
    if (!args) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    n->args = args;
    n->args[n->nargs++] = arg;
}

void node_free(Node *n)
{
    if (!n)
        return;
    for (size_t i = 0; i < n->nargs; i++)
        node_free(n->args[i]);
    for (size_t i = 0; i < n->nparams; i++)
        free(n->params[i]);
    free(n->params);
    free(n->text);
    free(n->args);
    free(n->name);
    rat_free(&n->value);
    free(n);
}

static Node *parse_expr(Parser *P);
static Node *parse_unary(Parser *P);

/* sum(k=1, 10, body) or sum(k, 1, 10, body); the '(' is already consumed. */
static Node *parse_big_op(Parser *P, NodeKind kind, const char *shown)
{
    if (P->tok.kind != T_NAME) {
        fail(P, "%s needs an index variable, like %s(k=1, 10, k^2)", shown, shown);
        return NULL;
    }
    Node *n = new_node(kind, 0);
    n->name = tok_dup(&P->tok);
    next(P);
    if (is_op(P, '=') || is_op(P, ','))
        next(P);
    else {
        fail(P, "expected '=' after the index variable, like %s(k=1, 10, k^2)", shown);
        node_free(n);
        return NULL;
    }
    for (int i = 0; i < 3; i++) {
        Node *arg = parse_expr(P);
        if (!arg || !expect(P, i < 2 ? ',' : ')')) {
            node_free(arg);
            node_free(n);
            return NULL;
        }
        add_arg(n, arg);
    }
    return n;
}

static Node *parse_primary(Parser *P)
{
    Token t = P->tok;

    if (t.kind == T_NUM) {
        Node *n = new_node(NODE_NUM, 0);
        if (rat_set_decimal(&n->value, t.start, t.len)) {
            fail(P, "invalid number '%.*s'", (int)t.len, t.start);
            node_free(n);
            return NULL;
        }
        next(P);
        return n;
    }

    if (t.kind == T_NAME) {
        next(P);
        if (!is_op(P, '(')) {
            Node *n = new_node(NODE_VAR, 0);
            n->name = tok_dup(&t);
            return n;
        }
        next(P);
        if (tok_is(&t, SUM_NAMES))
            return parse_big_op(P, NODE_SUM, "sum");
        if (tok_is(&t, PROD_NAMES))
            return parse_big_op(P, NODE_PROD, "prod");

        Node *n = new_node(NODE_CALL, 0);
        n->name = tok_dup(&t);
        if (is_op(P, ')')) {
            next(P);
            return n;
        }
        for (;;) {
            Node *arg = parse_expr(P);
            if (!arg) {
                node_free(n);
                return NULL;
            }
            add_arg(n, arg);
            if (is_op(P, ',')) {
                next(P);
                continue;
            }
            if (!expect(P, ')')) {
                node_free(n);
                return NULL;
            }
            return n;
        }
    }

    if (is_op(P, '(')) {
        next(P);
        Node *n = parse_expr(P);
        if (n && !expect(P, ')')) {
            node_free(n);
            return NULL;
        }
        return n;
    }

    if (t.kind == T_END)
        fail(P, "expected a number but the input ended");
    else
        fail(P, "unexpected '%.*s'", (int)t.len, t.start);
    return NULL;
}

static Node *parse_postfix(Parser *P)
{
    Node *n = parse_primary(P);
    while (n && is_op(P, '!')) {
        next(P);
        Node *f = new_node(NODE_FACT, 1);
        f->args[0] = n;
        n = f;
    }
    return n;
}

static Node *binop(char op, Node *l, Node *r)
{
    Node *n = new_node(NODE_BINOP, 2);
    n->op = op;
    n->args[0] = l;
    n->args[1] = r;
    return n;
}

static Node *parse_power(Parser *P)
{
    Node *base = parse_postfix(P);
    if (!base || !is_op(P, '^'))
        return base;
    next(P);
    Node *exp = parse_unary(P);
    if (!exp) {
        node_free(base);
        return NULL;
    }
    return binop('^', base, exp);
}

static Node *parse_unary(Parser *P)
{
    if (is_op(P, '-') || is_op(P, '+')) {
        char op = P->tok.op;
        next(P);
        Node *operand = parse_unary(P);
        if (!operand || op == '+')
            return operand;
        Node *n = new_node(NODE_NEG, 1);
        n->args[0] = operand;
        return n;
    }
    return parse_power(P);
}

/* Parses a left-associative chain like a * b / c using `sub` for operands. */
static Node *parse_chain(Parser *P, const char *ops, Node *(*sub)(Parser *))
{
    Node *l = sub(P);
    while (l && P->tok.kind == T_OP && strchr(ops, P->tok.op)) {
        char op = P->tok.op;
        next(P);
        Node *r = sub(P);
        if (!r) {
            node_free(l);
            return NULL;
        }
        l = binop(op, l, r);
    }
    return l;
}

/* With no operator between them, two operands multiply: 2pi, 3(4+1),
 * 2sin(30), as on a calculator. */
static Node *parse_term(Parser *P)
{
    Node *l = parse_unary(P);
    while (l) {
        char op;
        if (P->tok.kind == T_OP && strchr("*/%", P->tok.op)) {
            op = P->tok.op;
            next(P);
        } else if (P->tok.kind == T_NUM || P->tok.kind == T_NAME || is_op(P, '(')) {
            op = '*';
        } else {
            break;
        }
        Node *r = parse_unary(P);
        if (!r) {
            node_free(l);
            return NULL;
        }
        l = binop(op, l, r);
    }
    return l;
}

static Node *parse_expr(Parser *P)
{
    return parse_chain(P, "+-", parse_term);
}

/*
 * "x = ..." or "f(a, b) = ...". Looks ahead without consuming anything, and
 * falls back to a plain expression when the line isn't a definition.
 */
static Node *parse_statement(Parser *P)
{
    Parser start = *P;
    if (P->tok.kind == T_NAME) {
        Token name = P->tok;
        next(P);
        if (is_op(P, '=')) {   /* x = expression */
            next(P);
            Node *value = parse_expr(P);
            if (!value)
                return NULL;
            Node *n = new_node(NODE_ASSIGN, 1);
            n->name = tok_dup(&name);
            n->args[0] = value;
            return n;
        }
        if (is_op(P, '(')) {   /* f(a, b) = expression? */
            char *params[MAX_PARAMS];
            size_t np = 0;
            int ok = 1;
            next(P);
            while (ok && P->tok.kind == T_NAME && np < MAX_PARAMS) {
                params[np++] = tok_dup(&P->tok);
                next(P);
                if (is_op(P, ','))
                    next(P);
                else
                    break;
            }
            ok = np > 0 && is_op(P, ')');
            if (ok) {
                next(P);
                ok = is_op(P, '=');
            }
            if (ok) {
                next(P);
                const char *body_start = P->tok.start;
                Node *body = parse_expr(P);
                if (!body) {
                    while (np)
                        free(params[--np]);
                    return NULL;
                }
                Node *n = new_node(NODE_DEFUN, 1);
                n->name = tok_dup(&name);
                n->args[0] = body;
                n->params = malloc(np * sizeof *n->params);
                if (!n->params)
                    exit(1);
                memcpy(n->params, params, np * sizeof *params);
                n->nparams = np;
                Token t = {T_NAME, body_start, strlen(body_start), 0};
                n->text = tok_dup(&t);
                return n;
            }
            while (np)
                free(params[--np]);
        }
    }
    *P = start;   /* not a definition: rewind and read an expression */
    return parse_expr(P);
}

Node *parse(const char *src, char *err, size_t errlen)
{
    Parser P = {src, src, {T_END, src, 0, 0}, err, errlen, 0};
    next(&P);
    if (P.tok.kind == T_END) {
        fail(&P, "empty expression");
        return NULL;
    }
    Node *n = parse_statement(&P);
    if (n && P.tok.kind != T_END)
        fail(&P, "unexpected '%.*s'", (int)P.tok.len, P.tok.start);
    if (P.failed) {
        node_free(n);
        return NULL;
    }
    return n;
}
