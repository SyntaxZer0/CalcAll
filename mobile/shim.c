/*
 * Phone support for the web build: see shim.h. This file is compiled
 * without shim.h, so the raylib names here are raylib's own functions.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten.h>

#include "raylib.h"
#include "rlgl.h"

#define LOGICAL_W 480.0f   /* the app is laid out for a window this wide... */
#define LOGICAL_H 800.0f   /* ...and at least this tall */

static float scale = 1;        /* real pixels per layout pixel */
static int pixel_w, pixel_h;   /* the canvas in real pixels */
static double frame_start, last_save;
static int frames;

/* ---- The page, in JavaScript ---------------------------------------------- */

/* The canvas's size on the page, in real pixels. */
EM_JS(int, js_canvas_w, (void), {
    var c = Module.canvas;
    return Math.round(c.getBoundingClientRect().width * (window.devicePixelRatio || 1));
});
EM_JS(int, js_canvas_h, (void), {
    var c = Module.canvas;
    return Math.round(c.getBoundingClientRect().height * (window.devicePixelRatio || 1));
});

/*
 * Files (saved definitions, history, settings) live in the home folder;
 * in a browser that's backed by IndexedDB, so it's still there next time.
 */
EM_JS(void, js_mount_storage, (void), {
    Module.calcallLoaded = 0;
    try {
        FS.mount(IDBFS, {}, '/home/web_user');
        FS.syncfs(true, function () { Module.calcallLoaded = 1; });
    } catch (e) {
        Module.calcallLoaded = 1;   /* no storage (private browsing): carry on without */
    }
});
EM_JS(int, js_storage_loaded, (void), { return Module.calcallLoaded; });
EM_JS(void, js_save_storage, (void), {
    if (Module.calcallSaving)
        return;
    Module.calcallSaving = 1;
    FS.syncfs(false, function () { Module.calcallSaving = 0; });
});

/*
 * Characters and keys typed through the phone's keyboard (see shell.html),
 * in the order they were typed: a character is its code, a key minus its
 * raylib code. Each call takes the next one only if it's the right kind, so
 * "1+2", Enter, "3" isn't read as "1+23" and then Enter: the app takes all
 * characters before any keys in a frame, so what follows a key waits for the
 * next frame.
 */
EM_JS(int, js_take_input, (int want_key), {
    var q = Module.calcallInput;
    if (!q || !q.length || (q[0] < 0) != !!want_key)
        return 0;
    var v = q.shift();
    return v < 0 ? -v : v;
});

/* ---- Window and drawing ---------------------------------------------------- */

/* Fit the layout to the canvas: about 480 wide on a phone held upright,
 * wider when turned sideways or on a computer. */
static void fit(void)
{
    int w = js_canvas_w(), h = js_canvas_h();
    if (w < 64 || h < 64)
        return;
    if (w != pixel_w || h != pixel_h) {
        pixel_w = w;
        pixel_h = h;
        SetWindowSize(w, h);
    }
    scale = fminf((float)w / LOGICAL_W, (float)h / LOGICAL_H);
    if (scale <= 0)
        scale = 1;
    SetMouseScale(1 / scale, 1 / scale);
}

void mob_SetConfigFlags(unsigned int flags)
{
    /* The page decides the size, and the shim does the high-DPI scaling. */
    SetConfigFlags(flags & ~(unsigned)(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE));
}

void mob_InitWindow(int width, int height, const char *title)
{
    (void)width;
    (void)height;
    js_mount_storage();
    while (!js_storage_loaded())
        emscripten_sleep(10);

    /* A phone has no keyboard until you ask for one, so start with the keys. */
    const char *home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof path, "%s/.calcall_settings", home ? home : "/home/web_user");
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
    } else if ((f = fopen(path, "w"))) {
        fputs("text 0\n", f);
        fclose(f);
    }

    pixel_w = js_canvas_w();
    pixel_h = js_canvas_h();
    InitWindow(pixel_w > 0 ? pixel_w : 480, pixel_h > 0 ? pixel_h : 800, title);
    fit();
    frame_start = GetTime();
}

void mob_SetTargetFPS(int fps)
{
    (void)fps;   /* pacing is done in mob_EndDrawing, which lets the browser run */
}

void mob_BeginDrawing(void)
{
    fit();
    BeginDrawing();
    rlPushMatrix();
    rlScalef(scale, scale, 1);
}

void mob_EndDrawing(void)
{
    rlPopMatrix();
    EndDrawing();
    frames++;
    if (GetTime() - last_save > 2) {   /* files changed by then are kept */
        js_save_storage();
        last_save = GetTime();
    }

    /* Hand control back to the browser until the next frame is due. */
    double left = 1.0 / 60 - (GetTime() - frame_start);
    emscripten_sleep(left > 0 ? (unsigned)(left * 1000) : 0);
    frame_start = GetTime();
}

int mob_GetScreenWidth(void)
{
    return (int)((float)pixel_w / scale);
}

int mob_GetScreenHeight(void)
{
    return (int)((float)pixel_h / scale);
}

void mob_SetWindowSize(int width, int height)
{
    (void)width;   /* the page decides the size */
    (void)height;
}

void mob_SetWindowPosition(int x, int y)
{
    (void)x;
    (void)y;
}

void mob_SetWindowMinSize(int width, int height)
{
    (void)width;
    (void)height;
}

void mob_BeginScissorMode(int x, int y, int width, int height)
{
    int x0 = (int)floorf((float)x * scale), y0 = (int)floorf((float)y * scale);
    int x1 = (int)ceilf((float)(x + width) * scale), y1 = (int)ceilf((float)(y + height) * scale);
    BeginScissorMode(x0, y0, x1 - x0, y1 - y0);
}

/* ---- Touch ------------------------------------------------------------------ */

/*
 * The app zooms the graph and scrolls its lists with the mouse wheel. On a
 * phone, two fingers do both: pinching zooms, and moving them together up
 * or down scrolls. Worked out once per frame, as raylib's wheel is.
 */
float mob_GetMouseWheelMove(void)
{
    static int last_frame = -1;
    static float wheel;
    static float last_dist, last_y;
    if (last_frame == frames)
        return wheel;
    last_frame = frames;
    wheel = GetMouseWheelMove();
    if (GetTouchPointCount() == 2) {
        Vector2 a = GetTouchPosition(0), b = GetTouchPosition(1);
        float dist = hypotf(a.x - b.x, a.y - b.y), y = (a.y + b.y) / 2;
        if (last_dist > 0) {
            float zoom = logf(dist / last_dist) / logf(1.15f);   /* the app zooms 1.15x per notch */
            float scroll = (y - last_y) / (40 * scale);          /* and scrolls 40 pixels per notch */
            wheel += fabsf(zoom) > fabsf(scroll) ? zoom : scroll;
        }
        last_dist = dist;
        last_y = y;
    } else {
        last_dist = 0;
    }
    return wheel;
}

/* ---- Typing ------------------------------------------------------------------ */

int mob_GetCharPressed(void)
{
    int c = js_take_input(0);
    return c ? c : GetCharPressed();
}

int mob_GetKeyPressed(void)
{
    int k = js_take_input(1);
    return k ? k : GetKeyPressed();
}
