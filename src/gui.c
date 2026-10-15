/*
 * Graphical front end, drawn with raylib (https://www.raylib.com).
 *
 * Like main.c, it only uses the engine through calc.h: every button press
 * edits a line of text, and "=" hands that line to calc_eval().
 *
 * Keyboard: type expressions directly, Enter evaluates, Up/Down recall
 * earlier input, Esc clears, Cmd/Ctrl+C copies the last result, Cmd/Ctrl+V
 * pastes. Clicking an earlier line puts it back in the input for editing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "calc.h"

#define MAX_INPUT 4096
#define MAX_HISTORY 200
#define MAX_SHOWN_BYTES 20000   /* longer results are cut off on screen (copy gets all) */
#define FONT_FILE "DejaVuSansMono.ttf"

/* ---- Colors and sizes ----------------------------------------------------- */

#define COL_BG       (Color){24, 26, 31, 255}
#define COL_PANEL    (Color){33, 36, 43, 255}
#define COL_TEXT     (Color){232, 234, 240, 255}
#define COL_MUTED    (Color){138, 144, 158, 255}
#define COL_ERROR    (Color){240, 113, 113, 255}
#define COL_CURSOR   (Color){255, 170, 70, 255}
#define COL_NUM      (Color){58, 62, 72, 255}
#define COL_OP       (Color){74, 80, 96, 255}
#define COL_FN       (Color){45, 49, 58, 255}
#define COL_CTRL     (Color){110, 62, 66, 255}
#define COL_ACCENT   (Color){230, 140, 40, 255}
#define COL_TOGGLE   (Color){52, 86, 130, 255}

#define PAD 12.0f
#define GAP 8.0f
#define MODEBAR_H 34.0f
#define EXPR_SIZE 18.0f
#define RESULT_SIZE 24.0f
#define INPUT_SIZE 30.0f

/* ---- Buttons -------------------------------------------------------------- */

enum { ACT_INSERT, ACT_EVAL, ACT_CLEAR, ACT_BACKSPACE };
enum { K_NUM, K_OP, K_FN, K_CTRL, K_ACCENT };

typedef struct {
    const char *label;
    const char *insert;   /* text to insert for ACT_INSERT */
    int action;
    int kind;             /* K_* color group */
    int span;             /* how many columns it covers */
} Button;

#define FN(l, t) {l, t, ACT_INSERT, K_FN, 1}
#define OP(l, t) {l, t, ACT_INSERT, K_OP, 1}
#define NUM(l) {l, l, ACT_INSERT, K_NUM, 1}

#define COLS 6
static const Button BUTTONS[] = {
    FN("sin", "sin("), FN("cos", "cos("), FN("tan", "tan("),
    FN("ln", "ln("), FN("log", "log("), FN("\xE2\x88\x9A", "sqrt("),               /* √ */

    FN("asin", "asin("), FN("acos", "acos("), FN("atan", "atan("),
    FN("exp", "exp("), OP("x\xCA\xB8", "^"), OP("n!", "!"),                        /* xʸ */

    FN("\xCE\xA3", "\xCE\xA3(k=1, "), FN("\xCE\xA0", "\xCE\xA0(k=1, "),            /* Σ Π */
    FN("\xCF\x80", "\xCF\x80"), FN("e", "e"), OP("(", "("), OP(")", ")"),          /* π */

    NUM("7"), NUM("8"), NUM("9"), OP("\xC3\xB7", "/"),                             /* ÷ */
    {"\xE2\x8C\xAB", NULL, ACT_BACKSPACE, K_CTRL, 1},                            /* ⌫ */
    {"AC", NULL, ACT_CLEAR, K_CTRL, 1},

    NUM("4"), NUM("5"), NUM("6"), OP("\xC3\x97", "*"), FN("k", "k"), OP(",", ", "), /* × */

    NUM("1"), NUM("2"), NUM("3"), OP("\xE2\x88\x92", "-"),                         /* − */
    FN("ans", "ans"), OP("mod", "%"),

    {"0", "0", ACT_INSERT, K_NUM, 2}, NUM("."), OP("+", "+"),
    {"=", NULL, ACT_EVAL, K_ACCENT, 2},
};
static Color kind_color(int kind)
{
    switch (kind) {
    case K_NUM:
        return COL_NUM;
    case K_OP:
        return COL_OP;
    case K_CTRL:
        return COL_CTRL;
    case K_ACCENT:
        return COL_ACCENT;
    default:
        return COL_FN;
    }
}

