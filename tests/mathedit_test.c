/* Tests for the template editor: edit, serialize, and evaluate the result. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/calc.h"
#include "../src/mathedit.h"

static int pass, fail;
static Calc calc;

/* Checks the serialized text and, if `value` is given, what it evaluates to. */
static void check(const char *name, Editor *e, const char *text, const char *value)
{
    int empty;
    char *s = ed_serialize(e->root, &empty);
    int ok = strcmp(s, text) == 0;
    char *out = NULL, err[256] = "";
    if (ok && value) {
        out = calc_eval(&calc, s, err, sizeof err);
        ok = out && strcmp(out, value) == 0;
    }
    if (ok) {
        pass++;
    } else {
        fail++;
        printf("FAIL: %s\n  text:  %s (expected %s)\n  value: %s (expected %s)\n", name, s, text,
               out ? out : err, value ? value : "-");
    }
    free(s);
    free(out);
}

static void expect_true(const char *name, int ok)
{
    if (ok) {
        pass++;
    } else {
        fail++;
        printf("FAIL: %s\n", name);
    }
}

static void check_empty(const char *name, Editor *e, int expected)
{
    int empty;
    free(ed_serialize(e->root, &empty));
    if (empty == expected) {
        pass++;
    } else {
        fail++;
        printf("FAIL: %s: empty box flag is %d\n", name, empty);
    }
}

