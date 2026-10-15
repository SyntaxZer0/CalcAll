CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS = -lm

ENGINE = src/bigint.c src/rational.c src/approx.c src/cmath.c src/parser.c src/eval.c src/symbols.c src/calc.c \
         src/calculus.c src/feval.c src/graph.c
HDR = $(wildcard src/*.h)

calc: $(ENGINE) src/main.c $(HDR)
	$(CC) $(CFLAGS) -o $@ $(ENGINE) src/main.c $(LDLIBS)

# The GUI needs raylib. macOS: brew install raylib
# raylib is found with pkg-config, or in Homebrew's folders on macOS. If it's
# somewhere else, say where:
#   make gui RAYLIB_CFLAGS=-I/path/raylib/src RAYLIB_LIBS=/path/raylib/src/libraylib.a
ifeq ($(shell uname -s),Darwin)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
GUI_SYSLIBS = -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo
else
GUI_SYSLIBS = -lGL -lpthread -ldl -lrt -lX11
endif
RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib 2>/dev/null || \
                   echo $(if $(BREW_PREFIX),-I$(BREW_PREFIX)/include))
RAYLIB_LIBS ?= $(shell pkg-config --libs raylib 2>/dev/null || \
                 echo $(if $(BREW_PREFIX),-L$(BREW_PREFIX)/lib) -lraylib)

gui: calc-gui

calc-gui: $(ENGINE) src/mathedit.c src/gui.c $(HDR)
	@printf '#include "raylib.h"\n' | $(CC) $(RAYLIB_CFLAGS) -E -x c - >/dev/null 2>&1 || { \
	    echo "raylib not found. Install it (macOS: brew install raylib), or see the"; \
	    echo "comment above RAYLIB_CFLAGS in the Makefile to point at your copy."; \
	    exit 1; }
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $(ENGINE) src/mathedit.c src/gui.c $(RAYLIB_LIBS) $(GUI_SYSLIBS) $(LDLIBS)

test: calc tests/mathedit_test tests/graph_test
	sh tests/run_tests.sh ./calc
	./tests/mathedit_test
	CALC_DEFINITIONS= ./tests/graph_test

tests/graph_test: tests/graph_test.c $(ENGINE) $(HDR)
	$(CC) $(CFLAGS) -o $@ tests/graph_test.c $(ENGINE) $(LDLIBS)

tests/mathedit_test: tests/mathedit_test.c $(ENGINE) src/mathedit.c $(HDR)
	$(CC) $(CFLAGS) -o $@ tests/mathedit_test.c $(ENGINE) src/mathedit.c $(LDLIBS)

# macOS: CalcAll.app, with its icon in the Dock (open CalcAll.app, or drag it to Applications)
app: calc-gui
	rm -rf CalcAll.app
	mkdir -p CalcAll.app/Contents/MacOS CalcAll.app/Contents/Resources
	cp calc-gui CalcAll.app/Contents/MacOS/
	cp -R assets CalcAll.app/Contents/MacOS/
	cp assets/logo/CalcAll.icns CalcAll.app/Contents/Resources/
	cp assets/logo/Info.plist CalcAll.app/Contents/

clean:
	rm -rf calc calc-gui tests/mathedit_test tests/graph_test CalcAll.app

.PHONY: gui app test clean