#define NBUTTONS (sizeof BUTTONS / sizeof *BUTTONS)
#define ROWS 7

/* ---- State ---------------------------------------------------------------- */

typedef struct {
    char *expr;
    char *result;   /* text as shown, e.g. "= 42" or "error: ..." */
    char *copy;     /* the bare result for copying */
    int error;
    float wrap_width;    /* width the line counts below were measured at */
    int expr_lines, result_lines;
    float top, height;   /* position inside the history */
} Entry;

static Font font;
static Calc calc;
static char input[MAX_INPUT];
static size_t input_len, cursor;
static Entry history[MAX_HISTORY];
static int nhistory;
static int recall = -1;        /* index into history while using Up/Down */
static float scroll;           /* pixels scrolled up from the bottom of the history */

/* ---- Text helpers ---------------------------------------------------------- */

static void draw_text(const char *s, float x, float y, float size, Color c)
{
    DrawTextEx(font, s, (Vector2){x, y}, size, 0, c);
}

static float text_width(const char *s, float size)
{
    return MeasureTextEx(font, s, size, 0).x;
}

static float glyph_advance(int cp, float size)
{
    GlyphInfo g = GetGlyphInfo(font, cp);
    float adv = g.advanceX ? (float)g.advanceX : GetGlyphAtlasRec(font, cp).width;
    return adv * size / (float)font.baseSize;
}

/*
 * Wraps s to `width`, drawing it at (x, y) when `draw` is set, and returns the
 * number of lines. Breaks after a space when there is one, otherwise anywhere
 * (long numbers have no spaces).
 */
static int wrap(const char *s, float size, float width, float x, float y, Color c, int draw)
{
    int lines = 0;
    const char *line = s;
    while (*line) {
        float w = 0, space_w = 0;
        const char *p = line, *last_space = NULL, *end = NULL;
        while (*p) {
            int n;
            int cp = GetCodepointNext(p, &n);
            float a = glyph_advance(cp, size);
            if (w + a > width && p > line) {
                /* Break at the last space unless that leaves a stub like "= ". */
                end = last_space && space_w > width / 3 ? last_space + 1 : p;
                break;
            }
            if (cp == ' ') {
                last_space = p;
                space_w = w;
            }
            w += a;
            p += n;
        }
        if (!end)
            end = p;
        if (draw) {
            char buf[1024];
            size_t len = (size_t)(end - line);
            if (len >= sizeof buf)
                len = sizeof buf - 1;
            memcpy(buf, line, len);
            buf[len] = '\0';
            draw_text(buf, x, y + (float)lines * size * 1.15f, size, c);
        }
        lines++;
        line = end;
    }
    return lines ? lines : 1;
}

static char *dup(const char *s)
{
    char *d = malloc(strlen(s) + 1);
    if (!d) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    return strcpy(d, s);
}

/* Copy of s for the screen; huge results (like 50000!) are cut short. */
static char *for_display(const char *s)
{
    size_t len = strlen(s);
    if (len <= MAX_SHOWN_BYTES)
        return dup(s);
    char *cut = malloc(MAX_SHOWN_BYTES + 80);
    if (!cut)
        exit(1);
    snprintf(cut, MAX_SHOWN_BYTES + 80, "%.*s\xE2\x80\xA6 (%zu more; Copy gets all)",
             MAX_SHOWN_BYTES, s, len - MAX_SHOWN_BYTES);
    return cut;
}

