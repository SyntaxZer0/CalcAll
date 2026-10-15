/*
 * Graphical front end in the style of a Casio "natural display" calculator,
 * drawn with raylib (https://www.raylib.com).
 *
 * The expression at the top is a tree from mathedit.h: fractions, roots,
 * powers and sums are templates with boxes to fill in. Pressing "="
 * serializes the tree to text and hands it to calc_eval(), like main.c does.
 * The result appears in the bottom right of the screen.
 *
 * Keyboard: type normally; "/" makes a fraction from the number before it,
 * "^" makes a power. Arrows and Tab move between boxes, Up/Down also
 * replays earlier calculations, Enter evaluates, Esc clears, Cmd/Ctrl+C
 * copies the result, Cmd/Ctrl+V pastes text.
 *
 * The KEYS/TEXT switch hides the buttons. In TEXT mode, names typed in
 * calculator syntax turn into templates as you type (see ed_type()), so
 * "sum(k=1, 10, k^2)" becomes a Σ with its boxes filled in.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "calc.h"
#include "mathedit.h"

#define MAX_HISTORY 100
#define FONT_FILE "DejaVuSansMono.ttf"

/* ---- Colors and sizes ------------------------------------------------------ */

#define COL_BG      (Color){24, 26, 31, 255}
#define COL_SCREEN  (Color){33, 36, 43, 255}
#define COL_TEXT    (Color){232, 234, 240, 255}
#define COL_MUTED   (Color){138, 144, 158, 255}
#define COL_BOX     (Color){112, 118, 134, 255}
#define COL_ERROR   (Color){240, 113, 113, 255}
#define COL_CURSOR  (Color){255, 170, 70, 255}
#define COL_NUM     (Color){58, 62, 72, 255}
#define COL_OP      (Color){74, 80, 96, 255}
#define COL_FN      (Color){45, 49, 58, 255}
#define COL_CTRL    (Color){110, 62, 66, 255}
#define COL_ACCENT  (Color){230, 140, 40, 255}
#define COL_TOGGLE  (Color){52, 86, 130, 255}

#define PAD 12.0f
#define GAP 7.0f
#define MODEBAR_H 32.0f
#define EXPR_SIZE 34.0f
#define MIN_SIZE 12.0f

/* Font metrics as fractions of the font size (DejaVu Sans Mono). */
#define BASELINE 0.80f   /* baseline, measured down from the top of a text line */
#define ASC 0.68f        /* height of digits and capitals above the baseline */
#define DESC 0.20f       /* depth of descenders below the baseline */
#define AXIS 0.30f       /* height of the minus sign / fraction bar above the baseline */

/* ---- State ------------------------------------------------------------------- */

typedef struct {
    Row *expr;
    Value val;
    char *message;   /* set instead of val for commands like ":digits 30" */
} Entry;

static Font font;
static Calc calc;
static Editor ed;
static Entry history[MAX_HISTORY];
static int nhistory;
static int shown = -1;        /* history entry whose result is on screen; -1 while editing */
static int show_fraction = 1; /* S<=>D: exact fractions as a/b instead of decimals */
static int text_mode;         /* TEXT: no buttons; typed names like "sum" become templates */
static char error_text[300];  /* last error, shown until the next edit */
static float hscroll;         /* sideways scroll of a long expression */
static int any_hover;

static Row *icons[8];         /* little expressions drawn on template buttons */

/* ---- Text helpers ------------------------------------------------------------ */

static float tw(const char *s, float size)
{
    return MeasureTextEx(font, s, size, 0).x;
}

/* Draws text with its baseline at `base`. */
static void text_at(const char *s, float x, float base, float size, Color c)
{
    DrawTextEx(font, s, (Vector2){x, base - BASELINE * size}, size, 0, c);
}

static float sub_size(float s, float k)
{
    return s * k < MIN_SIZE ? MIN_SIZE : s * k;
}

