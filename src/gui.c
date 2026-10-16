/*
 * Graphical front end in the style of a Casio "natural display" calculator,
 * drawn with raylib (https://www.raylib.com).
 *
 * The expression at the top is a tree from mathedit.h: fractions, roots,
 * powers and sums are templates with boxes to fill in. Pressing "="
 * serializes the tree to text and hands it to calc_eval(), like main.c does.
 * The result appears in the bottom right of the screen.
 *
 * Keyboard: type normally; "/" divides, "?" (Shift+/) makes a fraction from
 * the number before it, "^" makes a power. Arrows and Tab move between boxes, Up/Down also
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
#include "graph.h"
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
static int text_mode = 1;     /* TEXT: no buttons; typed names like "sum" become templates */
static int show_history;      /* the Calculate screen shows past calculations */
static float history_scroll;  /* pixels scrolled up from the newest */
static char error_text[300];  /* last error, shown until the next edit */
static float hscroll;         /* sideways scroll of a long expression */
static int any_hover;

static Row *icons[8];         /* little expressions drawn on template buttons */

/* Table mode, like a Casio's TABLE: f(x), g(x), Start, End, Step -> rows */
enum { TAB_CALC, TAB_TABLE, TAB_GRAPH };
enum { FIELD_F, FIELD_G, FIELD_START, FIELD_END, FIELD_STEP, NFIELDS };
static int tab = TAB_CALC;
static Editor fields[NFIELDS];
static float field_scroll[NFIELDS];
static int focus;                  /* field being edited in the table setup */
static int table_view;             /* showing the table instead of its setup */
static Table table;
static int sel_row, sel_col;       /* selected cell; col 0 is x, 1 is f, 2 is g */
static int table_top;              /* first row on screen */
static char table_error[300];
static char goto_text[32];         /* the table's "go to" box: a row number, or x=value */
static int goto_bad;               /* what's in it matches no row */
static Editor *cur = &ed;          /* the editor that keys and buttons type into */

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

/* Is the cursor of the editor being typed in somewhere inside it? */
static int cursor_inside(const Item *it)
{
    for (const Row *r = cur->row; r && r->owner; r = r->owner->row)
        if (r->owner == it)
            return 1;
    return 0;
}

