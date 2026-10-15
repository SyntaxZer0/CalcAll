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
    check("grab numerator", &e, "((12)/(3))", "4");

    /* Fraction button: fill the top, Down to the bottom, Right to leave. */
    ed_clear(&e);
    ed_insert_template(&e, IT_FRAC, 0);
    ed_insert_text(&e, "1");
    ed_vertical(&e, 1);
    ed_insert_text(&e, "3");
    ed_right(&e);
    ed_insert_text(&e, "+1");
    check("fraction navigation", &e, "((1)/(3))+1", "1.33333333333333333333...  (= 4/3)");

    /* Sum: k is filled in, the cursor starts in the "from" box. */
    ed_clear(&e);
    ed_insert_template(&e, IT_SUM, 0);
    ed_insert_text(&e, "1");
    ed_vertical(&e, -1);   /* up to the "to" box */
    ed_insert_text(&e, "10");
    ed_right(&e);          /* into the body */
    ed_insert_text(&e, "k");
    ed_insert_pow(&e, "2");
    check("sum", &e, "sum(k=1, 10, k^(2))", "385");

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
    check("mixed templates", &e, "2^(10)+root(27, 3)+log(8, 2)+2abs(-5)", "1040");

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
    check("left/right", &e, "((10)/(2))", "5");

    /* Copies are independent and serialize the same. */
    Row *copy = row_clone(e.root);
    ed_clear(&e);
    ed_load(&e, copy);
    row_free(copy);
    check("clone and load", &e, "((10)/(2))", "5");

    ed_free(&e);
    calc_free(&calc);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
