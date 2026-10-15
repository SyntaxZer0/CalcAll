#include "eval.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Safety limits so a typo like 9^9^9 fails fast instead of freezing. */
#define MAX_RESULT_DIGITS 1000000
#define MAX_FACTORIAL 50000
#define MAX_TERMS 1000000

typedef struct {
    char *msg;
    size_t len;
} Err;

static int fail(Err *e, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, e->len, fmt, ap);
    va_end(ap);
    return -1;
}

static int need_int(const Rational *v, const char *what, int64_t *out, Err *e)
{
    if (!rat_is_int(v))
        return fail(e, "%s must be a whole number", what);
    if (rat_to_i64(v, out))
        return fail(e, "%s is too large", what);
    return 0;
}

static int check_size(const Rational *v, Err *e)
{
    if (rat_size_log10(v) > MAX_RESULT_DIGITS)
        return fail(e, "result has more than %d digits", MAX_RESULT_DIGITS);
    return 0;
}

static int eval_node(const Node *n, const Binding *env, Rational *out, Err *e);

static int power(Rational *out, const Rational *base, const Rational *exp, Err *e)
{
    int64_t x;
    if (!rat_is_int(exp))
        return fail(e, "non-integer exponents are not supported yet");
    if (rat_to_i64(exp, &x))
        return fail(e, "exponent is too large");
    if (rat_is_zero(base) && x < 0)
        return fail(e, "division by zero (0 to a negative power)");
    double digits = fabs((double)x) * rat_size_log10(base);
    if (digits > MAX_RESULT_DIGITS)
        return fail(e, "result would have about %.0f digits (limit is %d)", digits,
                    MAX_RESULT_DIGITS);
    rat_pow(out, base, x);
    return 0;
}

static int factorial(Rational *out, const Rational *v, Err *e)
{
    int64_t n;
    if (need_int(v, "factorial argument", &n, e))
        return -1;
    if (n < 0)
        return fail(e, "factorial of a negative number is undefined");
    if (n > MAX_FACTORIAL)
        return fail(e, "%lld! is too large (limit is %d!)", (long long)n, MAX_FACTORIAL);
    BigInt acc;
    bi_init(&acc);
    bi_set_i64(&acc, 1);
    for (int64_t i = 2; i <= n; i++)
        bi_mul_small(&acc, &acc, (uint32_t)i);
    rat_set_bigint(out, &acc);
    bi_free(&acc);
    return 0;
}

static int binary(Rational *out, char op, const Rational *a, const Rational *b, Err *e)
{
    switch (op) {
    case '+':
        rat_add(out, a, b);
        break;
    case '-':
        rat_sub(out, a, b);
        break;
    case '*':
        rat_mul(out, a, b);
        break;
    case '/':
        if (rat_div(out, a, b))
            return fail(e, "division by zero");
        break;
    case '%':
        if (rat_mod(out, a, b))
            return fail(e, "modulo by zero");
        break;
    case '^':
        return power(out, a, b, e);
    default:
        return fail(e, "unknown operator '%c'", op);
    }
    return check_size(out, e);
}

/* sum(k=a, b, body) or prod(k=a, b, body). */
static int big_op(const Node *n, const Binding *env, Rational *out, Err *e)
{
    int is_sum = n->kind == NODE_SUM;
    const char *opname = is_sum ? "sum" : "prod";
    Rational lo, hi, idx, term, acc;
    int64_t a, b;
    int rc = -1;

    rat_init(&lo);
    rat_init(&hi);
    rat_init(&idx);
    rat_init(&term);
    rat_init(&acc);

    if (eval_node(n->args[0], env, &lo, e) || eval_node(n->args[1], env, &hi, e))
        goto done;
    if (need_int(&lo, "lower bound", &a, e) || need_int(&hi, "upper bound", &b, e))
        goto done;

    /* An empty range gives the identity: 0 for a sum, 1 for a product. */
    uint64_t count = b >= a ? (uint64_t)b - (uint64_t)a + 1 : 0;
    if (count > MAX_TERMS) {
        fail(e, "%s has too many terms (limit is %d)", opname, MAX_TERMS);
        goto done;
    }

    rat_set_i64(&acc, is_sum ? 0 : 1);
    for (uint64_t i = 0; i < count; i++) {
        rat_set_i64(&idx, a + (int64_t)i);
        Binding bind = {n->name, &idx, env};
        if (eval_node(n->args[2], &bind, &term, e))
            goto done;
        if (is_sum)
            rat_add(&acc, &acc, &term);
        else
            rat_mul(&acc, &acc, &term);
        if (check_size(&acc, e))
            goto done;
    }
    rat_swap(out, &acc);
    rc = 0;

done:
    rat_free(&lo);
    rat_free(&hi);
    rat_free(&idx);
    rat_free(&term);
    rat_free(&acc);
    return rc;
}

/* choose(n, k): ways to pick k items from n, = n! / (k! (n-k)!). */
static int choose(Rational *out, const Rational *nv, const Rational *kv, Err *e)
{
    int64_t n, k;
    if (need_int(nv, "n", &n, e) || need_int(kv, "k", &k, e))
        return -1;
    if (k < 0 || (n >= 0 && k > n)) {
        rat_set_i64(out, 0);
        return 0;
    }
    if (n >= 0 && k > n - k)
        k = n - k;   /* C(n, k) = C(n, n-k); fewer steps */
    if (k > MAX_TERMS)
        return fail(e, "k is too large (limit is %d)", MAX_TERMS);
    if (n < INT64_MIN + MAX_TERMS)
        return fail(e, "n is too large");

    /* Multiply in (n-k+i)/i one step at a time; each partial product is
     * itself a binomial coefficient, so the division is always exact. */
    BigInt acc, f;
    bi_init(&acc);
    bi_init(&f);
    bi_set_i64(&acc, 1);
    for (int64_t i = 1; i <= k; i++) {
        bi_set_i64(&f, n - k + i);
        bi_mul(&acc, &acc, &f);
        bi_set_i64(&f, i);
        bi_divmod(&acc, NULL, &acc, &f);
    }
    rat_set_bigint(out, &acc);
    bi_free(&acc);
    bi_free(&f);
    return check_size(out, e);
}

