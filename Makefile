CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS = -lm

SRC = src/bigint.c src/rational.c src/approx.c src/parser.c src/eval.c src/calc.c src/main.c
HDR = $(wildcard src/*.h)

calc: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

test: calc
	sh tests/run_tests.sh ./calc

clean:
	rm -f calc

.PHONY: test clean
