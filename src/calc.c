#include "calc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "parser.h"

#define DEFAULT_DIGITS 20
#define MAX_DIGITS 1000
#define GUARD_DIGITS 10   /* approximate values keep this many digits beyond those shown */

static const char HELP[] =
    "Operators:  + - * / % ^ !  and ( )\n"
    "Numbers:    42  3.14  .5  6.02e23    (exact: 0.1 + 0.2 = 0.3)\n"
    "Constants:  pi (or \xCF\x80)  e\n"
    "Sum:        sum(k=1, 100, k^2)        also written \xCE\xA3(k=1, 100, k^2)\n"
    "Product:    prod(k=1, 10, k)          also written \xCE\xA0(k=1, 10, k)\n"
    "Roots:      sqrt(x)  cbrt(x)  root(x, n)  x^(1/n)\n"
    "Logs:       ln(x)  log(x) (base 10)  log2(x)  log(x, base)  exp(x)\n"
    "Trig:       sin cos tan asin acos atan sinh cosh tanh\n"
    "Complex:    i  3+4i  re im conj arg abs  (sqrt(-4) = 2i)\n"
    "Other:      abs floor ceil round gcd lcm choose(n,k) perm(n,k)\n"
    "Variables:  ans or Ans  (the previous result)\n"
    "Implicit:   2pi  3(4+1)  2sin(30)  mean 2*pi, 3*(4+1), 2*sin(30)\n"
    "Commands:   :deg  :rad  :sci  :fix  :digits N  :help  :quit\n"
    "\xE2\x89\x88 marks an approximate result; everything else is exact.";

static char *dup_str(const char *s)
{
    char *d = malloc(strlen(s) + 1);
    if (d)
        strcpy(d, s);
    return d;
}

void calc_init(Calc *c)
{
    c->digits = DEFAULT_DIGITS;
    c->sci = 0;
    c->degrees = 0;
    val_init(&c->ans);
}

void calc_free(Calc *c)
{
    val_free(&c->ans);
}

static char *command(Calc *c, const char *cmd, char *err, size_t errlen)
{
    char word[32], extra[2];
    int n;
    char msg[96];

    if (sscanf(cmd, "%31s", word) != 1)
        word[0] = '\0';

    if (strcmp(word, "help") == 0)
        return dup_str(HELP);
    if (strcmp(word, "sci") == 0) {
        c->sci = 1;
        snprintf(msg, sizeof msg, "scientific notation on (%d significant digits)", c->digits);
        return dup_str(msg);
    }
    if (strcmp(word, "fix") == 0) {
        c->sci = 0;
        return dup_str("scientific notation off (whole numbers show every digit)");
    }
    if (strcmp(word, "deg") == 0) {
        c->degrees = 1;
        return dup_str("angles in degrees");
    }
    if (strcmp(word, "rad") == 0) {
        c->degrees = 0;
        return dup_str("angles in radians");
    }
    if (strcmp(word, "digits") == 0) {
        if (sscanf(cmd, "%31s %d %1s", word, &n, extra) != 2 || n < 1 || n > MAX_DIGITS) {
            snprintf(err, errlen, "usage: :digits N  (N from 1 to %d)", MAX_DIGITS);
            return NULL;
        }
        c->digits = n;
        snprintf(msg, sizeof msg, "showing %d digits", n);
        return dup_str(msg);
    }
    snprintf(err, errlen, "unknown command ':%s' (try :help)", word);
    return NULL;
}

char *calc_eval(Calc *c, const char *line, char *err, size_t errlen)
{
    while (isspace((unsigned char)*line))
        line++;
    if (*line == '\0')
        return dup_str("");
    if (*line == ':')
        return command(c, line + 1, err, errlen);

    Node *tree = parse(line, err, errlen);
    if (!tree)
        return NULL;

    Binding ans_lower = {"ans", &c->ans, NULL};
    Binding ans = {"Ans", &c->ans, &ans_lower};
    EvalOptions opt = {c->digits + GUARD_DIGITS, c->degrees};
    Value v;
    val_init(&v);
    int rc = eval(tree, &ans, &opt, &v, err, errlen);
    node_free(tree);
    if (rc) {
        val_free(&v);
        return NULL;
    }
    val_swap(&c->ans, &v);
    val_free(&v);
    return val_to_str(&c->ans, c->digits, c->sci);
}
