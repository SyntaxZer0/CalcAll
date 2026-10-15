#ifndef MATHEDIT_H
#define MATHEDIT_H

/*
 * A "natural display" expression editor, like on Casio calculators.
 *
 * An expression is a Row: a list of Items. An Item is either one character
 * or a template such as a fraction, whose boxes ("slots") are Rows of their
 * own. So 1/2 + 3 is
 *
 *     Row: [FRAC(num: Row[1], den: Row[2]), '+', '3']
 *
 * The cursor is a position between two items of some Row. This file only
 * knows about structure; drawing it is the GUI's job, and turning it into
 * text the calculator engine understands is ed_serialize().
 */

typedef enum {
    IT_CHAR,   /* one character, in ch */
    IT_FRAC,   /* slots: numerator, denominator */
    IT_POW,    /* slots: exponent (raised after whatever precedes it) */
    IT_SQRT,   /* slots: radicand */
    IT_ROOT,   /* slots: index, radicand */
    IT_SUM,    /* slots: variable, from, to, body */
    IT_PROD,   /* slots: variable, from, to, body */
    IT_ABS,    /* slots: contents */
    IT_LOGB    /* slots: base, argument */
} ItemKind;

typedef struct Row Row;
typedef struct Item Item;

struct Item {
    ItemKind kind;
    char ch[8];       /* IT_CHAR: one UTF-8 character */
    int nslots;
    Row *slot[4];
    Row *row;         /* the row this item sits in */
};

struct Row {
    Item **items;
    int len, cap;
    Item *owner;      /* template whose slot this is; NULL for the top row */
};

typedef struct {
    Row *root;
    Row *row;   /* row the cursor is in */
    int pos;    /* cursor sits before row->items[pos] */
} Editor;

void ed_init(Editor *e);
void ed_free(Editor *e);
void ed_clear(Editor *e);
int  ed_is_empty(const Editor *e);

/* Inserts text one character at a time (no templates). */
void ed_insert_text(Editor *e, const char *utf8);

/*
 * Inserts a template and moves the cursor into its first box to fill in.
 * For IT_FRAC with `grab`, the number or name just before the cursor moves
 * into the numerator, as when typing "12/" on a Casio. Sums and products
 * start with the variable "k" filled in.
 */
void ed_insert_template(Editor *e, ItemKind kind, int grab);

/* x^exponent: with text, fills the exponent and leaves the cursor after it. */
void ed_insert_pow(Editor *e, const char *exponent);

void ed_left(Editor *e);
void ed_right(Editor *e);
void ed_home(Editor *e);
void ed_end(Editor *e);
/* Moves between boxes stacked vertically (numerator/denominator, limits of
 * a sum). dir < 0 is up. Returns 0 if there's nowhere to go. */
int  ed_vertical(Editor *e, int dir);
void ed_backspace(Editor *e);

/* Copies an expression into the editor, cursor at the end. */
void ed_load(Editor *e, const Row *src);
Row *row_clone(const Row *src);
void row_free(Row *r);

/*
 * Text for the calculator engine, e.g. "((1)/(2))+3" (malloc'd). Sets
 * *empty_box if any box was left empty.
 */
char *ed_serialize(const Row *r, int *empty_box);

#endif
