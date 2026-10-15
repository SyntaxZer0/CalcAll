/*
 * Terminal front end.
 *
 *   ./calc                     interactive prompt
 *   ./calc 'prod(k=1, 10, k)'  evaluate once and exit
 *   echo '2^100' | ./calc      evaluate each line from a pipe
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calc.h"

/* Reads one line of any length without the newline. Returns NULL at EOF. */
static char *read_line(FILE *f)
{
    size_t len = 0, cap = 128;
    char *s = malloc(cap);
    int ch;
    if (!s)
        return NULL;
    while ((ch = fgetc(f)) != EOF && ch != '\n') {
        if (len + 1 >= cap) {
            char *p = realloc(s, cap *= 2);
            if (!p) {
                free(s);
                return NULL;
            }
            s = p;
        }
        s[len++] = (char)ch;
    }
    if (ch == EOF && len == 0) {
        free(s);
        return NULL;
    }
    if (len > 0 && s[len - 1] == '\r')
        len--;
    s[len] = '\0';
    return s;
}

static int is_quit(const char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    return strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0 ||
           strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0;
}

static int run_once(Calc *c, int argc, char **argv)
{
    /* Join the arguments so `./calc 2 + 3` works without quotes. */
    size_t len = 1;
    for (int i = 1; i < argc; i++)
        len += strlen(argv[i]) + 1;
    char *expr = malloc(len);
    if (!expr)
        return 1;
    expr[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            strcat(expr, " ");
        strcat(expr, argv[i]);
    }

    char err[256];
    char *out = calc_eval(c, expr, err, sizeof err);
    free(expr);
    if (!out) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    puts(out);
    free(out);
    return 0;
}

static int run_repl(Calc *c)
{
    int interactive = isatty(STDIN_FILENO);
    int status = 0;
    char *line;

    if (interactive)
        puts("calc - type :help for help, :quit to exit");
    for (;;) {
        if (interactive) {
            fputs("> ", stdout);
            fflush(stdout);
        }
        if (!(line = read_line(stdin)))
            break;
        if (is_quit(line)) {
            free(line);
            break;
        }
        char err[256];
        char *out = calc_eval(c, line, err, sizeof err);
        free(line);
        if (!out) {
            printf("error: %s\n", err);
            status = 1;
        } else {
            if (*out)
                puts(out);
            free(out);
        }
    }
    return interactive ? 0 : status;
}

int main(int argc, char **argv)
{
    Calc c;
    calc_init(&c);
    calc_use_store(&c, calc_default_store());   /* bring back saved x = ..., f(x) = ... */
    calc_use_history(&c, calc_default_history());
    int status = argc > 1 ? run_once(&c, argc, argv) : run_repl(&c);
    calc_free(&c);
    return status;
}