int main(void)
{
    Editor e;
    calc_init(&calc);
    ed_init(&e);

    /* Typing "12/3" moves 12 into the numerator, like a Casio. */
    ed_insert_text(&e, "12");
    ed_insert_template(&e, IT_FRAC, 1);
    ed_insert_text(&e, "3");
    check("grab numerator", &e, "(12/3)", "4");

    /* Fraction button: fill the top, Down to the bottom, Right to leave. */
    ed_clear(&e);
    ed_insert_template(&e, IT_FRAC, 0);
    ed_insert_text(&e, "1");
    ed_vertical(&e, 1);
    ed_insert_text(&e, "3");
    ed_right(&e);
    ed_insert_text(&e, "+1");
    check("fraction navigation", &e, "(1/3)+1", "1.33333333333333333333...  (= 4/3)");

    /* Sum: k is filled in, the cursor starts in the "from" box. */
    ed_clear(&e);
    ed_insert_template(&e, IT_SUM, 0);
    ed_insert_text(&e, "1");
    ed_vertical(&e, -1);   /* up to the "to" box */
    ed_insert_text(&e, "10");
    ed_right(&e);          /* into the body */
    ed_insert_text(&e, "k");
    ed_insert_pow(&e, "2");
    check("sum", &e, "sum(k=1, 10, k^2)", "385");

    /* An empty box is reported. */
    ed_clear(&e);
    ed_insert_template(&e, IT_PROD, 0);
    ed_insert_text(&e, "1");
    check_empty("empty boxes", &e, 1);

    /* Backspace in an empty template removes it. */
    ed_clear(&e);
    ed_insert_text(&e, "2");
    ed_insert_template(&e, IT_SQRT, 0);
    ed_backspace(&e);
    check("backspace empty template", &e, "2", "2");

    /* Backspace after a filled template steps inside instead of deleting it. */
    ed_clear(&e);
    ed_insert_template(&e, IT_SQRT, 0);
    ed_insert_text(&e, "16");
    ed_right(&e);
    ed_backspace(&e);
    ed_backspace(&e);
    check("backspace steps in", &e, "sqrt(1)", "1");

    /* Powers, roots, logs, absolute value, implicit multiplication. */
    ed_clear(&e);
    ed_insert_text(&e, "2");
    ed_insert_pow(&e, NULL);
    ed_insert_text(&e, "10");
    ed_right(&e);
    ed_insert_text(&e, "+");
    ed_insert_template(&e, IT_ROOT, 0);
    ed_insert_text(&e, "3");
    ed_right(&e);
    ed_insert_text(&e, "27");
    ed_right(&e);
    ed_insert_text(&e, "+");
    ed_insert_template(&e, IT_LOGB, 0);
    ed_insert_text(&e, "2");
    ed_right(&e);
    ed_insert_text(&e, "8");
    ed_right(&e);
    ed_insert_text(&e, "+2");
    ed_insert_template(&e, IT_ABS, 0);
    ed_insert_text(&e, "-5");
    check("mixed templates", &e, "2^10+root(27, 3)+log(8, 2)+2abs(-5)", "1040");

    /* Left/Right walk through every box and back out. */
    ed_clear(&e);
    ed_insert_template(&e, IT_FRAC, 0);
    ed_insert_text(&e, "1");
    ed_right(&e);
    ed_insert_text(&e, "2");
    ed_right(&e);
    ed_left(&e);
    ed_left(&e);
    ed_left(&e);   /* now at the end of the numerator */
    ed_insert_text(&e, "0");
    check("left/right", &e, "(10/2)", "5");

    /* Copies are independent and serialize the same. */
    Row *copy = row_clone(e.root);
    ed_clear(&e);
    ed_load(&e, copy);
    row_free(copy);
    check("clone and load", &e, "(10/2)", "5");

    /* Text mode: typing the CLI syntax builds templates. */
    struct {
        const char *typed, *text, *value;
    } typed[] = {
        {"sum(k=1, 10, k^2)", "sum(k=1, 10, k^2)", "385"},
        {"sum(k,1,10,k)", "sum(k=1, 10, k)", "55"},   /* the CLI's other form */
        {"1/2+3", "(1/2)+3", "3.5"},
        {"1/2^3*4", "(1/(2^3))*4", "0.5"},
        {"2^10-1", "2^10-1", "1023"},
        {"2^-1", "2^(-1)", "0.5"},
        {"1/2/4", "(((1/2))/4)", "0.125"},
        {"prod(n=1,5,n)+1", "prod(n=1, 5, n)+1", "121"},
        {"sqrt(16)+cbrt(27)", "sqrt(16)+root(27, 3)", "7"},
        {"root(32,5)", "root(32, 5)", "2"},
        {"abs(-3)*2", "abs(-3)*2", "6"},
        {"1/(2+3)+1", "(1/(2+3))+1", "1.2"},
        {"(1+2)/3", "(((1+2))/3)", "1"},
        {"2^(3+1)", "2^(3+1)", "16"},
        {"sum(k=1,3,gcd(k,6))", "sum(k=1, 3, gcd(k,6))", "6"},
        {"exp(0)+pi*0", "e^0+\xCF\x80*0", "1"},
        {"sumx", "sum(x=, , )", NULL},   /* x goes in the variable box */
        {"1/3+1/6=1/2", "(1/3)+(1/6)=(1/2)", "True"},   /* true or false checks */
        {"1/2<=1", "(1/2)\xE2\x89\xA4" "1", "True"},
        {"3>=4", "3\xE2\x89\xA5" "4", "False"},
        {"2^3!=6", "2^3\xE2\x89\xA0" "6", "True"},   /* "!=" isn't a factorial in the exponent */
        {"d/dx(x^3,2)", "d/dx(x^3, 2)", "12"},   /* the fraction d/dx turns into a derivative */
        {"3d/dx(x^2,1)", "3 d/dx(x^2, 1)", "6"},
        {"d/dt(t^2)", "d/dt(t^2)", NULL},          /* no point: the slope at t */
        {"int(x=0,1,x^2)", "int(x=0, 1, x^2)", "0.33333333333333333333...  (= 1/3)"},
        {"int(t=1,2,2t)+1", "int(t=1, 2, 2t)+1", "4"},
        {"1/dx(2)", "(1/(dx(2)))", NULL},          /* not a d on top: an ordinary fraction */
    };
    for (size_t i = 0; i < sizeof typed / sizeof *typed; i++) {
        ed_clear(&e);
        for (const char *p = typed[i].typed; *p; p++) {
            char ch[2] = {*p, '\0'};
            if (*p != ' ')
                ed_type(&e, ch);
        }
        check(typed[i].typed, &e, typed[i].text, typed[i].value);
    }

    /* "sumx" is not a sum when the letters are part of a longer word. */
    ed_clear(&e);
    for (const char *p = "xsum"; *p; p++) {
        char ch[2] = {*p, '\0'};
        ed_type(&e, ch);
    }
    check("word boundary", &e, "xsum", NULL);

    /* Rebuilding from saved text: serialize, rebuild, serialize again is stable. */
    const char *texts[] = {"(1/3)+(1/6)", "((1/3))+((1/6))", "sqrt(2)", "sum(k=1, 10, k^2)", "(1/(2+3))+1",
                           "((1/2))^2", "2^10-1", "d/dx(x^3, 2)", "2 d/dx(sin(x))", "int(x=0, 1, x^2)"};
    const char *want[] = {"(1/3)+(1/6)", "(1/3)+(1/6)", "sqrt(2)", "sum(k=1, 10, k^2)", "(1/(2+3))+1",
                          "(1/2)^2", "2^10-1", "d/dx(x^3, 2)", "2 d/dx(sin(x))", "int(x=0, 1, x^2)"};
    for (size_t i = 0; i < sizeof texts / sizeof *texts; i++) {
        ed_from_text(&e, texts[i]);
        check(texts[i], &e, want[i], NULL);
    }

    /* d/dx then "(" after leaving the fraction, or with keys (no text rules) */
    ed_clear(&e);
    for (const char *p = "d/dx"; *p; p++) {
        char ch[2] = {*p, '\0'};
        ed_type(&e, ch);
    }
    ed_right(&e);   /* out of the fraction */
    for (const char *p = "(x^2,3)"; *p; p++) {
        char ch[2] = {*p, '\0'};
        ed_type(&e, ch);
    }
    check("d/dx then right", &e, "d/dx(x^2, 3)", "6");
    ed_clear(&e);
    ed_insert_template(&e, IT_FRAC, 0);
    ed_insert_text(&e, "d");
    ed_tab(&e, 1);
    ed_insert_text(&e, "dt");
    expect_true("keys d/dt(", ed_derivative(&e));
    ed_insert_text(&e, "t");
    ed_insert_pow(&e, "3");
    ed_tab(&e, 1);
    ed_insert_text(&e, "2");
    check("keys d/dt", &e, "d/dt(t^3, 2)", "12");

    /* ÷ divides without a fraction, and still ends an exponent; d÷dx( is a derivative */
    const char *divide[][3] = {
        {"2^3", "/4+1", "2^3/4+1"},
        {"d", "/dx(x^2,3)", "d/dx(x^2, 3)"},
        {"1", "/2", "1/2"},
    };
    const char *divide_value[] = {"3", "6", "0.5"};
    for (int i = 0; i < 3; i++) {
        ed_clear(&e);
        for (int k = 0; k < 2; k++)
            for (const char *p = divide[i][k]; *p; p++) {
                char ch[2] = {*p, '\0'};
                ed_type(&e, *p == '/' ? "\xC3\xB7" : ch);
            }
        check(divide[i][2], &e, divide[i][2], divide_value[i]);
    }

    /* Tab walks through the boxes and out. */
    ed_clear(&e);
    ed_insert_template(&e, IT_FRAC, 0);
    ed_insert_text(&e, "3");
    ed_tab(&e, 1);
    ed_insert_text(&e, "4");
    ed_tab(&e, 1);
    ed_insert_text(&e, "+1");
    ed_home(&e);
    ed_tab(&e, 1);   /* from the start, Tab enters the fraction */
    ed_insert_text(&e, "0");
    check("tab", &e, "(30/4)+1", "8.5");

    ed_free(&e);
    calc_free(&calc);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