/* ---- Input editing ---------------------------------------------------------- */

static void input_insert(const char *s)
{
    size_t n = strlen(s);
    if (input_len + n >= MAX_INPUT)
        return;
    memmove(input + cursor + n, input + cursor, input_len - cursor + 1);
    memcpy(input + cursor, s, n);
    input_len += n;
    cursor += n;
    recall = -1;
}

static void input_set(const char *s)
{
    snprintf(input, sizeof input, "%s", s);
    input_len = cursor = strlen(input);
}

/* Moves over one whole UTF-8 character. */
static size_t prev_char(size_t pos)
{
    if (pos == 0)
        return 0;
    do
        pos--;
    while (pos > 0 && (input[pos] & 0xC0) == 0x80);
    return pos;
}

static size_t next_char(size_t pos)
{
    if (pos >= input_len)
        return input_len;
    do
        pos++;
    while (pos < input_len && (input[pos] & 0xC0) == 0x80);
    return pos;
}

static void input_backspace(void)
{
    size_t start = prev_char(cursor);
    memmove(input + start, input + cursor, input_len - cursor + 1);
    input_len -= cursor - start;
    cursor = start;
}

static void input_delete(void)
{
    size_t end = next_char(cursor);
    memmove(input + cursor, input + end, input_len - end + 1);
    input_len -= end - cursor;
}

/* ---- History ------------------------------------------------------------------ */

static void add_entry(const char *expr, const char *shown, const char *copy, int error)
{
    if (nhistory == MAX_HISTORY) {
        free(history[0].expr);
        free(history[0].result);
        free(history[0].copy);
        memmove(history, history + 1, (MAX_HISTORY - 1) * sizeof *history);
        nhistory--;
    }
    Entry *e = &history[nhistory++];
    e->expr = dup(expr);
    e->result = for_display(shown);
    e->copy = dup(copy);
    e->error = error;
    e->wrap_width = 0;
}

static void evaluate(void)
{
    if (input_len == 0)
        return;

    /* Starting with an operator continues from the last answer: "+ 1" means "ans + 1". */
    char expr[MAX_INPUT + 8];
    int continues = nhistory > 0 && strchr("+*/^%", input[0]) != NULL;
    snprintf(expr, sizeof expr, "%s%s", continues ? "ans " : "", input);

    char err[256];
    char *out = calc_eval(&calc, expr, err, sizeof err);
    if (!out) {
        char msg[300];
        snprintf(msg, sizeof msg, "error: %s", err);
        add_entry(expr, msg, msg, 1);
        return;   /* keep the input so it can be fixed */
    }

    if (expr[0] == ':' || strncmp(out, "\xE2\x89\x88", 3) == 0) {   /* commands and "≈ ..." */
        add_entry(expr, out, out, 0);
    } else {
        char *shown = malloc(strlen(out) + 3);
        sprintf(shown, "= %s", out);
        add_entry(expr, shown, out, 0);
        free(shown);
    }
    free(out);
    input_set("");
    scroll = 0;
}

/* Re-runs a command like ":sci" and redraws the last result in the new format. */
static void command(const char *cmd, int reformat)
{
    char err[256];
    char *out = calc_eval(&calc, cmd, err, sizeof err);
    free(out);
    if (!reformat || nhistory == 0)
        return;
    Entry *e = &history[nhistory - 1];
    if (e->error || e->expr[0] == ':')
        return;
    char *s = rat_to_str(&calc.ans.r, calc.digits, calc.sci, calc.ans.exact);
    char *shown = malloc(strlen(s) + 3);
    sprintf(shown, "%s%s", calc.ans.exact ? "= " : "", s);
    free(e->result);
    free(e->copy);
    e->result = for_display(shown);
    e->copy = s;
    e->wrap_width = 0;
    free(shown);
}

static void press(const Button *b)
{
    switch (b->action) {
    case ACT_INSERT:
        input_insert(b->insert);
        break;
    case ACT_EVAL:
        evaluate();
        break;
    case ACT_CLEAR:
        input_set("");
        break;
    case ACT_BACKSPACE:
        input_backspace();
        break;
    }
}

