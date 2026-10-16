# The phone version: CalcAll compiled to WebAssembly, for any phone's browser
# (and, wrapped in an app, for Android). The desktop app's own code is used
# unchanged; mobile/shim.c fits it to a phone.
#
#   make -C mobile web RAYLIB=/path/to/raylib    (needs Emscripten: emcc)
#
# RAYLIB is raylib's source folder, with src/libraylib.a built for the web:
#   make -C $RAYLIB/src PLATFORM=PLATFORM_WEB
# The result is in mobile/build/web: put that folder on any web server.

RAYLIB ?= ../../raylib
VERSION ?= dev
OUT = build/web

ENGINE = ../src/bigint.c ../src/rational.c ../src/approx.c ../src/cmath.c ../src/parser.c ../src/eval.c \
         ../src/symbols.c ../src/calc.c ../src/calculus.c ../src/feval.c ../src/graph.c ../src/mathedit.c

CFLAGS = -std=c11 -O2 -DPLATFORM_WEB -I$(RAYLIB)/src
# ASYNCIFY lets the app's own main loop hand control back to the browser.
LDFLAGS = -sUSE_GLFW=3 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sEXPORTED_RUNTIME_METHODS=FS \
          -lidbfs.js --preload-file ../assets/DejaVuSansMono.ttf@assets/DejaVuSansMono.ttf \
          --shell-file web/shell.html

web: $(OUT)/index.html

$(OUT)/index.html: $(ENGINE) ../src/gui.c ../src/*.h shim.c shim.h web/*
	@command -v emcc >/dev/null || { echo "emcc not found: install Emscripten (emsdk) first"; exit 1; }
	@test -f $(RAYLIB)/src/libraylib.a || { echo "no $(RAYLIB)/src/libraylib.a: build raylib with PLATFORM=PLATFORM_WEB"; exit 1; }
	mkdir -p $(OUT)
	emcc $(CFLAGS) -include shim.h -c ../src/gui.c -o $(OUT)/gui.o
	emcc $(CFLAGS) -c shim.c -o $(OUT)/shim.o
	emcc $(CFLAGS) $(ENGINE) $(OUT)/gui.o $(OUT)/shim.o $(RAYLIB)/src/libraylib.a $(LDFLAGS) -o $@
	rm $(OUT)/gui.o $(OUT)/shim.o
	cp web/manifest.webmanifest web/apple-touch-icon.png web/icon-maskable-512.png $(OUT)/
	cp ../assets/logo/icon-256.png ../assets/logo/icon-512.png $(OUT)/
	sed 's/__VERSION__/$(VERSION)/' web/sw.js > $(OUT)/sw.js

# Try it on this computer: http://localhost:8000
serve: web
	cd $(OUT) && python3 -m http.server 8000

clean:
	rm -rf build

.PHONY: web serve clean