/* The plain text of a box of characters, like the x of d/dx. */
static void row_text(const Row *r, char *buf, size_t n, const char *empty)
{
    size_t len = 0;
    buf[0] = '\0';
    for (int i = 0; i < r->len; i++) {
        const Item *it = r->items[i];
        size_t k = strlen(it->ch);
        if (it->kind != IT_CHAR || len + k + 1 > n)
            break;
        memcpy(buf + len, it->ch, k + 1);
        len += k;
    }
    if (len == 0)
        snprintf(buf, n, "%s", empty);
}

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

    case IT_INT: {
        /* ∫ with "to" above and "from" below, the body, then d and the variable */
        const char *sym = "\xE2\x88\xAB";
        float ss = s * 1.75f, ls = sub_size(s, 0.6f);
        float sw = tw(sym, ss);
        float sym_base = base - AXIS * s + 0.30f * ss;   /* centers the symbol on the axis */
        float sym_top = sym_base - 0.78f * ss;
        Box up = measure(it->slot[2], ls), lo = measure(it->slot[1], ls);
        float col = maxf(maxf(sw, up.w), lo.w) + 0.1f * s;
        float up_base = sym_top - 0.04f * s - up.desc;
        float lo_base = sym_base + 0.3f * s + maxf(lo.asc, ASC * ls);
        Box body = measure(it->slot[3], s), v = measure(it->slot[0], s);
        float dw = tw("d", s), gap = 0.18f * s;
        b.w = col + 0.08f * s + body.w + gap + dw + v.w + 0.05f * s;
        b.asc = maxf(maxf(base - up_base + up.asc, body.asc), v.asc);
        b.desc = maxf(maxf(lo_base - base + maxf(lo.desc, DESC * ls), body.desc), v.desc);
        if (draw) {
            text_at(sym, x + (col - sw) / 2, sym_base, ss, c);
            layout_row(it->slot[2], ls, 1, x + (col - up.w) / 2, up_base, c);
            layout_row(it->slot[1], ls, 1, x + (col - lo.w) / 2, lo_base, c);
            float bx = x + col + 0.08f * s;
            layout_row(it->slot[3], s, 1, bx, base, c);
            text_at("d", bx + body.w + gap, base, s, c);
            layout_row(it->slot[0], s, 1, bx + body.w + gap + dw, base, c);
        }
        break;
    }

    case IT_DIFF: {
        /* d/dx, the body in parentheses, then |x=a for the point */
        float fs = s * 0.92f, ls = sub_size(s, 0.6f), gap = 0.12f * s;
        Box v = measure(it->slot[0], fs);
        float dw = tw("d", fs);
        float fw = maxf(dw, dw + v.w) + 0.3f * s;
        float axis = base - AXIS * s;
        float nbase = axis - gap - DESC * fs, dbase = axis + th + gap + maxf(v.asc, ASC * fs);
        Box body = measure(it->slot[1], s);
        float ps = paren_size(body, s), pw = tw("(", ps);
        Box pt = measure(it->slot[2], ls);
        char var[64];
        row_text(it->slot[0], var, sizeof var, "x");
        char at[80];
        snprintf(at, sizeof at, "%s=", var);
        float aw = tw(at, ls);
        float bar_top = base - maxf(body.asc, ASC * s) - 0.05f * s;
        float bar_bottom = base + maxf(body.desc, DESC * s) + 0.1f * s;
        float sub_base = bar_bottom + 0.05f * s;
        float barx = x + fw + 2 * pw + body.w + 0.12f * s;
        /* the point is optional: d/dx(x^3) on its own is the slope at x */
        int show_point = it->slot[2]->len > 0 || cursor_inside(it);
        b.w = show_point ? barx - x + th + 0.06f * s + aw + pt.w + 0.05f * s : barx - x;
        b.asc = maxf(base - (nbase - ASC * fs), base - bar_top);
        b.desc = maxf(maxf(dbase + maxf(v.desc, DESC * fs) - base, body.desc),
                      show_point ? sub_base - base + maxf(pt.desc, DESC * ls) : 0);
        if (!show_point)
            b.asc = maxf(base - (nbase - ASC * fs), body.asc);
        if (draw) {
            DrawRectangleRec((Rectangle){x + 0.1f * s, axis, fw - 0.2f * s, th}, c);
            text_at("d", x + (fw - dw) / 2, nbase, fs, c);
            float bx = x + (fw - dw - v.w) / 2;
            text_at("d", bx, dbase, fs, c);
            layout_row(it->slot[0], fs, 1, bx + dw, dbase, c);
            float px = x + fw;
            draw_paren("(", px, base, body, ps, c);
            layout_row(it->slot[1], s, 1, px + pw, base, c);
            draw_paren(")", px + pw + body.w, base, body, ps, c);
            if (show_point) {
                DrawRectangleRec((Rectangle){barx, bar_top, th, bar_bottom - bar_top}, c);
                text_at(at, barx + th + 0.06f * s, sub_base, ls, c);
                layout_row(it->slot[2], ls, 1, barx + th + 0.06f * s + aw, sub_base, c);
            }
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
    if (cur != &ed) {   /* a table field */
        table_error[0] = '\0';
        return;
    }
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
    if (cur != &ed) {
        table_error[0] = '\0';
        return;
    }
    shown = -1;
    error_text[0] = '\0';
}

static void type_text(const char *s)
{
    begin_edit(strchr("+-*/^!%", s[0]) != NULL);
    if (strcmp(s, "(") == 0 && ed_derivative(cur))
        return;   /* d/dx( makes a derivative in KEYS mode too */
    ed_insert_text(cur, s);
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

static void copy_cell(void);
static void copy_key_point(void);

static void copy_result(void)
{
    if (tab == TAB_GRAPH) {
        copy_key_point();
        return;
    }
    if (tab == TAB_TABLE && table_view) {
        copy_cell();
        return;
    }
    int frac;
    char *s = result_text(&frac);
    if (s) {
        SetClipboardText(strncmp(s, "\xE2\x89\x88 ", 4) == 0 ? s + 4 : s);   /* drop "≈ " */
        free(s);
    }
}

/* ---- Screen -------------------------------------------------------------------- */

static int text_button(Rectangle r, const char *label, Color color, float size);

/* Past calculations as natural-display expressions, built from their text. */
static Row **hist_rows;
static int hist_rows_count;
static const char *hist_rows_first;   /* to notice the list changing under us */

static void free_hist_rows(void)
{
    for (int i = 0; i < hist_rows_count; i++)
        row_free(hist_rows[i]);
    free(hist_rows);
    hist_rows = NULL;
    hist_rows_count = 0;
}

static void update_hist_rows(void)
{
    const char *first = calc.nhistory ? calc.history[0].input : NULL;
    if (hist_rows_count == calc.nhistory && hist_rows_first == first)
        return;
    free_hist_rows();
    hist_rows = calloc((size_t)calc.nhistory + 1, sizeof *hist_rows);
    if (!hist_rows)
        exit(1);
    for (int i = 0; i < calc.nhistory; i++) {
        /* typing the text back in rebuilds the fractions, roots and sums */
        Editor t;
        ed_init(&t);
        ed_from_text(&t, calc.history[i].input);
        hist_rows[i] = row_clone(t.root);
        ed_free(&t);
    }
    hist_rows_count = calc.nhistory;
    hist_rows_first = first;
}

/* Brings back a past calculation and works it out again. */
static void reuse_history(int i)
{
    ed_load(&ed, hist_rows[i]);
    show_history = 0;
    evaluate();
}

static void draw_history(Rectangle area)
{
    update_hist_rows();
    Rectangle head = {area.x + area.width - 150, area.y + 4, 66, 22};
    if (calc.nhistory && text_button(head, "Clear", COL_CTRL, 13)) {
        calc_clear_history(&calc);
        update_hist_rows();
    }
    head.x += 72;
    head.width = 70;
    if (text_button(head, "Close", COL_FN, 13))
        show_history = 0;
    DrawTextEx(font, "History", (Vector2){area.x + PAD, area.y + 6}, 13, 0, COL_MUTED);

    Rectangle list = {area.x + 4, area.y + 30, area.width - 8, area.height - 34};
    if (calc.nhistory == 0) {
        DrawTextEx(font, "No calculations yet.", (Vector2){list.x + 10, list.y + 10}, 16, 0, COL_MUTED);
        return;
    }
    if (CheckCollisionPointRec(GetMousePosition(), list))
        history_scroll += GetMouseWheelMove() * 40;
    if (history_scroll < 0)
        history_scroll = 0;

    /* newest at the bottom, like a terminal; each is the expression, then its result */
    const float es = 22, rs = 18, gap = 10;
    float cw = tw("0", rs);
    int fit = (int)((list.width - 24) / cw);
    float y = list.y + list.height + history_scroll;
    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    for (int i = calc.nhistory - 1; i >= 0 && y > list.y; i--) {
        Box b = measure(hist_rows[i], es);
        float h = b.asc + b.desc + rs * 1.2f + gap;
        y -= h;
        Rectangle item = {list.x, y, list.width, h};
        if (CheckCollisionPointRec(GetMousePosition(), item) && CheckCollisionPointRec(GetMousePosition(), list)) {
            any_hover = 1;
            DrawRectangleRec(item, ColorBrightness(COL_SCREEN, 0.06f));
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                reuse_history(i);
                break;
            }
        }
        layout_row(hist_rows[i], es, 1, item.x + 10, y + gap / 2 + b.asc, COL_MUTED);
        char line[512];
        const char *res = calc.history[i].result;
        if ((int)strlen(res) > fit && fit > 4)   /* one line: cut a long result short */
            snprintf(line, sizeof line, "%.*s\xE2\x80\xA6", fit - 1, res);
        else
            snprintf(line, sizeof line, "%s", res);
        Vector2 t = MeasureTextEx(font, line, rs, 0);
        DrawTextEx(font, line, (Vector2){item.x + item.width - t.x - 10, y + gap / 2 + b.asc + b.desc}, rs, 0,
                   COL_TEXT);
    }
    EndScissorMode();
    float top = y - list.y;   /* stop scrolling past the oldest */
    if (top > 0 && history_scroll > 0)
        history_scroll -= top < history_scroll ? top : history_scroll;
}

static void draw_screen(Rectangle area)
{
    DrawRectangleRounded(area, 0.05f, 8, COL_SCREEN);
    if (show_history) {
        draw_history(area);
        return;
    }
    if (text_button((Rectangle){area.x + area.width - 78, area.y + 4, 70, 22}, "History", COL_FN, 13)) {
        show_history = 1;
        history_scroll = 0;
    }

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

    {"d/dx", -1, A_TEMPLATE, NULL, IT_DIFF, K_FN}, {"\xE2\x88\xAB dx", -1, A_TEMPLATE, NULL, IT_INT, K_FN},
    TXT("x", "x", K_FN), TXT("t", "t", K_FN), TXT("floor", "floor(", K_FN), TXT("mod", "%", K_FN),
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

static void table_enter(void);

static void press(const Button *b)
{
    switch (b->action) {
    case A_TEXT:
        type_text(b->text);
        break;
    case A_TEMPLATE:
        begin_edit(0);
        ed_insert_template(cur, b->kind, 0);
        break;
    case A_SQUARE:
        begin_edit(1);
        ed_insert_pow(cur, "2");
        break;
    case A_INVERSE:   /* x^-1 */
        begin_edit(1);
        ed_insert_pow(cur, "-1");
        break;
    case A_POW:
        begin_edit(1);
        ed_insert_pow(cur, NULL);
        break;
    case A_EXP:
        begin_edit(0);
        ed_insert_text(cur, "e");
        ed_insert_pow(cur, NULL);
        break;
    case A_EVAL:
        if (tab == TAB_TABLE)
            table_enter();
        else
            evaluate();
        break;
    case A_DEL:
        resume_edit();
        ed_backspace(cur);
        break;
    case A_AC:
        ed_clear(cur);
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

static void draw_modebar_buttons(Rectangle area)
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

/* The mode bar's choices, kept between runs in ~/.calcall_settings. */
static const char *settings_path(void)
{
    static char path[4096];
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    snprintf(path, sizeof path, "%s/.calcall_settings", home);
    return path;
}

static void save_settings(void)
{
    const char *path = settings_path();
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f)
        return;
    fprintf(f, "text %d\ndegrees %d\nsci %d\ndigits %d\nfraction %d\n", text_mode, calc.degrees, calc.sci,
            calc.digits, show_fraction);
    fclose(f);
}

static void load_settings(void)
{
    const char *path = settings_path();
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f)
        return;
    char key[32];
    int v;
    while (fscanf(f, "%31s %d", key, &v) == 2) {
        if (strcmp(key, "text") == 0)
            text_mode = v != 0;
        else if (strcmp(key, "degrees") == 0)
            calc.degrees = v != 0;
        else if (strcmp(key, "sci") == 0)
            calc.sci = v != 0;
        else if (strcmp(key, "digits") == 0 && v >= 1 && v <= 1000)
            calc.digits = v;
        else if (strcmp(key, "fraction") == 0)
            show_fraction = v != 0;
    }
    fclose(f);
}

static void draw_modebar(Rectangle area)
{
    int before[5] = {text_mode, calc.degrees, calc.sci, calc.digits, show_fraction};
    draw_modebar_buttons(area);
    int after[5] = {text_mode, calc.degrees, calc.sci, calc.digits, show_fraction};
    if (memcmp(before, after, sizeof before) != 0)
        save_settings();
}

/* ---- Keyboard ------------------------------------------------------------------- */

static void table_key(int key);
static void goto_type(int cp);
static void set_focus(int f);

static void graph_key(int key);

static void handle_key(int key)
{
    if (show_history && tab == TAB_CALC) {
        if (key == KEY_ESCAPE)
            show_history = 0;
        if (key != KEY_ENTER && key != KEY_KP_ENTER && key != KEY_BACKSPACE && key != KEY_DELETE)
            return;
        show_history = 0;   /* Enter or Backspace: back to the calculation */
    }
    if (tab == TAB_GRAPH) {
        graph_key(key);
        return;
    }
    if (tab == TAB_TABLE && table_view) {
        table_key(key);
        return;
    }
    int in_table = tab == TAB_TABLE;
    int showing = !in_table && shown >= 0;   /* a calculator result is on screen */
    switch (key) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        if (in_table)
            table_enter();
        else
            evaluate();
        break;
    case KEY_BACKSPACE:
    case KEY_DELETE:
        resume_edit();
        ed_backspace(cur);
        break;
    case KEY_LEFT:
        if (showing)
            ed_end(cur);
        else
            ed_left(cur);
        resume_edit();
        break;
    case KEY_RIGHT:
        if (showing)
            ed_home(cur);
        else
            ed_right(cur);
        resume_edit();
        break;
    case KEY_HOME:
        ed_home(cur);
        resume_edit();
        break;
    case KEY_END:
        ed_end(cur);
        resume_edit();
        break;
    case KEY_UP:
    case KEY_DOWN: {
        int dir = key == KEY_UP ? -1 : 1;
        /* Inside a fraction or sum, move between its boxes; otherwise replay
         * earlier calculations, or move between the table's fields. */
        if (showing || !ed_vertical(cur, dir)) {
            if (in_table)
                set_focus(focus + dir);
            else if (showing || ed_is_empty(cur))
                replay(dir);
        }
        break;
    }
    case KEY_TAB: {
        int dir = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? -1 : 1;
        if (showing)
            ed_home(cur);
        Row *row = cur->row;
        int pos = cur->pos;
        ed_tab(cur, dir);
        if (in_table && cur->row == row && cur->pos == pos)
            set_focus(focus + dir);   /* no more boxes here: next field */
        resume_edit();
        break;
    }
    case KEY_ESCAPE:
        ed_clear(cur);
        resume_edit();
        break;
    }
}

/* Text mode: one character through the editor's typing rules (ed_type). */
static void type_char_text(const char *utf8)
{
    begin_edit(utf8[1] == '\0' && strchr("+-*/^!%", utf8[0]) != NULL);
    ed_type(cur, utf8);
}

static void handle_keys(void)
{
    int mod = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
              IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    int cp;
    while ((cp = GetCharPressed()) > 0) {
        if (mod || cp == ' ')
            continue;
        if (tab == TAB_TABLE && table_view) {
            goto_type(cp);
            continue;
        }
        show_history = 0;   /* typing goes back to the calculation */
        if (cp == '/' || cp == '?') {
            /* "/" divides, as the ÷ key does; Shift+/ makes a fraction: "12?" puts 12 on top */
            begin_edit(1);
            int typing = text_mode || tab == TAB_GRAPH;
            if (cp == '?' && typing)
                ed_type(cur, "/");   /* text rules: "1?2+3" is a half plus three */
            else if (cp == '?')
                ed_insert_template(cur, IT_FRAC, 1);
            else if (typing)
                ed_type(cur, "\xC3\xB7");
            else
                ed_insert_text(cur, "/");
        } else if (text_mode || tab == TAB_GRAPH) {   /* the graph has no keypad: words make templates */
            int n;
            char utf8[8];
            const char *bytes = CodepointToUTF8(cp, &n);
            memcpy(utf8, bytes, (size_t)n);
            utf8[n] = '\0';
            type_char_text(utf8);
        } else if (cp == '^') {
            begin_edit(1);
            ed_insert_pow(cur, NULL);
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
    if (mod && IsKeyPressed(KEY_V) && !(tab == TAB_TABLE && table_view)) {
        const char *clip = GetClipboardText();
        if (clip && *clip) {
            char buf[2048];
            size_t j = 0;
            for (size_t i = 0; clip[i] && j + 1 < sizeof buf; i++)
                if (clip[i] != '\n' && clip[i] != '\r' && clip[i] != '\t')
                    buf[j++] = clip[i];
            buf[j] = '\0';
            if (text_mode || tab == TAB_GRAPH) {
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
    const int repeating[] = {KEY_BACKSPACE, KEY_DELETE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN};
    for (size_t i = 0; i < sizeof repeating / sizeof *repeating; i++)
        if (IsKeyPressedRepeat(repeating[i]))
            handle_key(repeating[i]);
}

/* ---- Table mode -------------------------------------------------------------------- */

static void set_focus(int f)
{
    focus = f < 0 ? 0 : f >= NFIELDS ? NFIELDS - 1 : f;
    cur = &fields[focus];
}

static void graph_entered(void);
static Editor *graph_editor(void);

static void set_tab(int t)
{
    tab = t;
    cur = tab == TAB_TABLE ? &fields[focus] : tab == TAB_GRAPH ? graph_editor() : &ed;
    if (tab == TAB_GRAPH)
        graph_entered();
}

/* Cell text cache: formatting thousands of values every frame would be slow. */
static char **cells;
static int cells_width = -1, cells_digits, cells_sci;

static void clear_cells(void)
{
    if (cells)
        for (int i = 0; i < table.count * 3; i++)
            free(cells[i]);
    free(cells);
    cells = NULL;
    cells_width = -1;
}

static const Value *cell_value(int row, int col, const char **error)
{
    TableRow *r = &table.rows[row];
    *error = col == 1 ? r->f_error : col == 2 ? r->g_error : NULL;
    return col == 0 ? &r->x : col == 1 ? &r->f : &r->g;
}

static const char *cell_text(int row, int col, int width)
{
    if (width != cells_width || calc.digits != cells_digits || calc.sci != cells_sci) {
        clear_cells();
        cells = calloc((size_t)table.count * 3, sizeof *cells);
        if (!cells)
            exit(1);
        cells_width = width;
        cells_digits = calc.digits;
        cells_sci = calc.sci;
    }
    char **slot = &cells[row * 3 + col];
    if (!*slot) {
        const char *error;
        const Value *v = cell_value(row, col, &error);
        *slot = error ? dup("ERROR") : calc_cell(&calc, v, width);
    }
    return *slot;
}

static void build_table(void)
{
    char *text[NFIELDS];
    int any_empty = 0;
    for (int i = 0; i < NFIELDS; i++) {
        int empty;
        text[i] = ed_serialize(fields[i].root, &empty);
        any_empty |= empty;
    }
    char err[256];
    if (any_empty) {
        snprintf(table_error, sizeof table_error, "Fill in the empty box first");
    } else if (!text[FIELD_F][0]) {
        snprintf(table_error, sizeof table_error, "Enter f(x) first");
        set_focus(FIELD_F);
    } else {
        clear_cells();
        table_free(&table);
        if (calc_table(&calc, text[FIELD_F], text[FIELD_G], text[FIELD_START], text[FIELD_END],
                       text[FIELD_STEP], &table, err, sizeof err)) {
            snprintf(table_error, sizeof table_error, "%s", err);
        } else {
            table_error[0] = '\0';
            table_view = 1;
            sel_row = 0;
            sel_col = 1;
            table_top = 0;
            goto_text[0] = '\0';
            goto_bad = 0;
        }
    }
    for (int i = 0; i < NFIELDS; i++)
        free(text[i]);
}

/* Enter in the setup: on to the next field, and from Step, show the table. */
static void table_enter(void)
{
    if (focus < FIELD_STEP)
        set_focus(focus + 1);
    else
        build_table();
}

/* Typing in the table goes to the "go to" box. */
static void goto_type(int cp)
{
    size_t n = strlen(goto_text);
    if (cp == 'X')
        cp = 'x';
    if (n + 1 >= sizeof goto_text || cp > 127 || !strchr("0123456789.-+x=", cp))
        return;
    goto_text[n] = (char)cp;
    goto_text[n + 1] = '\0';
    goto_bad = 0;
}

/*
 * Jumps to what the "go to" box names: "25" is the 25th row, and "x=2.5"
 * (or "x2.5") the row whose x is closest to 2.5. Returns 0 if nothing matches.
 */
static int goto_jump(void)
{
    char *end;
    if (goto_text[0] == 'x') {
        const char *p = goto_text + 1;
        if (*p == '=')
            p++;
        double want = strtod(p, &end);
        if (end == p || *end || table.count == 0)
            return 0;
        int best = 0;
        double best_d = HUGE_VAL;
        for (int i = 0; i < table.count; i++) {
            double d = fabs(rat_to_double(&table.rows[i].x.r) - want);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        sel_row = best;
        return 1;
    }
    long n = strtol(goto_text, &end, 10);
    if (end == goto_text || *end || n < 1 || n > table.count)
        return 0;
    sel_row = (int)n - 1;
    return 1;
}

static void table_key(int key)
{
    int cols = table.has_g ? 3 : 2;
    if (goto_text[0]) {   /* editing the "go to" box */
        switch (key) {
        case KEY_ENTER:
        case KEY_KP_ENTER:
            if (goto_jump()) {
                goto_text[0] = '\0';
                table_top = sel_row;   /* show it at the top of the list */
            } else {
                goto_bad = 1;
            }
            return;
        case KEY_BACKSPACE:
        case KEY_DELETE:
            goto_text[strlen(goto_text) - 1] = '\0';
            goto_bad = 0;
            return;
        case KEY_ESCAPE:
            goto_text[0] = '\0';
            goto_bad = 0;
            return;
        }
    }
    switch (key) {
    case KEY_UP:
        sel_row--;
        break;
    case KEY_DOWN:
        sel_row++;
        break;
    case KEY_PAGE_UP:
        sel_row -= 10;
        break;
    case KEY_PAGE_DOWN:
        sel_row += 10;
        break;
    case KEY_HOME:
        sel_row = 0;
        break;
    case KEY_END:
        sel_row = table.count - 1;
        break;
    case KEY_LEFT:
        sel_col--;
        break;
    case KEY_RIGHT:
    case KEY_TAB:
        sel_col++;
        break;
    case KEY_ESCAPE:
    case KEY_BACKSPACE:
    case KEY_ENTER:
    case KEY_KP_ENTER:
        table_view = 0;   /* back to the setup */
        return;
    }
    sel_row = sel_row < 0 ? 0 : sel_row >= table.count ? table.count - 1 : sel_row;
    sel_col = sel_col < 0 ? 0 : sel_col >= cols ? cols - 1 : sel_col;
}

/* "x", "f(1.5)" or "g(1.5)": what the selected cell is */
static void cell_label(char *buf, size_t n, int row, int col)
{
    if (col == 0)
        snprintf(buf, n, "x");
    else
        snprintf(buf, n, "%s(%s)", col == 1 ? "f" : "g", cell_text(row, 0, 12));
}

static void copy_cell(void)
{
    if (table.count == 0)
        return;
    const char *error;
    const Value *v = cell_value(sel_row, sel_col, &error);
    if (error)
        return;
    char *s = val_to_str(v, calc.digits, calc.sci);
    SetClipboardText(strncmp(s, "\xE2\x89\x88 ", 4) == 0 ? s + 4 : s);
    free(s);
}

static void draw_tabs(Rectangle area)
{
    const char *names[] = {"Calculate", "Table", "Graph"};
    float w = (area.width - 2 * GAP) / 3;
    for (int i = 0; i < 3; i++) {
        Rectangle r = {area.x + (float)i * (w + GAP), area.y, w, area.height};
        int active = tab == i;
        if (button_frame(r, active ? COL_SCREEN : COL_BG) && !active)
            set_tab(i);
        if (active)
            DrawRectangleRec((Rectangle){r.x + 10, r.y + r.height - 3, r.width - 20, 3}, COL_ACCENT);
        Vector2 t = MeasureTextEx(font, names[i], 16, 0);
        DrawTextEx(font, names[i], (Vector2){r.x + (w - t.x) / 2, r.y + (r.height - t.y) / 2 - 1}, 16, 0,
                   active ? COL_TEXT : COL_MUTED);
    }
}

/*
 * An editable expression in a box: draws it (with the cursor when focused),
 * keeps the cursor in view by scrolling sideways, and returns 1 when clicked.
 */
static int editor_box(Editor *e, float *scroll, int focused, Rectangle r, float size, const char *placeholder)
{
    int clicked = 0;
    if (CheckCollisionPointRec(GetMousePosition(), r)) {
        any_hover = 1;
        clicked = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    }
    DrawRectangleRounded(r, 0.2f, 6, ColorBrightness(COL_SCREEN, -0.25f));
    if (focused)
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, 1.5f, COL_CURSOR);

    Box b = measure(e->root, size);
    float base = r.y + (r.height - (b.asc + b.desc)) / 2 + b.asc;
    float x = r.x + 8;
    caret_ed = focused ? e : NULL;
    caret_found = 0;
    BeginScissorMode((int)r.x + 2, (int)r.y, (int)r.width - 4, (int)r.height);
    layout_row(e->root, size, 1, x - *scroll, base, COL_TEXT);
    if (placeholder && ed_is_empty(e) && !focused)
        DrawTextEx(font, placeholder, (Vector2){x, r.y + (r.height - 14) / 2}, 14, 0, COL_MUTED);
    if (caret_found && ((int)(GetTime() * 2.5)) % 2 == 0)
        DrawRectangleRec(caret, COL_CURSOR);
    EndScissorMode();
    caret_ed = NULL;

    float room = r.width - 16;
    if (caret_found) {
        float cx = caret.x + *scroll - x;
        if (cx - *scroll > room)
            *scroll = cx - room;
        if (cx - *scroll < 0)
            *scroll = cx;
    }
    if (b.w <= room)
        *scroll = 0;
    return clicked;
}

/* One input field of the table setup; clicking it gives it focus. */
static void draw_field(int f, Rectangle r, float size)
{
    if (editor_box(&fields[f], &field_scroll[f], focus == f, r, size, f == FIELD_G ? "optional" : NULL)) {
        set_focus(f);
        table_error[0] = '\0';
    }
}

static void draw_table_setup(Rectangle area)
{
    DrawRectangleRounded(area, 0.05f, 8, COL_SCREEN);
    char status[64];
    snprintf(status, sizeof status, "TABLE  %s  %d digits", calc.degrees ? "DEG" : "RAD", calc.digits);
    DrawTextEx(font, status, (Vector2){area.x + PAD, area.y + 6}, 13, 0, COL_MUTED);

    float x = area.x + PAD, w = area.width - 2 * PAD, y = area.y + 24;
    const char *labels[] = {"f(x)=", "g(x)="};
    for (int f = FIELD_F; f <= FIELD_G; f++) {
        float lw = tw(labels[f], 22) + 6;
        Box b = measure(fields[f].root, 26);
        float h = maxf(34, b.asc + b.desc + 10);
        text_at(labels[f], x, y + h / 2 + 8, 22, COL_TEXT);
        draw_field(f, (Rectangle){x + lw, y, w - lw, h}, 26);
        y += h + 6;
    }

    /* Start, End, Step, and the button that builds the table, side by side */
    const char *names[] = {"Start", "End", "Step"};
    float cw = (w - 3 * GAP) / 3.9f, fh = 0;
    for (int f = FIELD_START; f <= FIELD_STEP; f++) {
        Box b = measure(fields[f].root, 20);
        fh = maxf(fh, maxf(28, b.asc + b.desc + 8));
    }
    for (int i = 0; i < 3; i++) {
        float cx = x + (float)i * (cw + GAP);
        DrawTextEx(font, names[i], (Vector2){cx + 2, y}, 13, 0, COL_MUTED);
        draw_field(FIELD_START + i, (Rectangle){cx, y + 15, cw, fh}, 20);
    }
    float bx = x + 3 * (cw + GAP);
    if (text_button((Rectangle){bx, y + 15, x + w - bx, fh}, "Table", COL_ACCENT, 16))
        build_table();
    y += 15 + fh + 6;

    Rectangle msg = {x, y, w, area.y + area.height - 6 - y};
    if (table_error[0])
        draw_message(table_error, msg, COL_ERROR);
    else if (msg.height >= 16)
        DrawTextEx(font, "Enter: next field \xC2\xB7 Enter on Step shows the table", (Vector2){x, y + 2}, 13, 0,
                   COL_MUTED);
}

static void draw_table_view(Rectangle area)
{
    DrawRectangleRounded(area, 0.03f, 8, COL_SCREEN);
    int cols = table.has_g ? 3 : 2;
    const float size = 18, row_h = 26, detail_h = 74;
    float x = area.x + PAD, w = area.width - 2 * PAD;
    float colw = w / (float)cols;
    int chars = (int)((colw - 14) / tw("0", size));
    if (chars < 4)
        chars = 4;

    const char *heads[] = {"x", "f(x)", "g(x)"};
    float y = area.y + 8;
    for (int c = 0; c < cols; c++) {
        Vector2 t = MeasureTextEx(font, heads[c], 16, 0);
        DrawTextEx(font, heads[c], (Vector2){x + (float)(c + 1) * colw - t.x - 8, y + 4}, 16, 0, COL_MUTED);
    }
    y += row_h;
    DrawRectangleRec((Rectangle){x, y - 2, w, 1}, COL_BOX);

    Rectangle list = {x, y, w, area.y + area.height - detail_h - y};
    int visible = (int)(list.height / row_h);
    if (visible < 1)
        visible = 1;
    if (CheckCollisionPointRec(GetMousePosition(), list))
        table_top -= (int)GetMouseWheelMove() * 3;
    if (sel_row < table_top)   /* keep the selection on screen when it moves */
        table_top = sel_row;
    if (sel_row >= table_top + visible)
        table_top = sel_row - visible + 1;
    if (table_top > table.count - visible)
        table_top = table.count - visible;
    if (table_top < 0)
        table_top = 0;

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    for (int i = 0; i < visible && table_top + i < table.count; i++) {
        int row = table_top + i;
        float ry = list.y + (float)i * row_h;
        if (row % 2)
            DrawRectangleRec((Rectangle){x, ry, w, row_h}, ColorBrightness(COL_SCREEN, 0.04f));
        if (row == sel_row)
            DrawRectangleRec((Rectangle){x, ry, w, row_h}, ColorBrightness(COL_SCREEN, 0.1f));
        for (int c = 0; c < cols; c++) {
            Rectangle cell = {x + (float)c * colw, ry, colw, row_h};
            if (CheckCollisionPointRec(GetMousePosition(), cell)) {
                any_hover = 1;
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    sel_row = row;
                    sel_col = c;
                }
            }
            if (row == sel_row && c == sel_col)
                DrawRectangleLinesEx(cell, 1.5f, COL_CURSOR);
            const char *text = cell_text(row, c, chars);
            Vector2 t = MeasureTextEx(font, text, size, 0);
            Color color = strcmp(text, "ERROR") == 0 ? COL_ERROR : c == 0 ? COL_MUTED : COL_TEXT;
            DrawTextEx(font, text, (Vector2){cell.x + colw - t.x - 8, ry + (row_h - t.y) / 2}, size, 0, color);
        }
    }
    EndScissorMode();

    /* The selected cell in full, like the bottom line of a Casio's table. */
    float dy = area.y + area.height - detail_h;
    DrawRectangleRec((Rectangle){x, dy, w, 1}, COL_BOX);
    char label[64];
    cell_label(label, sizeof label, sel_row, sel_col);
    char head[96];
    snprintf(head, sizeof head, "%s  \xC2\xB7  row %d of %d", label, sel_row + 1, table.count);
    DrawTextEx(font, head, (Vector2){x, dy + 6}, 14, 0, COL_MUTED);

    /* The "go to" box: type a row number (or x=value) and press Enter. */
    float gw = 150, gx = x + w - gw;
    Rectangle gbox = {gx, dy + 3, gw, 19};
    DrawRectangleRounded(gbox, 0.3f, 6, ColorBrightness(COL_SCREEN, -0.25f));
    if (goto_text[0])
        DrawRectangleRoundedLinesEx(gbox, 0.3f, 6, 1.2f, goto_bad ? COL_ERROR : COL_CURSOR);
    const char *gl = "Go to ";
    DrawTextEx(font, gl, (Vector2){gx - tw(gl, 13), dy + 6}, 13, 0, COL_MUTED);
    if (goto_text[0]) {
        DrawTextEx(font, goto_text, (Vector2){gx + 6, dy + 5}, 14, 0, goto_bad ? COL_ERROR : COL_TEXT);
        if (((int)(GetTime() * 2.5)) % 2 == 0)
            DrawRectangleRec((Rectangle){gx + 7 + tw(goto_text, 14), dy + 6, 1.5f, 14}, COL_CURSOR);
    } else {
        DrawTextEx(font, "row or x=value", (Vector2){gx + 6, dy + 6}, 13, 0, COL_MUTED);
    }
    Rectangle detail = {x, dy + 22, w, detail_h - 28};
    const char *error;
    const Value *v = cell_value(sel_row, sel_col, &error);
    if (error) {
        draw_message(error, detail, COL_ERROR);
    } else {
        char *s = val_to_str(v, calc.digits, calc.sci);
        draw_text_result(s, detail, COL_TEXT);
        free(s);
    }
}

/* ---- Graph mode ------------------------------------------------------------------- */

#define GROWS GRAPH_MAX_ROWS
#define MAX_SAMPLES 4096
#define MAX_KEYPOINTS 64

static Editor grows[GROWS];        /* the expression list */
static float grow_scroll[GROWS];
static int ghidden[GROWS];         /* curve switched off with its color dot */
static int gempty[GROWS];          /* line still has an empty box */
static int ngrows = 1, gfocus;
static Editor grange[GROWS][2];    /* t or θ range of parametric and polar lines */
static float grange_scroll[GROWS][2];
static int gsub;                   /* 0: the line itself; 1, 2: its range boxes */
static Graph graph;
static char *graph_sig;            /* all lines' text, to notice edits */
static double view_x, view_y, view_scale = 50;   /* centre, in graph units; pixels per unit */
static Rectangle grect;            /* where the graph is drawn */
static int window_widened;
static int sel_curve = -1;         /* curve whose key points are shown */
static float list_scroll;
static int panning, pan_moved, slider_drag = -1;
static Vector2 pan_from;

static double sample_x[MAX_SAMPLES];
static double samples[GROWS][MAX_SAMPLES];
static int sampled[GROWS];
static int nsamples;

typedef struct {
    double x, y, lo, hi;
    PointKind kind;
    int row, other;
} KeyPoint;
static KeyPoint kps[MAX_KEYPOINTS];
static int nkps;
static int kp_selected;            /* kp_sel holds a refined key point */
static int kp_point = -1;          /* or a point of a points line, by index */
static KeyPoint kp_sel;
static char *kp_x, *kp_y;

static const Color CURVE_COLORS[] = {
    {235, 95, 85, 255},  {80, 150, 235, 255},  {95, 195, 105, 255}, {170, 125, 245, 255},
    {250, 155, 60, 255}, {70, 205, 205, 255},  {235, 115, 195, 255}, {205, 200, 90, 255},
};

static Color curve_color(int row)
{
    return CURVE_COLORS[row % (int)(sizeof CURVE_COLORS / sizeof *CURVE_COLORS)];
}

static Editor *graph_editor(void)
{
    return gsub ? &grange[gfocus][gsub - 1] : &grows[gfocus];
}

static void set_gsub(int row, int sub)
{
    gfocus = row;
    gsub = sub;
    cur = graph_editor();
}

/* Parametric and polar lines go from 0 to 2π unless their range is changed. */
static void reset_range(int i)
{
    for (int k = 0; k < 2; k++) {
        ed_clear(&grange[i][k]);
        ed_insert_text(&grange[i][k], k ? "2\xCF\x80" : "0");
    }
}

static void set_gfocus(int f)
{
    gfocus = f < 0 ? 0 : f >= ngrows ? ngrows - 1 : f;
    gsub = 0;
    cur = &grows[gfocus];
    if (graph.count > gfocus && graph.rows[gfocus].plots)
        sel_curve = gfocus;
}

static void clear_key_point(void)
{
    kp_selected = 0;
    kp_point = -1;
    free(kp_x);
    free(kp_y);
    kp_x = kp_y = NULL;
}

/* Graphs need room: widen a narrow window the first time. */
static void graph_entered(void)
{
    if (window_widened)
        return;
    window_widened = 1;
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    int w = 1100, h = GetScreenHeight() > 760 ? GetScreenHeight() : 760;
    if (mw > 0 && w > mw * 9 / 10)
        w = mw * 9 / 10;
    if (mh > 0 && h > mh * 85 / 100)
        h = mh * 85 / 100;
    if (GetScreenWidth() >= w)
        return;
    SetWindowSize(w, h);
    /* Keep the wider window on the screen. */
    Vector2 pos = GetWindowPosition(), mpos = GetMonitorPosition(mon);
    float x = pos.x, y = pos.y;
    if (mw > 0 && x + (float)w > mpos.x + (float)mw)
        x = mpos.x + (float)(mw - w) / 2;
    if (mh > 0 && y + (float)h > mpos.y + (float)mh)
        y = mpos.y + (float)(mh - h) / 2;
    if (x != pos.x || y != pos.y)
        SetWindowPosition((int)x, (int)y);
}

static void add_grow(const char *text)
{
    if (ngrows >= GROWS)
        return;
    ed_clear(&grows[ngrows]);
    ed_insert_text(&grows[ngrows], text);
    reset_range(ngrows);
    ghidden[ngrows] = 0;
    ngrows++;
}

static void remove_grow(int i)
{
    if (ngrows <= 1)
        return;
    /* Rotate the removed editor to the end so no two slots share one. */
    Editor gone = grows[i];
    memmove(&grows[i], &grows[i + 1], (size_t)(GROWS - i - 1) * sizeof *grows);
    grows[GROWS - 1] = gone;
    ed_clear(&grows[GROWS - 1]);
    for (int k = 0; k < 2; k++) {
        Editor r = grange[i][k];
        for (int j = i; j < GROWS - 1; j++)
            grange[j][k] = grange[j + 1][k];
        grange[GROWS - 1][k] = r;
    }
    reset_range(GROWS - 1);
    memmove(&ghidden[i], &ghidden[i + 1], (size_t)(GROWS - i - 1) * sizeof *ghidden);
    memmove(&grow_scroll[i], &grow_scroll[i + 1], (size_t)(GROWS - i - 1) * sizeof *grow_scroll);
    ngrows--;
    if (sel_curve == i)
        sel_curve = -1;
    else if (sel_curve > i)
        sel_curve--;
    set_gfocus(gfocus >= i && gfocus > 0 ? gfocus - 1 : gfocus);
}

/* Re-reads every line. Cheap, so it runs every frame and picks up any change. */
static void graph_update(void)
{
    char *texts[GROWS], *ranges[2 * GROWS];
    size_t total = 1;
    for (int i = 0; i < ngrows; i++) {
        texts[i] = ed_serialize(grows[i].root, &gempty[i]);
        total += strlen(texts[i]) + 1;
        for (int k = 0; k < 2; k++) {
            int empty;
            ranges[2 * i + k] = ed_serialize(grange[i][k].root, &empty);
            total += strlen(ranges[2 * i + k]) + 1;
        }
    }
    char *sig = malloc(total);
    if (!sig)
        exit(1);
    sig[0] = '\0';
    for (int i = 0; i < ngrows; i++) {
        strcat(sig, texts[i]);
        strcat(sig, "\n");
        strcat(sig, ranges[2 * i]);
        strcat(sig, "\n");
        strcat(sig, ranges[2 * i + 1]);
        strcat(sig, "\n");
    }
    if (!graph_sig || strcmp(sig, graph_sig) != 0) {
        clear_key_point();   /* an edit makes a refined point stale */
        free(graph_sig);
        graph_sig = sig;
    } else {
        free(sig);
    }
    graph_build(&graph, &calc, texts, ranges, ngrows);
    for (int i = 0; i < ngrows; i++) {
        free(texts[i]);
        free(ranges[2 * i]);
        free(ranges[2 * i + 1]);
    }
    if (sel_curve >= graph.count || (sel_curve >= 0 && !graph.rows[sel_curve].plots))
        sel_curve = -1;
}

static float to_sx(double x)
{
    return grect.x + grect.width / 2 + (float)((x - view_x) * view_scale);
}

static float to_sy(double y)
{
    return grect.y + grect.height / 2 - (float)((y - view_y) * view_scale);
}

static double to_wx(float px)
{
    return view_x + (px - (grect.x + grect.width / 2)) / view_scale;
}

static double to_wy(float py)
{
    return view_y - (py - (grect.y + grect.height / 2)) / view_scale;
}

/* Gridline spacing: 1, 2 or 5 times a power of ten, about `raw` apart. */
static double nice_step(double raw, int *sub)
{
    double p = pow(10, floor(log10(raw))), m = raw / p;
    if (m <= 1) {
        *sub = 5;
        return p;
    }
    if (m <= 2) {
        *sub = 4;
        return 2 * p;
    }
    if (m <= 5) {
        *sub = 5;
        return 5 * p;
    }
    *sub = 5;
    return 10 * p;
}

static void format_number(char *buf, size_t n, double v, double step)
{
    if (fabs(v) < step * 1e-6)
        v = 0;
    double a = fabs(v);
    if (a != 0 && (a >= 1e6 || a < 1e-4))
        snprintf(buf, n, "%.3g", v);
    else
        snprintf(buf, n, "%.*f", step >= 1 ? 0 : (int)ceil(-log10(step) - 1e-9), v);
}

/* A short "(x, y)" for tooltips. */
static void format_point(char *buf, size_t n, double x, double y)
{
    char xs[32], ys[32];
    /* less than a hundredth of a pixel from zero is zero */
    snprintf(xs, sizeof xs, "%.4g", fabs(x) * view_scale < 0.01 ? 0 : x);
    snprintf(ys, sizeof ys, "%.4g", fabs(y) * view_scale < 0.01 ? 0 : y);
    snprintf(buf, n, "(%s, %s)", xs, ys);
}

static void tooltip(const char *text, float px, float py)
{
    Vector2 t = MeasureTextEx(font, text, 15, 0);
    float x = px + 10, y = py - t.y - 12;
    if (x + t.x + 12 > grect.x + grect.width)
        x = px - t.x - 22;
    if (y < grect.y)
        y = py + 12;
    DrawRectangleRounded((Rectangle){x, y, t.x + 12, t.y + 8}, 0.3f, 6, (Color){12, 13, 16, 230});
    DrawTextEx(font, text, (Vector2){x + 6, y + 4}, 15, 0, COL_TEXT);
}

static void draw_gridlines(void)
{
    int sub;
    double step = nice_step(85 / view_scale, &sub);
    double minor = step / sub;
    double x0 = to_wx(grect.x), x1 = to_wx(grect.x + grect.width);
    double y0 = to_wy(grect.y + grect.height), y1 = to_wy(grect.y);
    Color c_minor = {255, 255, 255, 10}, c_major = {255, 255, 255, 28}, c_axis = {220, 222, 230, 170};

    for (double x = ceil(x0 / minor) * minor; x <= x1; x += minor)
        DrawLineV((Vector2){to_sx(x), grect.y}, (Vector2){to_sx(x), grect.y + grect.height}, c_minor);
    for (double y = ceil(y0 / minor) * minor; y <= y1; y += minor)
        DrawLineV((Vector2){grect.x, to_sy(y)}, (Vector2){grect.x + grect.width, to_sy(y)}, c_minor);
    for (double x = ceil(x0 / step) * step; x <= x1; x += step)
        DrawLineV((Vector2){to_sx(x), grect.y}, (Vector2){to_sx(x), grect.y + grect.height}, c_major);
    for (double y = ceil(y0 / step) * step; y <= y1; y += step)
        DrawLineV((Vector2){grect.x, to_sy(y)}, (Vector2){grect.x + grect.width, to_sy(y)}, c_major);

    /* Axes, and numbers along them (kept on screen when an axis isn't). */
    float ax = to_sx(0), ay = to_sy(0);
    if (ax >= grect.x && ax <= grect.x + grect.width)
        DrawLineEx((Vector2){ax, grect.y}, (Vector2){ax, grect.y + grect.height}, 1.5f, c_axis);
    if (ay >= grect.y && ay <= grect.y + grect.height)
        DrawLineEx((Vector2){grect.x, ay}, (Vector2){grect.x + grect.width, ay}, 1.5f, c_axis);
    float ly = ay + 4 < grect.y + 4 ? grect.y + 4 : ay + 4 > grect.y + grect.height - 18 ? grect.y + grect.height - 18 : ay + 4;
    float lx = ax - 6;
    char buf[32];
    for (double x = ceil(x0 / step) * step; x <= x1; x += step) {
        if (fabs(x) < step / 2)
            continue;
        format_number(buf, sizeof buf, x, step);
        Vector2 t = MeasureTextEx(font, buf, 13, 0);
        DrawTextEx(font, buf, (Vector2){to_sx(x) - t.x / 2, ly}, 13, 0, COL_MUTED);
    }
    for (double y = ceil(y0 / step) * step; y <= y1; y += step) {
        if (fabs(y) < step / 2)
            continue;
        format_number(buf, sizeof buf, y, step);
        Vector2 t = MeasureTextEx(font, buf, 13, 0);
        float x = lx - t.x;
        if (x < grect.x + 4)
            x = grect.x + 4;
        if (x + t.x > grect.x + grect.width - 4)
            x = grect.x + grect.width - 4 - t.x;
        DrawTextEx(font, buf, (Vector2){x, to_sy(y) - t.y / 2}, 13, 0, COL_MUTED);
    }
    if (ax >= grect.x && ax <= grect.x + grect.width && ay >= grect.y && ay <= grect.y + grect.height)
        DrawTextEx(font, "0", (Vector2){ax - 12, ay + 4}, 13, 0, COL_MUTED);
}

/*
 * Is the curve continuous between two samples? Halving the interval toward
 * the bigger change shrinks the gap for a continuous curve, but a jump
 * (floor) or an asymptote (1/x) keeps it large.
 */
static int continuous(const GraphRow *r, double x0, double y0, double x1, double y1)
{
    double gap = fabs(y1 - y0);
    for (int k = 0; k < 14; k++) {
        double xm = (x0 + x1) / 2, ym = graph_y(r, xm);
        if (isnan(ym))
            return 0;
        if (fabs(ym - y0) > fabs(y1 - ym)) {
            x1 = xm;
            y1 = ym;
        } else {
            x0 = xm;
            y0 = ym;
        }
    }
    return fabs(y1 - y0) < gap * 0.25;
}

static void draw_curve(int row, float thick)
{
    const GraphRow *r = &graph.rows[row];
    Color col = curve_color(row);
    float lim = grect.height * 4;
    int pen = 0;
    float px0 = 0, py0 = 0;
    for (int i = 0; i < nsamples; i++) {
        double y = samples[row][i];
        if (isnan(y)) {
            pen = 0;
            continue;
        }
        float px = grect.x + (float)i, py = to_sy(y);
        if (py < grect.y - lim)
            py = grect.y - lim;
        if (py > grect.y + grect.height + lim)
            py = grect.y + grect.height + lim;
        if (pen && (fabsf(py - py0) < 40 ||
                    continuous(r, sample_x[i - 1], samples[row][i - 1], sample_x[i], y)))
            DrawLineEx((Vector2){px0, py0}, (Vector2){px, py}, thick, col);
        pen = 1;
        px0 = px;
        py0 = py;
    }
}

static void add_key_point(PointKind kind, double x, double y, double lo, double hi, int row, int other)
{
    /* One dot per spot: the minimum at (0, -2) is also the y-intercept. */
    for (int i = 0; i < nkps; i++)
        if (fabs(kps[i].x - x) * view_scale < 1 && fabs(kps[i].y - y) * view_scale < 1)
            return;
    if (nkps < MAX_KEYPOINTS)
        kps[nkps++] = (KeyPoint){x, y, lo, hi, kind, row, other};
}

/* f, or f - g for a crossing */
static double key_f(int row, int other, double x)
{
    double y = graph_y(&graph.rows[row], x);
    return other < 0 ? y : y - graph_y(&graph.rows[other], x);
}

/* Roots (other < 0) or crossings with curve `other`, from the samples. */
static void find_sign_changes(int row, int other)
{
    for (int i = 1; i < nsamples; i++) {
        double a = sample_x[i - 1], b = sample_x[i];
        double fa = samples[row][i - 1] - (other < 0 ? 0 : samples[other][i - 1]);
        double fb = samples[row][i] - (other < 0 ? 0 : samples[other][i]);
        if (isnan(fa) || isnan(fb) || fb == 0 || (fa < 0) == (fb < 0))
            continue;
        double lo = a, hi = b, flo = fa;
        for (int k = 0; k < 60; k++) {
            double m = (lo + hi) / 2, fm = key_f(row, other, m);
            if (isnan(fm))
                break;
            if ((fm < 0) == (flo < 0)) {
                lo = m;
                flo = fm;
            } else {
                hi = m;
            }
        }
        double m = (lo + hi) / 2, fm = key_f(row, other, m);
        /* A sign change through infinity (1/x at 0) isn't a root. */
        if (!isnan(fm) && fabs(fm) <= 1e-7 * fmax(1, fmin(fabs(fa), fabs(fb))))
            add_key_point(other < 0 ? PT_ROOT : PT_CROSS, m, other < 0 ? 0 : graph_y(&graph.rows[row], m), a, b,
                          row, other);
    }
}

static void find_key_points(void)
{
    nkps = 0;
    if (sel_curve < 0 || !sampled[sel_curve])
        return;
    int row = sel_curve;
    const GraphRow *r = &graph.rows[row];
    find_sign_changes(row, -1);
    for (int i = 0; i < nsamples; i++)   /* roots that touch zero exactly at a sample */
        if (samples[row][i] == 0)
            add_key_point(PT_ROOT, sample_x[i], 0, sample_x[i > 0 ? i - 1 : i],
                          sample_x[i + 1 < nsamples ? i + 1 : i], row, -1);

    /* Turning points: refine with a golden-section search. */
    for (int i = 1; i + 1 < nsamples; i++) {
        double a = samples[row][i - 1], b = samples[row][i], c = samples[row][i + 1];
        if (isnan(a) || isnan(b) || isnan(c))
            continue;
        int is_max = b > a && b >= c, is_min = b < a && b <= c;
        if (!is_max && !is_min)
            continue;
        double lo = sample_x[i - 1], hi = sample_x[i + 1];
        const double g = 0.6180339887498949;
        for (int k = 0; k < 80; k++) {
            double m1 = hi - g * (hi - lo), m2 = lo + g * (hi - lo);
            double f1 = graph_y(r, m1), f2 = graph_y(r, m2);
            if ((f1 > f2) == is_max)
                hi = m2;
            else
                lo = m1;
        }
        double x = (lo + hi) / 2;
        add_key_point(is_max ? PT_MAX : PT_MIN, x, graph_y(r, x), sample_x[i - 1], sample_x[i + 1], row, -1);
    }

    double y0 = graph_y(r, 0);
    if (!isnan(y0) && sample_x[0] <= 0 && sample_x[nsamples - 1] >= 0)
        add_key_point(PT_YINT, 0, y0, 0, 0, row, -1);

    for (int other = 0; other < graph.count; other++)
        if (other != row && sampled[other])
            find_sign_changes(row, other);
}

static const char *kind_name(PointKind k)
{
    switch (k) {
    case PT_ROOT:
        return "Root";
    case PT_MAX:
        return "Maximum";
    case PT_MIN:
        return "Minimum";
    case PT_CROSS:
        return "Intersection";
    default:
        return "y-intercept";
    }
}

/* Clicking a key point: pin it down with the exact engine. */
static void select_key_point(const KeyPoint *k)
{
    clear_key_point();
    kp_sel = *k;
    kp_selected = 1;
    if (graph_exact_point(&calc, &graph, k->row, k->other, k->kind, k->lo, k->hi, &kp_x, &kp_y) != 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "\xE2\x89\x88 %.15g", k->x);
        kp_x = dup(buf);
        snprintf(buf, sizeof buf, "\xE2\x89\x88 %.15g", k->y);
        kp_y = dup(buf);
    }
}

static void copy_key_point(void)
{
    if (!kp_selected)
        return;
    size_t n = strlen(kp_x) + strlen(kp_y) + 8;
    char *s = malloc(n);
    if (!s)
        exit(1);
    snprintf(s, n, "(%s, %s)", kp_x, kp_y);
    SetClipboardText(s);
    free(s);
}

/* A segment of a dashed line: on for 8 pixels, off for 6, by screen position. */
static int dash_on(float x, float y)
{
    return (int)((x + y) / 7) % 2 == 0;
}

/* x = g(y): one sample per pixel row, with the same gap test as curves. */
static void draw_sideways(int row, float thick)
{
    const GraphRow *r = &graph.rows[row];
    Color col = curve_color(row);
    int pen = 0;
    float px0 = 0, py0 = 0;
    double y0 = 0, x0 = 0;
    for (float py = grect.y; py <= grect.y + grect.height; py += 1) {
        double y = to_wy(py), x = graph_x_of_y(r, y);
        if (isnan(x)) {
            pen = 0;
            continue;
        }
        float px = to_sx(x);
        if (pen && fabsf(px - px0) < 40)
            DrawLineEx((Vector2){px0, py0}, (Vector2){px, py}, thick, col);
        else if (pen) {
            /* a big step: connect only if halving keeps shrinking the gap */
            double ya = y0, xa = x0, yb = y, xb = x, gap = fabs(x - x0);
            int ok = 1;
            for (int k = 0; k < 14 && ok; k++) {
                double ym = (ya + yb) / 2, xm = graph_x_of_y(r, ym);
                if (isnan(xm))
                    ok = 0;
                else if (fabs(xm - xa) > fabs(xb - xm)) {
                    yb = ym;
                    xb = xm;
                } else {
                    ya = ym;
                    xa = xm;
                }
            }
            if (ok && fabs(xb - xa) < gap * 0.25)
                DrawLineEx((Vector2){px0, py0}, (Vector2){px, py}, thick, col);
        }
        pen = 1;
        px0 = px;
        py0 = py;
        x0 = x;
        y0 = y;
    }
}

/* The edge of "y < f(x)": an ordinary curve, dashed when the edge isn't included. */
static void draw_boundary(int row, int dashed)
{
    const GraphRow *r = &graph.rows[row];
    Color col = curve_color(row);
    int pen = 0;
    float px0 = 0, py0 = 0;
    for (int i = 0; i < nsamples; i++) {
        double x = sample_x[i], y = fexpr_eval(r->fy, &x);
        if (isnan(y)) {
            pen = 0;
            continue;
        }
        float px = grect.x + (float)i, py = to_sy(y);
        py = py < grect.y - 2000 ? grect.y - 2000 : py > grect.y + grect.height + 2000 ? grect.y + grect.height + 2000 : py;
        if (pen && fabsf(py - py0) < 60 && (!dashed || dash_on(px, py)))
            DrawLineEx((Vector2){px0, py0}, (Vector2){px, py}, 2.5f, col);
        pen = 1;
        px0 = px;
        py0 = py;
    }
}

/*
 * Equations and inequalities on a grid of 4-pixel cells: shade the cells
 * inside an inequality, and trace where the value crosses zero with
 * marching squares (each cell's corners say which edges the curve crosses).
 */
static void draw_region(int row, int shade, int contour, int dashed)
{
    const GraphRow *r = &graph.rows[row];
    const int cell = 4;
    int nx = (int)(grect.width / cell) + 2, ny = (int)(grect.height / cell) + 2;
    double *v = malloc((size_t)nx * (size_t)ny * sizeof *v);
    if (!v)
        return;
    for (int j = 0; j < ny; j++) {
        double wy = to_wy(grect.y + (float)(j * cell));
        for (int i = 0; i < nx; i++)
            v[j * nx + i] = graph_d(r, to_wx(grect.x + (float)(i * cell)), wy);
    }
    Color col = curve_color(row);

    if (shade) {
        Color fill = {col.r, col.g, col.b, 55};
        for (int j = 0; j + 1 < ny; j++) {
            int run = -1;
            for (int i = 0; i <= nx - 1; i++) {
                int in = 0;
                if (i + 1 < nx) {
                    double a = v[j * nx + i], b = v[j * nx + i + 1], c = v[(j + 1) * nx + i], d = v[(j + 1) * nx + i + 1];
                    in = !isnan(a) && !isnan(b) && !isnan(c) && !isnan(d) && graph_inside(r, (a + b + c + d) / 4);
                }
                if (in && run < 0)
                    run = i;
                if (!in && run >= 0) {   /* one rectangle for each run of cells */
                    DrawRectangle((int)grect.x + run * cell, (int)grect.y + j * cell, (i - run) * cell, cell, fill);
                    run = -1;
                }
            }
        }
    }

    if (contour) {
        /* which pairs of cell edges the curve joins, for each of the 16 sign patterns
         * (edges: 0 top, 1 right, 2 bottom, 3 left; -1 ends the list) */
        static const signed char SEG[16][5] = {
            {-1}, {3, 0, -1}, {0, 1, -1}, {3, 1, -1}, {1, 2, -1}, {3, 0, 1, 2, -1}, {0, 2, -1}, {3, 2, -1},
            {2, 3, -1}, {0, 2, -1}, {0, 1, 2, 3, -1}, {1, 2, -1}, {1, 3, -1}, {0, 1, -1}, {3, 0, -1}, {-1},
        };
        for (int j = 0; j + 1 < ny; j++) {
            for (int i = 0; i + 1 < nx; i++) {
                double c[4] = {v[j * nx + i], v[j * nx + i + 1], v[(j + 1) * nx + i + 1], v[(j + 1) * nx + i]};
                if (isnan(c[0]) || isnan(c[1]) || isnan(c[2]) || isnan(c[3]))
                    continue;
                int idx = (c[0] > 0) | (c[1] > 0) << 1 | (c[2] > 0) << 2 | (c[3] > 0) << 3;
                if (idx == 0 || idx == 15)
                    continue;
                float x0 = grect.x + (float)(i * cell), y0 = grect.y + (float)(j * cell);
                /* where the value crosses zero along each edge */
                const int ends[4][2] = {{0, 1}, {1, 2}, {3, 2}, {0, 3}};
                const float ex[4][2] = {{0, 1}, {1, 1}, {0, 1}, {0, 0}}, ey[4][2] = {{0, 0}, {0, 1}, {1, 1}, {0, 1}};
                Vector2 p[4];
                for (int e = 0; e < 4; e++) {
                    double a = c[ends[e][0]], b = c[ends[e][1]];
                    float t = a == b ? 0.5f : (float)(a / (a - b));
                    p[e] = (Vector2){x0 + cell * (ex[e][0] + (ex[e][1] - ex[e][0]) * t),
                                     y0 + cell * (ey[e][0] + (ey[e][1] - ey[e][0]) * t)};
                }
                double big = fmax(fmax(fabs(c[0]), fabs(c[1])), fmax(fabs(c[2]), fabs(c[3])));
                for (int k = 0; SEG[idx][k] >= 0; k += 2) {
                    Vector2 a = p[SEG[idx][k]], b = p[SEG[idx][k + 1]];
                    /* across a pole (1/x = y) the sign flips through infinity: skip it */
                    double mid = graph_d(r, to_wx((a.x + b.x) / 2), to_wy((a.y + b.y) / 2));
                    if (isnan(mid) || fabs(mid) > big)
                        continue;
                    if (!dashed || dash_on(a.x, a.y))
                        DrawLineEx(a, b, 2.5f, col);
                }
            }
        }
    }
    free(v);
}

/* Parametric and polar curves, refined until neighbouring points are close;
 * a gap that won't close however finely it's split is a break. */
static void param_segment(const GraphRow *r, Color col, double t0, Vector2 p0, double t1, Vector2 p1, int depth)
{
    float margin = 200;
    if ((p0.x < grect.x - margin && p1.x < grect.x - margin) ||
        (p0.x > grect.x + grect.width + margin && p1.x > grect.x + grect.width + margin) ||
        (p0.y < grect.y - margin && p1.y < grect.y - margin) ||
        (p0.y > grect.y + grect.height + margin && p1.y > grect.y + grect.height + margin))
        return;   /* entirely off screen */
    float dx = p1.x - p0.x, dy = p1.y - p0.y;
    if (dx * dx + dy * dy < 16) {
        DrawLineEx(p0, p1, 2.5f, col);
        return;
    }
    if (depth == 0)
        return;
    double tm = (t0 + t1) / 2, x, y;
    if (!graph_param(r, tm, &x, &y))
        return;
    Vector2 pm = {to_sx(x), to_sy(y)};
    param_segment(r, col, t0, p0, tm, pm, depth - 1);
    param_segment(r, col, tm, pm, t1, p1, depth - 1);
}

static void draw_param(int row)
{
    const GraphRow *r = &graph.rows[row];
    const int n = 400;
    double x, y;
    int ok0 = graph_param(r, r->tmin, &x, &y);
    Vector2 p0 = {to_sx(x), to_sy(y)};
    for (int i = 1; i <= n; i++) {
        double t0 = r->tmin + (r->tmax - r->tmin) * (i - 1) / n, t1 = r->tmin + (r->tmax - r->tmin) * i / n;
        int ok1 = graph_param(r, t1, &x, &y);
        Vector2 p1 = {to_sx(x), to_sy(y)};
        if (ok0 && ok1)
            param_segment(r, curve_color(row), t0, p0, t1, p1, 10);
        ok0 = ok1;
        p0 = p1;
    }
}

static void zoom_at(float px, float py, double factor)
{
    double wx = to_wx(px), wy = to_wy(py);
    view_scale *= factor;
    if (view_scale < 1e-6)
        view_scale = 1e-6;
    if (view_scale > 1e12)
        view_scale = 1e12;
    view_x = wx - (px - (grect.x + grect.width / 2)) / view_scale;
    view_y = wy + (py - (grect.y + grect.height / 2)) / view_scale;
}

static void draw_graph(Rectangle area)
{
    grect = area;
    DrawRectangleRec(area, ColorBrightness(COL_SCREEN, -0.15f));
    Vector2 m = GetMousePosition();
    int inside = CheckCollisionPointRec(m, area);

    /* zoom buttons, top right */
    Rectangle zb[3];
    for (int i = 0; i < 3; i++)
        zb[i] = (Rectangle){area.x + area.width - 40, area.y + 10 + (float)i * 36, 30, 30};
    int over_buttons = CheckCollisionPointRec(m, (Rectangle){zb[0].x, zb[0].y, 30, 102});

    /* panning and zooming */
    if (inside && !over_buttons && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && slider_drag < 0) {
        panning = 1;
        pan_moved = 0;
        pan_from = m;
    }
    if (panning) {
        Vector2 d = {m.x - pan_from.x, m.y - pan_from.y};
        if (fabsf(d.x) + fabsf(d.y) > 3)
            pan_moved = 1;
        if (pan_moved) {
            view_x -= d.x / view_scale;
            view_y += d.y / view_scale;
            pan_from = m;
        }
    }
    if (inside && GetMouseWheelMove() != 0)
        zoom_at(m.x, m.y, pow(1.15, GetMouseWheelMove()));

    /* sample every curve once per pixel column */
    nsamples = (int)area.width < MAX_SAMPLES ? (int)area.width : MAX_SAMPLES;
    for (int i = 0; i < nsamples; i++)
        sample_x[i] = to_wx(area.x + (float)i);
    for (int row = 0; row < graph.count; row++) {
        sampled[row] = graph.rows[row].plots && !ghidden[row];
        if (sampled[row])
            for (int i = 0; i < nsamples; i++)
                samples[row][i] = graph_y(&graph.rows[row], sample_x[i]);
    }

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    draw_gridlines();
    for (int row = 0; row < graph.count; row++) {   /* shaded regions first, under the curves */
        const GraphRow *r = &graph.rows[row];
        if (r->kind != GR_INEQ || ghidden[row])
            continue;
        if (r->fy) {
            /* y < f(x): shade each pixel column from the edge, for a smooth edge */
            Color col = curve_color(row), fill = {col.r, col.g, col.b, 55};
            for (int i = 0; i < nsamples; i++) {
                double yb = fexpr_eval(r->fy, &sample_x[i]);
                if (isnan(yb))
                    continue;
                float py = to_sy(yb), top = grect.y, bottom = grect.y + grect.height;
                py = py < top ? top : py > bottom ? bottom : py;
                if (r->boundary_flip)
                    DrawRectangleRec((Rectangle){grect.x + (float)i, top, 1, py - top}, fill);
                else
                    DrawRectangleRec((Rectangle){grect.x + (float)i, py, 1, bottom - py}, fill);
            }
        } else {
            draw_region(row, 1, 1, r->op == '<' || r->op == '>');
        }
    }
    for (int row = 0; row < graph.count; row++) {
        const GraphRow *r = &graph.rows[row];
        if (ghidden[row])
            continue;
        if (sampled[row])
            draw_curve(row, row == sel_curve ? 3.5f : 2.5f);
        else if (r->kind == GR_INEQ && r->fy)
            draw_boundary(row, r->op == '<' || r->op == '>');
        else if (r->kind == GR_IMPLICIT)
            draw_region(row, 0, 1, 0);
        else if (r->kind == GR_XOFY)
            draw_sideways(row, 2.5f);
        else if (r->kind == GR_PARAM || r->kind == GR_POLAR)
            draw_param(row);
    }

    /* points, on top */
    int hover_point_row = -1, hover_point = -1;
    for (int row = 0; row < graph.count; row++) {
        const GraphRow *r = &graph.rows[row];
        if (r->kind != GR_POINTS || ghidden[row])
            continue;
        for (int k = 0; k < r->npoints; k++) {
            if (isnan(r->px[k]) || isnan(r->py[k]))
                continue;
            Vector2 p = {to_sx(r->px[k]), to_sy(r->py[k])};
            DrawCircleV(p, 7, (Color){20, 22, 26, 255});
            DrawCircleV(p, 5.5f, curve_color(row));
            if (fabsf(p.x - m.x) < 9 && fabsf(p.y - m.y) < 9) {
                hover_point_row = row;
                hover_point = k;
            }
        }
    }

    /* key points of the selected curve */
    find_key_points();
    int hover_kp = -1;
    for (int i = 0; i < nkps; i++) {
        float px = to_sx(kps[i].x), py = to_sy(kps[i].y);
        if (fabsf(px - m.x) < 8 && fabsf(py - m.y) < 8)
            hover_kp = i;
        DrawCircleV((Vector2){px, py}, 5.5f, (Color){20, 22, 26, 255});
        DrawCircleV((Vector2){px, py}, 4, (Color){175, 178, 188, 255});
    }
    if (kp_selected && kp_point < 0) {
        float px = to_sx(kp_sel.x), py = to_sy(kp_sel.y);
        DrawCircleV((Vector2){px, py}, 7, curve_color(kp_sel.row));
        DrawCircleV((Vector2){px, py}, 3.5f, COL_TEXT);
    }

    /* tracing: a dot on the curve nearest the mouse */
    int trace_row = -1;
    double trace_x = 0, trace_y = 0;
    if (inside && !over_buttons && !pan_moved && hover_kp < 0) {
        float best = 12;
        double wx = to_wx(m.x);
        for (int row = 0; row < graph.count; row++) {
            if (!sampled[row])
                continue;
            double y = graph_y(&graph.rows[row], wx);
            if (!isnan(y) && fabsf(to_sy(y) - m.y) < best) {
                best = fabsf(to_sy(y) - m.y);
                trace_row = row;
                trace_x = wx;
                trace_y = y;
            }
        }
    }
    char buf[96];
    if (hover_point >= 0) {
        any_hover = 1;
        const GraphRow *r = &graph.rows[hover_point_row];
        format_point(buf, sizeof buf, r->px[hover_point], r->py[hover_point]);
        tooltip(buf, to_sx(r->px[hover_point]), to_sy(r->py[hover_point]));
    } else if (hover_kp >= 0) {
        any_hover = 1;
        char pt[80];
        format_point(pt, sizeof pt, kps[hover_kp].x, kps[hover_kp].y);
        snprintf(buf, sizeof buf, "%s %s", kind_name(kps[hover_kp].kind), pt);
        tooltip(buf, to_sx(kps[hover_kp].x), to_sy(kps[hover_kp].y));
    } else if (trace_row >= 0) {
        DrawCircleV((Vector2){to_sx(trace_x), to_sy(trace_y)}, 5, curve_color(trace_row));
        format_point(buf, sizeof buf, trace_x, trace_y);
        tooltip(buf, to_sx(trace_x), to_sy(trace_y));
    }

    /* a click (not a drag): pick a key point, a curve, or nothing */
    if (panning && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (!pan_moved && inside && !over_buttons) {
            if (hover_point >= 0) {
                /* a point: show its coordinates worked out exactly */
                const GraphRow *r = &graph.rows[hover_point_row];
                clear_key_point();
                kp_sel = (KeyPoint){r->px[hover_point], r->py[hover_point], 0, 0, PT_ROOT, hover_point_row, -1};
                kp_selected = 1;
                kp_point = hover_point;
                if (graph_exact_coords(&calc, &graph, hover_point_row, hover_point, &kp_x, &kp_y) != 0) {
                    snprintf(buf, sizeof buf, "%.15g", kp_sel.x);
                    kp_x = dup(buf);
                    snprintf(buf, sizeof buf, "%.15g", kp_sel.y);
                    kp_y = dup(buf);
                }
            } else if (hover_kp >= 0) {
                select_key_point(&kps[hover_kp]);
            } else if (trace_row >= 0) {
                sel_curve = trace_row;
                gfocus = trace_row;
                cur = &grows[gfocus];
                clear_key_point();
            } else {
                sel_curve = -1;
                clear_key_point();
            }
        }
        panning = 0;
    }

    /* the selected key point in full, bottom left */
    if (kp_selected) {
        char lines[3][160];
        snprintf(lines[0], sizeof lines[0], "%s", kp_point >= 0 ? "Point" : kind_name(kp_sel.kind));
        /* "x = 2", or "x ≈ 1.414..." for an approximate value */
        int ax = strncmp(kp_x, "\xE2\x89\x88 ", 4) == 0, ay = strncmp(kp_y, "\xE2\x89\x88 ", 4) == 0;
        snprintf(lines[1], sizeof lines[1], "x %s %s", ax ? "\xE2\x89\x88" : "=", kp_x + (ax ? 4 : 0));
        snprintf(lines[2], sizeof lines[2], "y %s %s", ay ? "\xE2\x89\x88" : "=", kp_y + (ay ? 4 : 0));
        float w = 0;
        for (int i = 0; i < 3; i++)
            w = maxf(w, MeasureTextEx(font, lines[i], i ? 16 : 14, 0).x);
        Rectangle box = {area.x + 10, area.y + area.height - 74, w + 20, 64};
        DrawRectangleRounded(box, 0.15f, 6, (Color){12, 13, 16, 235});
        DrawRectangleRec((Rectangle){box.x, box.y + 8, 3, box.height - 16}, curve_color(kp_sel.row));
        DrawTextEx(font, lines[0], (Vector2){box.x + 10, box.y + 6}, 14, 0, COL_MUTED);
        DrawTextEx(font, lines[1], (Vector2){box.x + 10, box.y + 24}, 16, 0, COL_TEXT);
        DrawTextEx(font, lines[2], (Vector2){box.x + 10, box.y + 43}, 16, 0, COL_TEXT);
    }
    EndScissorMode();

    const char *zl[] = {"+", "\xE2\x88\x92", "\xE2\x8C\x82"};
    for (int i = 0; i < 3; i++) {
        if (text_button(zb[i], zl[i], COL_FN, 18)) {
            if (i == 2) {
                view_x = view_y = 0;
                view_scale = 50;
            } else {
                zoom_at(area.x + area.width / 2, area.y + area.height / 2, i == 0 ? 1.5 : 1 / 1.5);
            }
        }
    }
}

/* A slider under a line like "a = 2"; dragging it rewrites the line. */
static void draw_slider(int i, Rectangle r)
{
    GraphRow *g = &graph.rows[i];
    double v = g->value;
    double lo = v < -10 ? floor(v) : -10, hi = v > 10 ? ceil(v) : 10;
    float t = (float)((v - lo) / (hi - lo));
    Rectangle track = {r.x + 30, r.y + r.height / 2 - 2, r.width - 60, 4};
    char buf[64];
    format_number(buf, sizeof buf, lo, 1);
    DrawTextEx(font, buf, (Vector2){r.x, r.y + 3}, 12, 0, COL_MUTED);
    format_number(buf, sizeof buf, hi, 1);
    DrawTextEx(font, buf, (Vector2){track.x + track.width + 6, r.y + 3}, 12, 0, COL_MUTED);
    DrawRectangleRounded(track, 1, 4, COL_BOX);
    DrawRectangleRounded((Rectangle){track.x, track.y, track.width * t, 4}, 1, 4, curve_color(i));
    Vector2 knob = {track.x + track.width * t, track.y + 2};
    DrawCircleV(knob, 8, curve_color(i));
    DrawCircleV(knob, 4, COL_TEXT);

    Vector2 m = GetMousePosition();
    Rectangle hit = {track.x - 8, r.y, track.width + 16, r.height};
    if (CheckCollisionPointRec(m, hit)) {
        any_hover = 1;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            slider_drag = i;
    }
    if (slider_drag == i) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            slider_drag = -1;
            return;
        }
        double nt = (m.x - track.x) / track.width;
        nt = nt < 0 ? 0 : nt > 1 ? 1 : nt;
        double step = pow(10, floor(log10((hi - lo) / 100)));
        double nv = round((lo + nt * (hi - lo)) / step) * step;
        char line[96];
        snprintf(line, sizeof line, "%s=%.*f", g->name, step >= 1 ? 0 : (int)ceil(-log10(step) - 1e-9), nv);
        if (strcmp(line + strlen(g->name) + 1, "-0") == 0)
            snprintf(line, sizeof line, "%s=0", g->name);
        ed_clear(&grows[i]);
        ed_insert_text(&grows[i], line);
    }
}

static int has_definition(const char *name)
{
    for (int i = 0; i < graph.count; i++)
        if (graph.rows[i].name && strcmp(graph.rows[i].name, name) == 0)
            return 1;
    return 0;
}

static void draw_list(Rectangle area)
{
    DrawRectangleRec(area, COL_SCREEN);
    if (CheckCollisionPointRec(GetMousePosition(), area))
        list_scroll -= GetMouseWheelMove() * 30;
    if (list_scroll < 0)
        list_scroll = 0;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    float y = area.y + 8 - list_scroll, x = area.x + 8, w = area.width - 16;
    char buf[160];
    for (int i = 0; i < ngrows; i++) {
        GraphRow *g = i < graph.count ? &graph.rows[i] : NULL;
        Box b = measure(grows[i].root, 22);
        float h = maxf(38, b.asc + b.desc + 10);

        /* color dot: click to show or hide the curve */
        Vector2 dot = {x + 10, y + h / 2};
        int draws = g && (g->plots || g->kind == GR_XOFY || g->kind == GR_IMPLICIT || g->kind == GR_INEQ ||
                          g->kind == GR_PARAM || g->kind == GR_POLAR || g->kind == GR_POINTS);
        if (draws) {
            if (ghidden[i])
                DrawCircleLines((int)dot.x, (int)dot.y, 7, curve_color(i));
            else
                DrawCircleV(dot, 7, curve_color(i));
            if (CheckCollisionPointCircle(GetMousePosition(), dot, 10)) {
                any_hover = 1;
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                    ghidden[i] = !ghidden[i];
            }
        } else {
            snprintf(buf, sizeof buf, "%d", i + 1);
            DrawTextEx(font, buf, (Vector2){x + 5, y + h / 2 - 7}, 13, 0, COL_MUTED);
        }

        Rectangle field = {x + 24, y, w - 24 - (ngrows > 1 ? 28 : 0), h};
        const char *ph = i == ngrows - 1 && ngrows == 1 ? "y = x^2, x^2 + y^2 = 1, (1, 2), ..." : NULL;
        if (editor_box(&grows[i], &grow_scroll[i], gfocus == i && gsub == 0, field, 22, ph)) {
            set_gfocus(i);
            clear_key_point();
        }
        if (ngrows > 1) {
            Rectangle del = {field.x + field.width + 4, y + h / 2 - 12, 24, 24};
            if (text_button(del, "\xC3\x97", COL_SCREEN, 18)) {
                remove_grow(i);
                break;   /* the list changed under us; draw it next frame */
            }
        }
        y += h + 4;

        if (!g)
            continue;
        int show_error = g->kind == GR_ERROR && !(gfocus == i && gempty[i]);
        if (g->kind == GR_PARAM || g->kind == GR_POLAR) {
            /* "t from [0] to [2π]" */
            const char *var = g->kind == GR_PARAM ? "t" : "\xCE\xB8";
            snprintf(buf, sizeof buf, "%s from", var);
            DrawTextEx(font, buf, (Vector2){field.x + 2, y + 7}, 14, 0, COL_MUTED);
            float bx = field.x + tw(buf, 14) + 10, bw = 74;
            for (int k = 0; k < 2; k++) {
                Rectangle rb = {bx, y, bw, 28};
                if (editor_box(&grange[i][k], &grange_scroll[i][k], gfocus == i && gsub == k + 1, rb, 16, NULL))
                    set_gsub(i, k + 1);
                bx += bw + 6;
                if (k == 0) {
                    DrawTextEx(font, "to", (Vector2){bx, y + 7}, 14, 0, COL_MUTED);
                    bx += tw("to", 14) + 6;
                }
            }
            y += 32;
        }
        if (g->kind == GR_SLIDER) {
            draw_slider(i, (Rectangle){field.x, y, field.width, 22});
            y += 26;
        } else if (g->kind == GR_VALUE || g->kind == GR_VAR) {
            snprintf(buf, sizeof buf, "= %.10g", g->value);
            Vector2 t = MeasureTextEx(font, buf, 15, 0);
            DrawTextEx(font, buf, (Vector2){field.x + field.width - t.x - 6, y}, 15, 0, COL_MUTED);
            y += 20;
        }
        const char *const *unknown;
        int nu = graph_unknowns(g, &unknown);
        if (nu > 0) {
            DrawTextEx(font, "add slider:", (Vector2){field.x + 2, y + 5}, 13, 0, COL_MUTED);
            float cx = field.x + 92;
            for (int k = 0; k < nu; k++) {
                if (eval_is_reserved(unknown[k]) || has_definition(unknown[k]) ||
                    strcmp(unknown[k], "x") == 0 || strcmp(unknown[k], "y") == 0 || strcmp(unknown[k], "t") == 0 ||
                    strcmp(unknown[k], "r") == 0 || strcmp(unknown[k], "\xCE\xB8") == 0)
                    continue;   /* the graph's own variables */
                float cw = tw(unknown[k], 15) + 18;
                if (text_button((Rectangle){cx, y, cw, 22}, unknown[k], COL_TOGGLE, 15)) {
                    snprintf(buf, sizeof buf, "%s=1", unknown[k]);
                    add_grow(buf);
                }
                cx += cw + 6;
            }
            y += 26;
        }
        if (show_error) {
            snprintf(buf, sizeof buf, "%s", g->error);
            char *at = strstr(buf, " (at column");   /* too long for the list */
            if (at)
                *at = '\0';
            DrawTextEx(font, buf, (Vector2){field.x + 2, y}, 13, 0, COL_ERROR);
            y += 18;
        }
        y += 4;
    }
    if (ngrows < GROWS) {
        Rectangle add = {x + 24, y + 2, 120, 26};
        if (text_button(add, "+ add line", COL_FN, 14)) {
            add_grow("");
            set_gfocus(ngrows - 1);
        }
        y += 34;
    }
    EndScissorMode();
    float content = y + list_scroll - area.y;
    if (list_scroll > content - area.height + 20)
        list_scroll = content - area.height + 20 > 0 ? content - area.height + 20 : 0;
}

static void draw_graph_tab(Rectangle area)
{
    graph_update();
    if (area.width >= 700) {   /* side by side */
        float lw = 320;
        draw_list((Rectangle){area.x, area.y, lw, area.height});
        draw_graph((Rectangle){area.x + lw + GAP, area.y, area.width - lw - GAP, area.height});
    } else {                   /* the list above the graph */
        float lh = area.height * 0.38f;
        draw_list((Rectangle){area.x, area.y, area.width, lh});
        draw_graph((Rectangle){area.x, area.y + lh + GAP, area.width, area.height - lh - GAP});
    }
}

static void graph_key(int key)
{
    if (gsub && (key == KEY_ENTER || key == KEY_KP_ENTER || key == KEY_TAB)) {
        set_gsub(gfocus, gsub == 1 ? 2 : 0);   /* from -> to -> back to the line */
        return;
    }
    if (gsub && (key == KEY_UP || key == KEY_DOWN)) {
        set_gfocus(gfocus + (key == KEY_UP ? -1 : 1));
        return;
    }
    if (gsub && (key == KEY_BACKSPACE || key == KEY_DELETE)) {
        ed_backspace(cur);
        return;
    }
    switch (key) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        /* next line, starting a new one after the last */
        if (gfocus == ngrows - 1 && !ed_is_empty(cur) && ngrows < GROWS)
            add_grow("");
        set_gfocus(gfocus + 1);
        break;
    case KEY_BACKSPACE:
    case KEY_DELETE:
        if (ed_is_empty(cur) && ngrows > 1) {
            remove_grow(gfocus);
            ed_end(cur);
        } else {
            ed_backspace(cur);
        }
        break;
    case KEY_LEFT:
        ed_left(cur);
        break;
    case KEY_RIGHT:
        ed_right(cur);
        break;
    case KEY_HOME:
        ed_home(cur);
        break;
    case KEY_END:
        ed_end(cur);
        break;
    case KEY_UP:
    case KEY_DOWN: {
        int dir = key == KEY_UP ? -1 : 1;
        if (!ed_vertical(cur, dir))
            set_gfocus(gfocus + dir);
        break;
    }
    case KEY_TAB: {
        int dir = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? -1 : 1;
        Row *row = cur->row;
        int pos = cur->pos;
        ed_tab(cur, dir);
        if (cur->row == row && cur->pos == pos)
            set_gfocus(gfocus + dir);
        break;
    }
    case KEY_ESCAPE:
        if (kp_selected)
            clear_key_point();
        else if (sel_curve >= 0)
            sel_curve = -1;
        else
            ed_clear(cur);
        break;
    }
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
        "\xE2\x87\x94\xE2\x86\x92\xC2\xB7\xE2\x86\x90\xE2\x86\x91\xE2\x86\x93\xE2\x8C\x82\xCE\xB8\xE2\x89\xA4\xE2\x89\xA5\xE2\x89\xA0\xE2\x88\xAB";
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
        TraceLog(LOG_WARNING, "CalcAll: %s not found, symbols like pi won't draw", FONT_FILE);
        return GetFontDefault();
    }
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(480, 800, "CalcAll");
    SetWindowMinSize(380, 660);
    SetExitKey(KEY_NULL);   /* Esc clears instead of quitting */
    SetTargetFPS(60);
    font = load_font();
    /* The window's icon (Linux and Windows; on macOS the app bundle's icon is used). */
    const char *icon = TextFormat("%sassets/logo/icon-256.png", GetApplicationDirectory());
    if (!FileExists(icon))
        icon = "assets/logo/icon-256.png";
    if (FileExists(icon)) {
        Image img = LoadImage(icon);
        SetWindowIcon(img);
        UnloadImage(img);
    }
    calc_init(&calc);
    calc_use_store(&calc, calc_default_store());   /* bring back saved definitions */
    calc_use_history(&calc, calc_default_history());
    load_settings();
    ed_init(&ed);
    for (int i = 0; i < NFIELDS; i++)
        ed_init(&fields[i]);
    ed_insert_text(&fields[FIELD_START], "1");   /* Casio's defaults */
    ed_insert_text(&fields[FIELD_END], "5");
    ed_insert_text(&fields[FIELD_STEP], "1");
    for (int i = 0; i < GROWS; i++) {
        ed_init(&grows[i]);
        ed_init(&grange[i][0]);
        ed_init(&grange[i][1]);
        reset_range(i);
    }
    make_icons();

    while (!WindowShouldClose()) {
        handle_keys();

        float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
        int in_view = tab == TAB_TABLE && table_view;
        int no_keys = text_mode || in_view || tab == TAB_GRAPH;   /* typing only: room for a hint */
        float keys_h = no_keys ? 40 : H * 0.6f;
        Rectangle tabs = {PAD, PAD - 4, W - 2 * PAD, 30};
        float top = tabs.y + tabs.height + 6;
        Rectangle screen = {PAD, top, W - 2 * PAD, H - keys_h - MODEBAR_H - 2 * PAD - top};
        Rectangle modebar = {PAD, screen.y + screen.height + PAD, W - 2 * PAD > 700 ? 700 : W - 2 * PAD,
                             MODEBAR_H};
        float gy = modebar.y + MODEBAR_H + PAD;
        float fn_h = (H - gy - PAD) * 0.58f;   /* 6 rows of functions over 4 of numbers */
        Rectangle fn_area = {PAD, gy, W - 2 * PAD, fn_h};
        Rectangle num_area = {PAD, gy + fn_h + PAD, W - 2 * PAD, H - gy - fn_h - 2 * PAD};

        any_hover = 0;
        BeginDrawing();
        ClearBackground(COL_BG);
        draw_tabs(tabs);
        if (tab == TAB_CALC)
            draw_screen(screen);
        else if (tab == TAB_GRAPH)
            draw_graph_tab(screen);
        else if (table_view)
            draw_table_view(screen);
        else
            draw_table_setup(screen);
        draw_modebar(modebar);
        if (no_keys) {
            const char *text_hint[] = {
                "sum prod int sqrt root cbrt abs exp frac pi, d/dx( \xE2\x86\x92 templates",
                "Tab next box \xC2\xB7 ) leave \xC2\xB7 / \xC3\xB7 \xC2\xB7 shift / fraction \xC2\xB7 ^ power",
            };
            const char *table_hint[] = {
                "\xE2\x86\x91\xE2\x86\x93 \xE2\x86\x90\xE2\x86\x92 move \xC2\xB7 wheel scrolls \xC2\xB7 click a cell",
                "type a row (or x=2.5) + Enter: jump there \xC2\xB7 Esc: back to the setup",
            };
            const char *graph_hint[] = {
                "drag: pan \xC2\xB7 scroll: zoom \xC2\xB7 click a curve, then a dot, for key points",
                "Enter: new line \xC2\xB7 y < x, x^2 + y^2 = 1, (cos t, sin t), r = cos(theta), (1, 2), a = 2",
            };
            const char **hint = tab == TAB_GRAPH ? graph_hint : in_view ? table_hint : text_hint;
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
    for (int i = 0; i < NFIELDS; i++)
        ed_free(&fields[i]);
    clear_cells();
    table_free(&table);
    free_hist_rows();
    for (int i = 0; i < GROWS; i++) {
        ed_free(&grows[i]);
        ed_free(&grange[i][0]);
        ed_free(&grange[i][1]);
    }
    graph_free(&graph);
    clear_key_point();
    free(graph_sig);
    calc_free(&calc);
    if (font.texture.id != GetFontDefault().texture.id)
        UnloadFont(font);
    CloseWindow();
    return 0;
}