/* ---- Widgets ------------------------------------------------------------------ */

static int any_hover;   /* set when the mouse is over anything clickable */

static int button(Rectangle r, const char *label, Color color, float text_size)
{
    Vector2 m = GetMousePosition();
    int hover = CheckCollisionPointRec(m, r);
    if (hover)
        any_hover = 1;
    int clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        color = ColorBrightness(color, -0.15f);
    else if (hover)
        color = ColorBrightness(color, 0.12f);
    DrawRectangleRounded(r, 0.22f, 8, color);

    float size = text_size;
    while (size > 10 && text_width(label, size) > r.width - 8)
        size -= 1;
    Vector2 t = MeasureTextEx(font, label, size, 0);
    draw_text(label, r.x + (r.width - t.x) / 2, r.y + (r.height - t.y) / 2, size, COL_TEXT);
    return clicked;
}

/* ---- Main ----------------------------------------------------------------------- */

static Font load_font(void)
{
    /* Only these characters get rendered; the rest of the font isn't loaded. */
    static const char *chars =
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~"
        "\xE2\x89\x88\xCF\x80\xCE\xA3\xCE\xA0\xE2\x88\x91\xE2\x88\x8F\xE2\x88\x9A"
        "\xC3\xB7\xC3\x97\xE2\x88\x92\xE2\x8C\xAB\xCA\xB8\xE2\x80\xA6";
    int count;
    int *cps = LoadCodepoints(chars, &count);

    const char *paths[] = {
        TextFormat("%sassets/%s", GetApplicationDirectory(), FONT_FILE),
        "assets/" FONT_FILE,
    };
    Font f = {0};
    for (size_t i = 0; i < 2 && f.texture.id == 0; i++) {
        if (FileExists(paths[i]))
            f = LoadFontEx(paths[i], 64, cps, count);
    }
    UnloadCodepoints(cps);
    if (f.texture.id == 0) {
        TraceLog(LOG_WARNING, "Calc: %s not found, symbols like pi won't draw", FONT_FILE);
        return GetFontDefault();
    }
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

static void handle_key(int key)
{
    switch (key) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        evaluate();
        break;
    case KEY_BACKSPACE:
        input_backspace();
        break;
    case KEY_DELETE:
        input_delete();
        break;
    case KEY_LEFT:
        cursor = prev_char(cursor);
        break;
    case KEY_RIGHT:
        cursor = next_char(cursor);
        break;
    case KEY_HOME:
        cursor = 0;
        break;
    case KEY_END:
        cursor = input_len;
        break;
    case KEY_ESCAPE:
        input_set("");
        recall = -1;
        break;
    case KEY_UP:
        if (nhistory > 0) {
            recall = recall < 0 ? nhistory - 1 : (recall > 0 ? recall - 1 : 0);
            input_set(history[recall].expr);
        }
        break;
    case KEY_DOWN:
        if (recall >= 0) {
            recall++;
            if (recall >= nhistory) {
                recall = -1;
                input_set("");
            } else {
                input_set(history[recall].expr);
            }
        }
        break;
    }
}

static void handle_keys(void)
{
    int mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
              IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    int cp;
    while ((cp = GetCharPressed()) > 0) {
        if (mod)
            continue;
        int n;
        char utf8[8];
        const char *bytes = CodepointToUTF8(cp, &n);
        memcpy(utf8, bytes, (size_t)n);
        utf8[n] = '\0';
        input_insert(utf8);
    }

    if (mod && IsKeyPressed(KEY_C) && nhistory > 0)
        SetClipboardText(history[nhistory - 1].copy);
    if (mod && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            char buf[MAX_INPUT];
            size_t j = 0;
            for (size_t i = 0; clip[i] && j + 1 < sizeof buf; i++)
                buf[j++] = clip[i] == '\n' || clip[i] == '\r' || clip[i] == '\t' ? ' ' : clip[i];
            buf[j] = '\0';
            input_insert(buf);
        }
    }

    /* GetKeyPressed() is a queue, so a key pressed and released within one
     * frame still counts; held keys repeat through IsKeyPressedRepeat(). */
    int key;
    while ((key = GetKeyPressed()) != 0)
        handle_key(key);
    const int repeating[] = {KEY_BACKSPACE, KEY_DELETE, KEY_LEFT, KEY_RIGHT};
    for (size_t i = 0; i < sizeof repeating / sizeof *repeating; i++)
        if (IsKeyPressedRepeat(repeating[i]))
            handle_key(repeating[i]);
}