static float thickness(float s)
{
    return s * 0.055f < 1.5f ? 1.5f : s * 0.055f;
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
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

/* ---- Natural display layout ---------------------------------------------------- */

typedef struct {
    float w, asc, desc;   /* width, height above the baseline, depth below it */
} Box;

static const Editor *caret_ed;   /* editor whose cursor is being drawn, if any */
static Rectangle caret;
static int caret_found;

static Box layout_row(const Row *r, float s, int draw, float x, float base, Color c);

static Box measure(const Row *r, float s)
{
    return layout_row(r, s, 0, 0, 0, BLANK);
}

static const char *display_char(const char *ch)
{
    if (strcmp(ch, "*") == 0)
        return "\xC3\x97";   /* × */
    if (strcmp(ch, "-") == 0)
        return "\xE2\x88\x92";   /* − */
    if (strcmp(ch, "/") == 0)
        return "\xC3\xB7";   /* ÷ */
    return ch;
}

/* Font size for parentheses tall enough to wrap the content. */
static float paren_size(Box content, float s)
{
    return maxf(s, (content.asc + content.desc) / (ASC + DESC) * 0.95f);
}

static void draw_paren(const char *p, float x, float base, Box content, float ps, Color c)
{
    /* Center the parenthesis on the content's vertical middle. */
    float mid = base - (content.asc - content.desc) / 2;
    text_at(p, x, mid + (BASELINE - 0.5f) * ps, ps, c);
}

/* The √ sign: a short tick, a down stroke, a long up stroke, then the bar. */
static void draw_radical(float x, float base, float s, float top, float bottom, float end, Color c)
{
    float th = thickness(s);
    Vector2 a = {x, base - 0.26f * s};
    Vector2 b = {x + 0.12f * s, base - 0.33f * s};
    Vector2 d = {x + 0.27f * s, bottom};
    Vector2 e = {x + 0.5f * s, top};
    Vector2 f = {end, top};
    DrawLineEx(a, b, th, c);
    DrawLineEx(b, d, th * 1.3f, c);
    DrawLineEx(d, e, th, c);
    DrawLineEx(e, f, th, c);
}

static float prev_asc;   /* height of the item before the current one, for exponents */

static Box layout_item(const Item *it, float s, int draw, float x, float base, Color c)
{
    float th = thickness(s);
    Box b = {0, ASC * s, DESC * s};

    switch (it->kind) {
    case IT_CHAR: {
        const char *t = display_char(it->ch);
        b.w = tw(t, s);
        if (draw)
            text_at(t, x, base, s, c);
        break;
    }

    case IT_FRAC: {
        float gap = 0.12f * s;
        Box n = measure(it->slot[0], s * 0.92f), d = measure(it->slot[1], s * 0.92f);
        float axis = base - AXIS * s;
        float nbase = axis - gap - n.desc, dbase = axis + th + gap + d.asc;
        b.w = maxf(n.w, d.w) + 0.4f * s;
        b.asc = base - (nbase - n.asc);
        b.desc = dbase + d.desc - base;
        if (draw) {
            DrawRectangleRec((Rectangle){x + 0.1f * s, axis, b.w - 0.2f * s, th}, c);
            layout_row(it->slot[0], s * 0.92f, 1, x + (b.w - n.w) / 2, nbase, c);
            layout_row(it->slot[1], s * 0.92f, 1, x + (b.w - d.w) / 2, dbase, c);
        }
        break;
    }

    case IT_POW: {
        float es = sub_size(s, 0.66f);
        Box e = measure(it->slot[0], es);
        /* Sit above whatever precedes it, even a tall fraction. */
        float raise = maxf(0.42f * s, prev_asc - 0.3f * s);
        b.w = e.w + 0.06f * s;
        b.asc = maxf(ASC * s, raise + e.asc);
        b.desc = maxf(0, e.desc - raise);
        if (draw)
            layout_row(it->slot[0], es, 1, x + 0.03f * s, base - raise, c);
        break;
    }

    case IT_SQRT:
    case IT_ROOT: {
        int has_index = it->kind == IT_ROOT;
        const Row *rad = it->slot[has_index ? 1 : 0];
        Box r = measure(rad, s);
        float is = sub_size(s, 0.55f);
        Box idx = has_index ? measure(it->slot[0], is) : (Box){0, 0, 0};
        float shift = has_index ? maxf(0, idx.w - 0.22f * s) : 0;
        float lead = 0.55f * s;
        float top = base - r.asc - 0.14f * s;
        b.w = shift + lead + r.w + 0.12f * s;
        b.asc = r.asc + 0.14f * s + th;
        b.desc = r.desc + 0.06f * s;
        float ibase = base - 0.4f * s;
        if (has_index)
            b.asc = maxf(b.asc, base - ibase + idx.asc);
        if (draw) {
            draw_radical(x + shift, base, s, top, base + r.desc + 0.04f * s, x + b.w, c);
            layout_row(rad, s, 1, x + shift + lead, base, c);
            if (has_index)
                layout_row(it->slot[0], is, 1, x + shift + 0.22f * s - idx.w, ibase, c);
        }
        break;
    }

    case IT_SUM:
    case IT_PROD: {
        const char *sym = it->kind == IT_SUM ? "\xCE\xA3" : "\xCE\xA0";
        float ss = s * 1.4f, ls = sub_size(s, 0.6f);
        float sw = tw(sym, ss);
        float sym_base = base - AXIS * s + 0.34f * ss;   /* centers the symbol on the axis */
        float sym_top = sym_base - 0.72f * ss;
        Box up = measure(it->slot[2], ls);
        Box v = measure(it->slot[0], ls), lo = measure(it->slot[1], ls);
        float eqw = tw("=", ls);
        float lw = v.w + eqw + lo.w;
        float lasc = maxf(maxf(v.asc, lo.asc), ASC * ls);
        float ldesc = maxf(maxf(v.desc, lo.desc), DESC * ls);
        float col = maxf(maxf(sw, up.w), lw) + 0.1f * s;
        float up_base = sym_top - 0.08f * s - up.desc;
        float lo_base = sym_base + 0.12f * s + lasc;
        Box body = measure(it->slot[3], s);
        float ps = paren_size(body, s), pw = tw("(", ps);
        b.w = col + 0.08f * s + 2 * pw + body.w;
        b.asc = maxf(base - up_base + up.asc, body.asc);
        b.desc = maxf(lo_base - base + ldesc, body.desc);
        if (draw) {
            text_at(sym, x + (col - sw) / 2, sym_base, ss, c);
            layout_row(it->slot[2], ls, 1, x + (col - up.w) / 2, up_base, c);
            float lx = x + (col - lw) / 2;
            layout_row(it->slot[0], ls, 1, lx, lo_base, c);
            text_at("=", lx + v.w, lo_base, ls, c);
            layout_row(it->slot[1], ls, 1, lx + v.w + eqw, lo_base, c);
            float bx = x + col + 0.08f * s;
            draw_paren("(", bx, base, body, ps, c);
            layout_row(it->slot[3], s, 1, bx + pw, base, c);
            draw_paren(")", bx + pw + body.w, base, body, ps, c);
        }
        break;
    }

    case IT_ABS: {
        Box in = measure(it->slot[0], s);
        b.w = in.w + 0.4f * s;
        b.asc = maxf(in.asc, ASC * s) + 0.06f * s;
        b.desc = maxf(in.desc, DESC * s) + 0.06f * s;
        if (draw) {
            DrawRectangleRec((Rectangle){x + 0.1f * s, base - b.asc, th, b.asc + b.desc}, c);
            DrawRectangleRec((Rectangle){x + b.w - 0.1f * s - th, base - b.asc, th, b.asc + b.desc}, c);
            layout_row(it->slot[0], s, 1, x + 0.2f * s, base, c);
        }
        break;
    }

    case IT_LOGB: {
        float bs = sub_size(s, 0.6f);
        float lw = tw("log", s);
        Box bb = measure(it->slot[0], bs), arg = measure(it->slot[1], s);
        float bbase = base + 0.25f * s;
        float ps = paren_size(arg, s), pw = tw("(", ps);
        b.w = lw + bb.w + 0.05f * s + 2 * pw + arg.w;
        b.asc = maxf(ASC * s, arg.asc);
        b.desc = maxf(maxf(DESC * s, bbase - base + bb.desc), arg.desc);
        if (draw) {
            text_at("log", x, base, s, c);
            layout_row(it->slot[0], bs, 1, x + lw, bbase, c);
            float ax = x + lw + bb.w + 0.05f * s;
            draw_paren("(", ax, base, arg, ps, c);
            layout_row(it->slot[1], s, 1, ax + pw, base, c);
            draw_paren(")", ax + pw + arg.w, base, arg, ps, c);
        }
        break;
    }
    }
    return b;
}

static void set_caret(float x, float base, float s)
{
    caret = (Rectangle){x, base - ASC * s - 0.08f * s, 2, (ASC + DESC + 0.12f) * s};
    caret_found = 1;
}

/*
 * Lays out a row at font size s. With `draw`, also draws it with its
 * baseline at (x, base), and records where the cursor goes. Returns the size.
 */
static Box layout_row(const Row *r, float s, int draw, float x, float base, Color c)
{
    int is_cursor_row = draw && caret_ed && caret_ed->row == r;

    if (r->len == 0) {
        if (!r->owner) {   /* the empty top row: just a cursor */
            if (is_cursor_row)
                set_caret(x, base, s);
            return (Box){0, ASC * s, DESC * s};
        }
        /* An empty box to fill in. */
        Box e = {0.6f * s, 0.7f * s, 0.06f * s};
        if (draw) {
            Rectangle box = {x + 0.06f * s, base - e.asc, e.w - 0.12f * s, e.asc + e.desc};
            DrawRectangleRec(box, Fade(COL_BOX, 0.25f));
            DrawRectangleLinesEx(box, thickness(s) * 0.8f, is_cursor_row ? COL_CURSOR : COL_BOX);
            if (is_cursor_row)
                set_caret(x + 0.15f * s, base, s);
        }
        return e;
    }

    Box b = {0, ASC * s, DESC * s};
    float cx = x;
    prev_asc = ASC * s;
    for (int i = 0; i < r->len; i++) {
        if (is_cursor_row && caret_ed->pos == i)
            set_caret(cx, base, s);
        Box ib = layout_item(r->items[i], s, draw, cx, base, c);
        prev_asc = ib.asc;
        cx += ib.w;
        b.asc = maxf(b.asc, ib.asc);
        b.desc = maxf(b.desc, ib.desc);
    }
    if (is_cursor_row && caret_ed->pos == r->len)
        set_caret(cx, base, s);
    b.w = cx - x;
    return b;
}

/* ---- Calculation ------------------------------------------------------------- */

static void entry_free(Entry *e)
{
    row_free(e->expr);
    val_free(&e->val);
    free(e->message);
}

static void evaluate(void)
{
    if (ed_is_empty(&ed))
        return;
    int empty;
    char *text = ed_serialize(ed.root, &empty);
    if (empty) {
        snprintf(error_text, sizeof error_text, "Fill in the empty box first");
        free(text);
        return;
    }

    char err[256];
    char *out = calc_eval(&calc, text, err, sizeof err);
    free(text);
    if (!out) {
        snprintf(error_text, sizeof error_text, "%s", err);
        return;
    }

    if (nhistory == MAX_HISTORY) {
        entry_free(&history[0]);
        memmove(history, history + 1, (MAX_HISTORY - 1) * sizeof *history);
        nhistory--;
    }
    Entry *e = &history[nhistory++];
    e->expr = row_clone(ed.root);
    val_init(&e->val);
    e->message = NULL;
    if (!calc.result_is_value)
        e->message = dup(out);   /* a command or a definition, like f(x) = x^2 */
    else
        val_copy(&e->val, &calc.ans);
    free(out);
    shown = nhistory - 1;
}

/* Shows an earlier calculation (dir < 0: older). */
static void replay(int dir)
{
    if (nhistory == 0)
        return;
    int i = shown >= 0 ? shown + dir : (dir < 0 ? nhistory - 1 : -1);
    if (i < 0)
        i = 0;
    if (i >= nhistory)
        return;
    ed_load(&ed, history[i].expr);
    shown = i;
    error_text[0] = '\0';
}

/*
 * Called before anything is typed. After a result, typing starts a new
 * calculation; starting with an operator continues from Ans, as on a Casio.
 */
static void begin_edit(int continues)
{
    if (shown >= 0) {
        ed_clear(&ed);
        if (continues)
            ed_insert_text(&ed, "Ans");
        shown = -1;
    }
    error_text[0] = '\0';
}

/* Like begin_edit, but keeps the expression to edit it (arrows, delete). */
static void resume_edit(void)
{
    shown = -1;
    error_text[0] = '\0';
}

static void type_text(const char *s)
{
    begin_edit(strchr("+-*/^!%", s[0]) != NULL);
    ed_insert_text(&ed, s);
}

/* ---- Results ---------------------------------------------------------------- */

/* The current result as text, or NULL when there is none to show. */
static char *result_text(int *is_fraction)
{
    *is_fraction = 0;
    if (shown < 0)
        return NULL;
    Entry *e = &history[shown];
    if (e->message)
        return dup(e->message);
    const Rational *r = &e->val.r;
    if (e->val.exact && rat_is_zero(&e->val.im) && !rat_is_int(r) && show_fraction && !calc.sci &&
        bi_num_digits(&r->num) + bi_num_digits(&r->den) <= 40) {
        *is_fraction = 1;
        char *n = bi_to_str(&r->num), *d = bi_to_str(&r->den);
        char *s = malloc(strlen(n) + strlen(d) + 2);
        sprintf(s, "%s/%s", n, d);
        free(n);
        free(d);
        return s;
    }
    char *s = val_to_str(&e->val, calc.digits, calc.sci);
    char *note = strstr(s, "  (=");   /* "0.333...  (= 1/3)": S<=>D shows the fraction */
    if (note)
        *note = '\0';
    return s;
}

static size_t utf8_len(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        n += ((unsigned char)*s & 0xC0) != 0x80;
    return n;
}

/* Byte offset of the n-th character. */
static size_t utf8_offset(const char *s, size_t n)
{
    size_t i = 0;
    for (; s[i] && n; n--) {
        i++;
        while (s[i] && ((unsigned char)s[i] & 0xC0) == 0x80)
            i++;
    }
    return i;
}

/* Right-aligned text in the bottom of `area`, wrapped, shrinking if needed. */
static void draw_text_result(const char *s, Rectangle area, Color c)
{
    /* Shrink to fit on one line if possible (down to 20); only longer
     * results wrap, at the largest size where all the lines fit. */
    static const float sizes[] = {36, 32, 28, 24, 20, 18};
    const int count = sizeof sizes / sizeof *sizes;
    size_t n = utf8_len(s);
    float size = 18, cw = 0;
    size_t per_line = 1, max_lines = 1;
    for (int pass = 0; pass < 2; pass++) {
        int found = 0;
        for (int i = 0; i < count && !found; i++) {
            size = sizes[i];
            cw = tw("0", size);   /* every character is this wide in a monospaced font */
            per_line = (size_t)(area.width / cw);
            max_lines = (size_t)(area.height / (size * 1.15f));
            if (per_line < 1)
                per_line = 1;
            if (max_lines < 1)
                max_lines = 1;
            if (pass == 0)
                found = size >= 20 && n <= per_line;
            else
                found = (n + per_line - 1) / per_line <= max_lines;
        }
        if (found)
            break;
    }

    size_t lines = (n + per_line - 1) / per_line;
    char note[96] = "";
    if (lines > max_lines) {
        /* Too long even at the smallest size: show the start and how much is left. */
        size_t kept = (max_lines - 1) * per_line;
        snprintf(note, sizeof note, "\xE2\x80\xA6 %zu more (Copy gets all)", n - kept);
        n = kept;
        lines = max_lines - 1;
    }

    /* Lines are right-aligned, so a short line goes first and the number
     * ends flush right; a cut-off result keeps its start instead. */
    size_t first = note[0] || n % per_line == 0 ? per_line : n % per_line;
    float line_h = size * 1.15f;
    float y = area.y + area.height - (float)(lines + (note[0] ? 1 : 0)) * line_h;
    char buf[1024];
    size_t at = 0;
    for (size_t l = 0; l < lines && at < n; l++) {
        size_t count = l == 0 ? first : per_line;
        if (count > n - at)
            count = n - at;
        size_t from = utf8_offset(s, at), to = utf8_offset(s, at + count);
        size_t len = to - from < sizeof buf - 1 ? to - from : sizeof buf - 1;
        memcpy(buf, s + from, len);
        buf[len] = '\0';
        DrawTextEx(font, buf, (Vector2){area.x + area.width - (float)count * cw, y + (float)l * line_h}, size,
                   0, c);
        at += count;
    }
    if (note[0])
        DrawTextEx(font, note, (Vector2){area.x + area.width - tw(note, 16), y + (float)lines * line_h},
                   16, 0, COL_MUTED);
}

/* A message (like an error), word-wrapped and right-aligned at the bottom. */
static void draw_message(const char *s, Rectangle area, Color c)
{
    const float size = 20, line_h = size * 1.2f;
    char lines[8][160];
    int n = 0;
    const char *p = s;
    while (*p && n < 8) {
        /* Take as many whole words as fit on this line. */
        size_t fit = 0, i = 0;
        char buf[160];
        while (p[i] && i < sizeof buf - 1) {
            size_t j = i;
            while (p[j] && p[j] != ' ' && j < sizeof buf - 1)
                j++;
            memcpy(buf, p, j);
            buf[j] = '\0';
            if (fit && tw(buf, size) > area.width)
                break;
            fit = j;
            i = p[j] == ' ' ? j + 1 : j;
        }
        if (!fit)
            fit = i;
        snprintf(lines[n++], sizeof lines[0], "%.*s", (int)fit, p);
        p += fit;
        while (*p == ' ')
            p++;
    }
    float y = area.y + area.height - (float)n * line_h;
    for (int i = 0; i < n; i++)
        DrawTextEx(font, lines[i], (Vector2){area.x + area.width - tw(lines[i], size), y + (float)i * line_h},
                   size, 0, c);
}

/* "a/b" drawn as a stacked fraction in the bottom right. */
static void draw_fraction_result(const char *s, Rectangle area)
{
    float size = 34;
    const char *slash = strchr(s, '/');
    char num[64], den[64];
    int neg = s[0] == '-';
    snprintf(num, sizeof num, "%.*s", (int)(slash - s - neg), s + neg);
    snprintf(den, sizeof den, "%s", slash + 1);
    while (size > 16 && maxf(tw(num, size), tw(den, size)) + tw("\xE2\x88\x92", size) > area.width)
        size -= 2;
    float th = thickness(size);
    float w = maxf(tw(num, size), tw(den, size)) + 0.3f * size;
    float dbase = area.y + area.height - DESC * size;        /* denominator baseline */
    float axis = dbase - ASC * size - 0.15f * size - th;     /* fraction bar */
    float nbase = axis - 0.15f * size - DESC * size * 0.5f;  /* numerator baseline */
    float x = area.x + area.width - w;
    DrawRectangleRec((Rectangle){x, axis, w, th}, COL_TEXT);
    text_at(num, x + (w - tw(num, size)) / 2, nbase, size, COL_TEXT);
    text_at(den, x + (w - tw(den, size)) / 2, dbase, size, COL_TEXT);
    if (neg) {
        float mw = tw("\xE2\x88\x92", size);
        text_at("\xE2\x88\x92", x - mw - 0.1f * size, axis + AXIS * size, size, COL_TEXT);
    }
}

static void copy_result(void)
{
    int frac;
    char *s = result_text(&frac);
    if (s) {
        SetClipboardText(strncmp(s, "\xE2\x89\x88 ", 4) == 0 ? s + 4 : s);   /* drop "≈ " */
        free(s);
    }
}

/* ---- Screen -------------------------------------------------------------------- */

static void draw_screen(Rectangle area)
{
    DrawRectangleRounded(area, 0.05f, 8, COL_SCREEN);

    /* Status line, like the indicators at the top of a Casio display. */
    char status[64];
    snprintf(status, sizeof status, "%s  %s  %s  %d digits", calc.degrees ? "DEG" : "RAD",
             calc.sci ? "SCI" : "FIX", show_fraction ? "a/b" : "dec", calc.digits);
    DrawTextEx(font, status, (Vector2){area.x + PAD, area.y + 6}, 13, 0, COL_MUTED);

    /* The expression, top left, scrolled sideways to keep the cursor visible. */
    Rectangle expr_area = {area.x + PAD, area.y + 24, area.width - 2 * PAD, area.height * 0.5f};
    Box b = measure(ed.root, EXPR_SIZE);
    float base = expr_area.y + b.asc + 4;
    caret_ed = shown < 0 ? &ed : NULL;
    caret_found = 0;
    BeginScissorMode((int)area.x + 4, (int)area.y, (int)area.width - 8, (int)area.height);
    layout_row(ed.root, EXPR_SIZE, 1, expr_area.x - hscroll, base, COL_TEXT);
    if (caret_found && ((int)(GetTime() * 2.5)) % 2 == 0)
        DrawRectangleRec(caret, COL_CURSOR);
    EndScissorMode();
    caret_ed = NULL;

    if (caret_found) {
        float cx = caret.x + hscroll - expr_area.x;   /* cursor position within the expression */
        if (cx - hscroll > expr_area.width - 10)
            hscroll = cx - expr_area.width + 10;
        if (cx - hscroll < 0)
            hscroll = cx > 20 ? cx - 20 : 0;
    }
    if (b.w <= expr_area.width)
        hscroll = 0;

    /* The result, bottom right. */
    float top = base + b.desc + 10;
    Rectangle res = {area.x + PAD, top, area.width - 2 * PAD, area.y + area.height - PAD - top};
    if (error_text[0]) {
        draw_message(error_text, res, COL_ERROR);
    } else {
        int frac;
        char *s = result_text(&frac);
        if (s) {
            if (frac)
                draw_fraction_result(s, res);
            else if (history[shown].message)
                draw_message(s, res, COL_MUTED);
            else
                draw_text_result(s, res, COL_TEXT);
            free(s);
        }
    }
}

/* ---- Buttons ------------------------------------------------------------------- */

enum { A_TEXT, A_TEMPLATE, A_SQUARE, A_INVERSE, A_POW, A_EXP, A_EVAL, A_DEL, A_AC };
enum { K_NUM, K_OP, K_FN, K_CTRL, K_ACCENT };

typedef struct {
    const char *label;   /* text label, unless icon >= 0 */
    int icon;            /* index into icons[], or -1 */
    int action;
    const char *text;    /* A_TEXT: what to type */
    ItemKind kind;       /* A_TEMPLATE */
    int color;
} Button;

enum { ICON_FRAC, ICON_SQRT, ICON_ROOT, ICON_SQUARE, ICON_POW, ICON_LOGB, ICON_ABS, ICON_EXP };

#define TPL(icon, kind) {NULL, icon, A_TEMPLATE, NULL, kind, K_FN}
#define TXT(label, text, color) {label, -1, A_TEXT, text, IT_CHAR, color}

static const Button FN_BUTTONS[] = {
    TPL(ICON_FRAC, IT_FRAC), TPL(ICON_SQRT, IT_SQRT), TPL(ICON_ROOT, IT_ROOT),
    {NULL, ICON_SQUARE, A_SQUARE, NULL, IT_CHAR, K_FN}, {NULL, ICON_POW, A_POW, NULL, IT_CHAR, K_FN},
    TPL(ICON_LOGB, IT_LOGB),

    {"\xCE\xA3", -1, A_TEMPLATE, NULL, IT_SUM, K_FN}, {"\xCE\xA0", -1, A_TEMPLATE, NULL, IT_PROD, K_FN},
    TPL(ICON_ABS, IT_ABS), TXT("ln", "ln(", K_FN), TXT("log", "log(", K_FN),
    {NULL, ICON_EXP, A_EXP, NULL, IT_CHAR, K_FN},

    TXT("sin", "sin(", K_FN), TXT("cos", "cos(", K_FN), TXT("tan", "tan(", K_FN),
    TXT("sin\xE2\x81\xBB\xC2\xB9", "asin(", K_FN), TXT("cos\xE2\x81\xBB\xC2\xB9", "acos(", K_FN),
    TXT("tan\xE2\x81\xBB\xC2\xB9", "atan(", K_FN),

    TXT("\xCF\x80", "\xCF\x80", K_FN), TXT("e", "e", K_FN), TXT("k", "k", K_FN), TXT("n!", "!", K_FN),
    TXT("(", "(", K_FN), TXT(")", ")", K_FN),

    TXT("i", "i", K_FN), TXT("Re", "re(", K_FN), TXT("Im", "im(", K_FN), TXT("conj", "conj(", K_FN),
    TXT("arg", "arg(", K_FN), {"x\xE2\x81\xBB\xC2\xB9", -1, A_INVERSE, NULL, IT_CHAR, K_FN},
};

static const Button NUM_BUTTONS[] = {
    TXT("7", "7", K_NUM), TXT("8", "8", K_NUM), TXT("9", "9", K_NUM),
    {"DEL", -1, A_DEL, NULL, IT_CHAR, K_CTRL}, {"AC", -1, A_AC, NULL, IT_CHAR, K_CTRL},

    TXT("4", "4", K_NUM), TXT("5", "5", K_NUM), TXT("6", "6", K_NUM),
    TXT("\xC3\x97", "*", K_OP), TXT("\xC3\xB7", "/", K_OP),

    TXT("1", "1", K_NUM), TXT("2", "2", K_NUM), TXT("3", "3", K_NUM),
    TXT("+", "+", K_OP), TXT("\xE2\x88\x92", "-", K_OP),

    TXT("0", "0", K_NUM), TXT(".", ".", K_NUM), TXT(",", ",", K_OP), TXT("Ans", "Ans", K_OP),
    {"=", -1, A_EVAL, NULL, IT_CHAR, K_ACCENT},
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

static void make_icons(void)
{
    Editor t;
    ItemKind tpl[] = {IT_FRAC, IT_SQRT, IT_ROOT};
    for (int i = 0; i < 3; i++) {
        ed_init(&t);
        ed_insert_template(&t, tpl[i], 0);
        icons[i] = row_clone(t.root);
        ed_free(&t);
    }
    ed_init(&t);
    ed_insert_text(&t, "x");
    ed_insert_pow(&t, "2");
    icons[ICON_SQUARE] = row_clone(t.root);
    ed_clear(&t);
    ed_insert_text(&t, "x");
    ed_insert_pow(&t, NULL);
    icons[ICON_POW] = row_clone(t.root);
    ed_clear(&t);
    ed_insert_template(&t, IT_LOGB, 0);
    icons[ICON_LOGB] = row_clone(t.root);
    ed_clear(&t);
    ed_insert_template(&t, IT_ABS, 0);
    icons[ICON_ABS] = row_clone(t.root);
    ed_clear(&t);
    ed_insert_text(&t, "e");
    ed_insert_pow(&t, "x");
    icons[ICON_EXP] = row_clone(t.root);
    ed_free(&t);
}

static int button_frame(Rectangle r, Color color)
{
    int hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (hover) {
        any_hover = 1;
        color = ColorBrightness(color, IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? -0.15f : 0.12f);
    }
    DrawRectangleRounded(r, 0.22f, 8, color);
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static int text_button(Rectangle r, const char *label, Color color, float size)
{
    int clicked = button_frame(r, color);
    while (size > 10 && tw(label, size) > r.width - 8)
        size -= 1;
    Vector2 t = MeasureTextEx(font, label, size, 0);
    DrawTextEx(font, label, (Vector2){r.x + (r.width - t.x) / 2, r.y + (r.height - t.y) / 2}, size, 0,
               COL_TEXT);
    return clicked;
}

static int icon_button(Rectangle r, const Row *icon, Color color)
{
    int clicked = button_frame(r, color);
    float s = r.height * 0.36f;
    Box b = measure(icon, s);
    while (s > 8 && (b.asc + b.desc > r.height - 6 || b.w > r.width - 8)) {
        s -= 1;
        b = measure(icon, s);
    }
    float base = r.y + (r.height - (b.asc + b.desc)) / 2 + b.asc;
    layout_row(icon, s, 1, r.x + (r.width - b.w) / 2, base, COL_TEXT);
    return clicked;
}

static void press(const Button *b)
{
    switch (b->action) {
    case A_TEXT:
        type_text(b->text);
        break;
    case A_TEMPLATE:
        begin_edit(0);
        ed_insert_template(&ed, b->kind, 0);
        break;
    case A_SQUARE:
        begin_edit(1);
        ed_insert_pow(&ed, "2");
        break;
    case A_INVERSE:   /* x^-1 */
        begin_edit(1);
        ed_insert_pow(&ed, "-1");
        break;
    case A_POW:
        begin_edit(1);
        ed_insert_pow(&ed, NULL);
        break;
    case A_EXP:
        begin_edit(0);
        ed_insert_text(&ed, "e");
        ed_insert_pow(&ed, NULL);
        break;
    case A_EVAL:
        evaluate();
        break;
    case A_DEL:
        resume_edit();
        ed_backspace(&ed);
        break;
    case A_AC:
        ed_clear(&ed);
        resume_edit();
        break;
    }
}

static void draw_grid(const Button *buttons, int count, int cols, Rectangle area)
{
    int rows = (count + cols - 1) / cols;
    float bw = (area.width - (float)(cols - 1) * GAP) / (float)cols;
    float bh = (area.height - (float)(rows - 1) * GAP) / (float)rows;
    float size = bh * 0.4f < 24 ? bh * 0.4f : 24;
    for (int i = 0; i < count; i++) {
        const Button *b = &buttons[i];
        Rectangle r = {area.x + (float)(i % cols) * (bw + GAP), area.y + (float)(i / cols) * (bh + GAP), bw,
                       bh};
        Color color = kind_color(b->color);
        int clicked = b->icon >= 0 ? icon_button(r, icons[b->icon], color)
                                   : text_button(r, b->label, color, size);
        if (clicked)
            press(b);
    }
}

static void command(const char *cmd)
{
    char err[256];
    free(calc_eval(&calc, cmd, err, sizeof err));
}

static void draw_modebar(Rectangle area)
{
    float unit = (area.width - 5 * GAP) / 6.6f;
    Rectangle r = {area.x, area.y, unit, area.height};
    char label[32];

    if (text_button(r, text_mode ? "TEXT" : "KEYS", COL_ACCENT, 15))
        text_mode = !text_mode;
    r.x += unit + GAP;

    if (text_button(r, calc.degrees ? "DEG" : "RAD", COL_TOGGLE, 15))
        command(calc.degrees ? ":rad" : ":deg");
    r.x += unit + GAP;
    if (text_button(r, calc.sci ? "SCI" : "FIX", COL_TOGGLE, 15))
        command(calc.sci ? ":fix" : ":sci");
    r.x += unit + GAP;
    if (text_button(r, "S\xE2\x87\x94" "D", COL_TOGGLE, 15))
        show_fraction = !show_fraction;
    r.x += unit + GAP;

    float dw = unit * 1.6f;
    Rectangle minus = {r.x, r.y, r.height, r.height};
    Rectangle plus = {r.x + dw - r.height, r.y, r.height, r.height};
    if (text_button(minus, "\xE2\x88\x92", COL_FN, 18) && calc.digits > 1) {
        snprintf(label, sizeof label, ":digits %d", calc.digits > 5 ? calc.digits - 5 : 1);
        command(label);
    }
    if (text_button(plus, "+", COL_FN, 18)) {
        snprintf(label, sizeof label, ":digits %d", calc.digits + 5);
        command(label);
    }
    snprintf(label, sizeof label, "%d", calc.digits);
    Vector2 t = MeasureTextEx(font, label, 15, 0);
    DrawTextEx(font, label, (Vector2){r.x + (dw - t.x) / 2, r.y + (r.height - t.y) / 2}, 15, 0, COL_MUTED);
    r.x += dw + GAP;

    if (text_button(r, "Copy", COL_FN, 15))
        copy_result();
}

/* ---- Keyboard ------------------------------------------------------------------- */

static void handle_key(int key)
{
    switch (key) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        evaluate();
        break;
    case KEY_BACKSPACE:
    case KEY_DELETE:
        resume_edit();
        ed_backspace(&ed);
        break;
    case KEY_LEFT:
        if (shown >= 0)
            ed_end(&ed);
        else
            ed_left(&ed);
        resume_edit();
        break;
    case KEY_RIGHT:
        if (shown >= 0)
            ed_home(&ed);
        else
            ed_right(&ed);
        resume_edit();
        break;
    case KEY_HOME:
        ed_home(&ed);
        resume_edit();
        break;
    case KEY_END:
        ed_end(&ed);
        resume_edit();
        break;
    case KEY_UP:
    case KEY_DOWN: {
        int dir = key == KEY_UP ? -1 : 1;
        /* Inside a fraction or sum, move between its boxes; otherwise replay. */
        if (shown >= 0 || !ed_vertical(&ed, dir)) {
            if (shown >= 0 || ed_is_empty(&ed))
                replay(dir);
        }
        break;
    }
    case KEY_TAB:
        if (shown >= 0)
            ed_home(&ed);
        ed_tab(&ed, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? -1 : 1);
        resume_edit();
        break;
    case KEY_ESCAPE:
        ed_clear(&ed);
        resume_edit();
        break;
    }
}

/* Text mode: one character through the editor's typing rules (ed_type). */
static void type_char_text(const char *utf8)
{
    begin_edit(utf8[1] == '\0' && strchr("+-*/^!%", utf8[0]) != NULL);
    ed_type(&ed, utf8);
}

static void handle_keys(void)
{
    int mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
              IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    int cp;
    while ((cp = GetCharPressed()) > 0) {
        if (mod || cp == ' ')
            continue;
        if (text_mode) {
            int n;
            char utf8[8];
            const char *bytes = CodepointToUTF8(cp, &n);
            memcpy(utf8, bytes, (size_t)n);
            utf8[n] = '\0';
            type_char_text(utf8);
        } else if (cp == '/') {   /* "12/" makes a fraction with 12 on top */
            begin_edit(1);
            ed_insert_template(&ed, IT_FRAC, 1);
        } else if (cp == '^') {
            begin_edit(1);
            ed_insert_pow(&ed, NULL);
        } else {
            int n;
            char utf8[8];
            const char *bytes = CodepointToUTF8(cp, &n);
            memcpy(utf8, bytes, (size_t)n);
            utf8[n] = '\0';
            type_text(utf8);
        }
    }

    if (mod && IsKeyPressed(KEY_C))
        copy_result();
    if (mod && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip && *clip) {
            char buf[2048];
            size_t j = 0;
            for (size_t i = 0; clip[i] && j + 1 < sizeof buf; i++)
                if (clip[i] != '\n' && clip[i] != '\r' && clip[i] != '\t')
                    buf[j++] = clip[i];
            buf[j] = '\0';
            if (text_mode) {
                /* Pasting "sum(k=1, 10, k^2)" builds the template too. */
                for (const char *p = buf; *p;) {
                    unsigned char c = (unsigned char)*p;
                    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                    char one[8] = {0};
                    for (int k = 0; k < n && p[k]; k++)
                        one[k] = p[k];
                    if (*p != ' ')
                        type_char_text(one);
                    p += strlen(one);
                }
            } else {
                type_text(buf);
            }
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

/* ---- Main ------------------------------------------------------------------------ */

static Font load_font(void)
{
    /* Only these characters get rendered; the rest of the font isn't loaded. */
    static const char *chars =
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~"
        "\xE2\x89\x88\xCF\x80\xCE\xA3\xCE\xA0\xE2\x88\x91\xE2\x88\x8F\xE2\x88\x9A"
        "\xC3\xB7\xC3\x97\xE2\x88\x92\xE2\x8C\xAB\xCA\xB8\xE2\x80\xA6\xE2\x81\xBB\xC2\xB9"
        "\xE2\x87\x94\xE2\x86\x92\xC2\xB7";
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

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(480, 800, "Calc");
    SetWindowMinSize(380, 660);
    SetExitKey(KEY_NULL);   /* Esc clears instead of quitting */
    SetTargetFPS(60);
    font = load_font();
    calc_init(&calc);
    ed_init(&ed);
    make_icons();

    while (!WindowShouldClose()) {
        handle_keys();

        float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
        float keys_h = text_mode ? 40 : H * 0.6f;   /* text mode: room for a hint, no buttons */
        Rectangle screen = {PAD, PAD, W - 2 * PAD, H - keys_h - MODEBAR_H - 3 * PAD};
        Rectangle modebar = {PAD, screen.y + screen.height + PAD, W - 2 * PAD, MODEBAR_H};
        float gy = modebar.y + MODEBAR_H + PAD;
        float fn_h = (H - gy - PAD) * 0.53f;   /* 5 rows of functions over 4 of numbers */
        Rectangle fn_area = {PAD, gy, W - 2 * PAD, fn_h};
        Rectangle num_area = {PAD, gy + fn_h + PAD, W - 2 * PAD, H - gy - fn_h - 2 * PAD};

        any_hover = 0;
        BeginDrawing();
        ClearBackground(COL_BG);
        draw_screen(screen);
        draw_modebar(modebar);
        if (text_mode) {
            const char *hint[] = {
                "sum prod sqrt root cbrt abs exp frac pi \xE2\x86\x92 templates",
                "Tab or , next box \xC2\xB7 ) leave \xC2\xB7 / fraction \xC2\xB7 ^ power",
            };
            for (int i = 0; i < 2; i++)
                DrawTextEx(font, hint[i], (Vector2){PAD + 4, gy - 2 + (float)i * 17}, 13, 0, COL_MUTED);
        } else {
            draw_grid(FN_BUTTONS, sizeof FN_BUTTONS / sizeof *FN_BUTTONS, 6, fn_area);
            draw_grid(NUM_BUTTONS, sizeof NUM_BUTTONS / sizeof *NUM_BUTTONS, 5, num_area);
        }
        SetMouseCursor(any_hover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
        EndDrawing();
    }

    for (int i = 0; i < nhistory; i++)
        entry_free(&history[i]);
    for (size_t i = 0; i < sizeof icons / sizeof *icons; i++)
        row_free(icons[i]);
    ed_free(&ed);
    calc_free(&calc);
    if (font.texture.id != GetFontDefault().texture.id)
        UnloadFont(font);
    CloseWindow();
    return 0;
}