/* perm(n, k): ordered arrangements of k items from n, = n! / (n-k)!. */
static int perm(Rational *out, const Rational *nv, const Rational *kv, Err *e)
{
    int64_t n, k;
    if (need_int(nv, "n", &n, e) || need_int(kv, "k", &k, e))
        return -1;
    if (n < 0 || k < 0)
        return fail(e, "perm needs n >= 0 and k >= 0");
    if (k > n) {
        rat_set_i64(out, 0);
        return 0;
    }
    if (k > MAX_FACTORIAL)
        return fail(e, "k is too large (limit is %d)", MAX_FACTORIAL);
    BigInt acc, f;
    bi_init(&acc);
    bi_init(&f);
    bi_set_i64(&acc, 1);
    for (int64_t i = n - k + 1; i <= n; i++) {
        bi_set_i64(&f, i);
        bi_mul(&acc, &acc, &f);
    }
    rat_set_bigint(out, &acc);
    bi_free(&acc);
    bi_free(&f);
    return check_size(out, e);
}

static int call(const Node *n, Rational *args, Rational *out, Err *e)
{
    const char *name = n->name;
    size_t argc = n->nargs;
#define IS(s) (strcmp(name, (s)) == 0)
#define ARITY(k)                                                                   \
    do {                                                                           \
        if (argc != (k))                                                           \
            return fail(e, "%s takes %d argument%s", name, (k), (k) == 1 ? "" : "s"); \
    } while (0)

    if (IS("abs")) {
        ARITY(1);
        rat_copy(out, &args[0]);
        if (rat_sign(out) < 0)
            rat_neg(out);
        return 0;
    }
    if (IS("floor")) {
        ARITY(1);
        rat_floor(out, &args[0]);
        return 0;
    }
    if (IS("ceil")) {
        ARITY(1);
        rat_ceil(out, &args[0]);
        return 0;
    }
    if (IS("gcd") || IS("lcm")) {
        ARITY(2);
        if (!rat_is_int(&args[0]) || !rat_is_int(&args[1]))
            return fail(e, "%s needs whole numbers", name);
        BigInt g;
        bi_init(&g);
        bi_gcd(&g, &args[0].num, &args[1].num);
        if (IS("gcd")) {
            rat_set_bigint(out, &g);
        } else if (bi_is_zero(&g)) {
            rat_set_i64(out, 0);   /* lcm(0, 0) = 0 */
        } else {
            /* lcm(a, b) = |a / gcd * b| */
            BigInt l;
            bi_init(&l);
            bi_divmod(&l, NULL, &args[0].num, &g);
            bi_mul(&l, &l, &args[1].num);
            bi_abs(&l);
            rat_set_bigint(out, &l);
            bi_free(&l);
        }
        bi_free(&g);
        return 0;
    }
    if (IS("choose") || IS("nCr") || IS("binomial")) {
        ARITY(2);
        return choose(out, &args[0], &args[1], e);
    }
    if (IS("perm") || IS("nPr")) {
        ARITY(2);
        return perm(out, &args[0], &args[1], e);
    }
    return fail(e, "unknown function '%s'", name);
#undef IS
#undef ARITY
}

static int eval_node(const Node *n, const Binding *env, Rational *out, Err *e)
{
    switch (n->kind) {
    case NODE_NUM:
        rat_copy(out, &n->value);
        return 0;

    case NODE_VAR:
        for (const Binding *b = env; b; b = b->next) {
            if (strcmp(b->name, n->name) == 0) {
                rat_copy(out, b->value);
                return 0;
            }
        }
        return fail(e, "unknown variable '%s'", n->name);

    case NODE_NEG:
        if (eval_node(n->args[0], env, out, e))
            return -1;
        rat_neg(out);
        return 0;

    case NODE_FACT: {
        Rational v;
        rat_init(&v);
        int rc = eval_node(n->args[0], env, &v, e) || factorial(out, &v, e) ? -1 : 0;
        rat_free(&v);
        return rc;
    }

    case NODE_BINOP: {
        Rational a, b;
        rat_init(&a);
        rat_init(&b);
        int rc = eval_node(n->args[0], env, &a, e) || eval_node(n->args[1], env, &b, e) ||
                         binary(out, n->op, &a, &b, e)
                     ? -1
                     : 0;
        rat_free(&a);
        rat_free(&b);
        return rc;
    }

    case NODE_CALL: {
        Rational args[8];
        size_t argc = n->nargs;
        if (argc > sizeof args / sizeof *args)
            return fail(e, "too many arguments to %s", n->name);
        for (size_t i = 0; i < argc; i++)
            rat_init(&args[i]);
        int rc = 0;
        for (size_t i = 0; i < argc && rc == 0; i++)
            rc = eval_node(n->args[i], env, &args[i], e);
        if (rc == 0)
            rc = call(n, args, out, e);
        for (size_t i = 0; i < argc; i++)
            rat_free(&args[i]);
        return rc;
    }

    case NODE_SUM:
    case NODE_PROD:
        return big_op(n, env, out, e);
    }
    return fail(e, "internal error: unknown node");
}

int eval(const Node *n, const Binding *env, Rational *out, char *err, size_t errlen)
{
    Err e = {err, errlen};
    return eval_node(n, env, out, &e);
}
