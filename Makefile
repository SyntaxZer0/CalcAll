CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS = -lm

ENGINE = src/bigint.c src/rational.c src/approx.c src/parser.c src/eval.c src/calc.c
HDR = $(wildcard src/*.h)

calc: $(ENGINE) src/main.c $(HDR)
	$(CC) $(CFLAGS) -o $@ $(ENGINE) src/main.c $(LDLIBS)

# The GUI needs raylib. macOS: brew install raylib pkg-config
# Elsewhere, point these at your raylib build if pkg-config can't find it:
#   make gui RAYLIB_CFLAGS=-I/path/raylib/src RAYLIB_LIBS=/path/raylib/src/libraylib.a
RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS ?= $(shell pkg-config --libs raylib 2>/dev/null || echo -lraylib)
ifeq ($(shell uname -s),Darwin)
GUI_SYSLIBS = -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo
else
GUI_SYSLIBS = -lGL -lpthread -ldl -lrt -lX11
endif

gui: calc-gui

calc-gui: $(ENGINE) src/gui.c $(HDR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $(ENGINE) src/gui.c $(RAYLIB_LIBS) $(GUI_SYSLIBS) $(LDLIBS)

test: calc
	sh tests/run_tests.sh ./calc

clean:
	rm -f calc calc-gui

.PHONY: gui test clean