static void draw_history(Rectangle area)
{
    float width = area.width - 2 * PAD;

    /* Lay out entries from the top, then anchor the bottom to the area. */
    float total = 0;
    for (int i = 0; i < nhistory; i++) {
        Entry *e = &history[i];
        if (e->wrap_width != width) {   /* measuring long results is slow; redo only on resize */
            e->expr_lines = wrap(e->expr, EXPR_SIZE, width, 0, 0, BLANK, 0);
            e->result_lines = wrap(e->result, RESULT_SIZE, width, 0, 0, BLANK, 0);
            e->wrap_width = width;
        }
        e->top = total;
        e->height = (float)e->expr_lines * EXPR_SIZE * 1.15f +
                    (float)e->result_lines * RESULT_SIZE * 1.15f + 4;
        total += e->height + 14;
    }

    float max_scroll = total > area.height ? total - area.height + PAD : 0;
    if (CheckCollisionPointRec(GetMousePosition(), area))
        scroll += GetMouseWheelMove() * 40;
    if (scroll < 0)
        scroll = 0;
    if (scroll > max_scroll)
        scroll = max_scroll;
    float origin = area.y + area.height - total + scroll;   /* y of the first entry */
    if (total < area.height)
        origin = area.y + area.height - total;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    if (nhistory == 0)
        draw_text("Type an expression or use the buttons.", area.x + PAD, area.y + PAD, EXPR_SIZE,
                  COL_MUTED);
    for (int i = 0; i < nhistory; i++) {
        Entry *e = &history[i];
        float y = origin + e->top;
        if (y + e->height < area.y || y > area.y + area.height)
            continue;
        Rectangle hit = {area.x, y - 4, area.width, e->height + 8};
        if (CheckCollisionPointRec(GetMousePosition(), hit)) {
            DrawRectangleRec(hit, ColorBrightness(COL_PANEL, 0.06f));
            any_hover = 1;
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                input_set(e->expr);   /* click to edit an earlier line */
        }
        int le = wrap(e->expr, EXPR_SIZE, width, area.x + PAD, y, COL_MUTED, 1);
        y += (float)le * EXPR_SIZE * 1.15f + 4;
        wrap(e->result, RESULT_SIZE, width, area.x + PAD, y, e->error ? COL_ERROR : COL_TEXT, 1);
    }
    EndScissorMode();
}

static void draw_input(Rectangle area)
{
    DrawRectangleRec(area, ColorBrightness(COL_PANEL, -0.12f));

    /* Scroll sideways so the cursor stays visible. */
    char before[MAX_INPUT];
    memcpy(before, input, cursor);
    before[cursor] = '\0';
    float cx = text_width(before, INPUT_SIZE);
    float room = area.width - 2 * PAD;
    float shift = cx > room ? cx - room : 0;
    float ty = area.y + (area.height - INPUT_SIZE) / 2;

    BeginScissorMode((int)area.x + (int)PAD / 2, (int)area.y, (int)(area.width - PAD), (int)area.height);
    draw_text(input, area.x + PAD - shift, ty, INPUT_SIZE, COL_TEXT);
    if (((int)(GetTime() * 2)) % 2 == 0)
        DrawRectangleRec((Rectangle){area.x + PAD - shift + cx, ty, 2, INPUT_SIZE}, COL_CURSOR);
    EndScissorMode();
}

static void draw_modebar(Rectangle area)
{
    float w = (area.width - 3 * GAP) / 4.6f;   /* the digits control is 1.6x wide */
    Rectangle r = {area.x, area.y, w, area.height};
    char label[32];

    if (button(r, calc.degrees ? "DEG" : "RAD", COL_TOGGLE, 16))
        command(calc.degrees ? ":rad" : ":deg", 0);
    r.x += w + GAP;
    if (button(r, calc.sci ? "SCI" : "FIX", COL_TOGGLE, 16))
        command(calc.sci ? ":fix" : ":sci", 1);
    r.x += w + GAP;

    /* digits: [-] 20 [+] */
    float dw = w * 1.6f;
    Rectangle minus = {r.x, r.y, r.height, r.height};
    Rectangle plus = {r.x + dw - r.height, r.y, r.height, r.height};
    if (button(minus, "\xE2\x88\x92", COL_FN, 18) && calc.digits > 1) {
        snprintf(label, sizeof label, ":digits %d", calc.digits > 5 ? calc.digits - 5 : 1);
        command(label, 0);
    }
    if (button(plus, "+", COL_FN, 18)) {
        snprintf(label, sizeof label, ":digits %d", calc.digits + 5);
        command(label, 0);
    }
    snprintf(label, sizeof label, "%d digits", calc.digits);
    Vector2 t = MeasureTextEx(font, label, 15, 0);
    draw_text(label, r.x + (dw - t.x) / 2, r.y + (r.height - t.y) / 2, 15, COL_MUTED);
    r.x += dw + GAP;

    if (button(r, "Copy", COL_FN, 16) && nhistory > 0)
        SetClipboardText(history[nhistory - 1].copy);
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(480, 780, "Calc");
    SetWindowMinSize(360, 620);
    SetExitKey(KEY_NULL);   /* Esc clears the input instead of quitting */
    SetTargetFPS(60);
    font = load_font();
    calc_init(&calc);

    while (!WindowShouldClose()) {
        handle_keys();

        float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
        float grid_h = H * 0.52f;
        Rectangle display = {PAD, PAD, W - 2 * PAD, H - grid_h - MODEBAR_H - 4 * PAD};
        Rectangle input_area = {display.x, display.y + display.height - 56, display.width, 56};
        Rectangle hist = {display.x, display.y, display.width, display.height - 56};
        Rectangle modebar = {PAD, display.y + display.height + PAD, W - 2 * PAD, MODEBAR_H};
        float gy = modebar.y + MODEBAR_H + PAD;
        float bw = (W - 2 * PAD - (COLS - 1) * GAP) / COLS;
        float bh = (H - gy - PAD - (ROWS - 1) * GAP) / ROWS;

        any_hover = 0;
        BeginDrawing();
        ClearBackground(COL_BG);
        DrawRectangleRounded(display, 0.04f, 8, COL_PANEL);
        draw_history(hist);
        draw_input(input_area);
        draw_modebar(modebar);

        int col = 0, row = 0;
        for (size_t i = 0; i < NBUTTONS; i++) {
            const Button *b = &BUTTONS[i];
            Rectangle r = {PAD + (float)col * (bw + GAP), gy + (float)row * (bh + GAP),
                           bw * (float)b->span + GAP * (float)(b->span - 1), bh};
            float size = bh * 0.38f < 26 ? bh * 0.38f : 26;
            if (button(r, b->label, kind_color(b->kind), size))
                press(b);
            col += b->span;
            if (col >= COLS) {
                col = 0;
                row++;
            }
        }
        SetMouseCursor(any_hover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
        EndDrawing();
    }

    for (int i = 0; i < nhistory; i++) {
        free(history[i].expr);
        free(history[i].result);
        free(history[i].copy);
    }
    calc_free(&calc);
    if (font.texture.id != GetFontDefault().texture.id)
        UnloadFont(font);
    CloseWindow();
    return 0;
}
