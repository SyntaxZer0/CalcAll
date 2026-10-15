#include "mathedit.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n, size);
    if (!p) {
        fputs("calc: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

static int slots_for(ItemKind kind)
{
    switch (kind) {
    case IT_CHAR:
        return 0;
    case IT_POW:
    case IT_SQRT:
    case IT_ABS:
        return 1;
    case IT_FRAC:
    case IT_ROOT:
    case IT_LOGB:
        return 2;
    case IT_SUM:
    case IT_PROD:
        return 4;
    }
    return 0;
}

static Row *row_new(Item *owner)
{
    Row *r = xcalloc(1, sizeof *r);
    r->owner = owner;
    return r;
}

static Item *item_new(ItemKind kind)
{
    Item *it = xcalloc(1, sizeof *it);
    it->kind = kind;
    it->nslots = slots_for(kind);
    for (int i = 0; i < it->nslots; i++)
        it->slot[i] = row_new(it);
    return it;
}

static void item_free(Item *it)
{
    for (int i = 0; i < it->nslots; i++)
        row_free(it->slot[i]);
    free(it);
}

void row_free(Row *r)
{
    if (!r)
        return;
    for (int i = 0; i < r->len; i++)
        item_free(r->items[i]);
    free(r->items);
    free(r);
}

static void row_insert(Row *r, int pos, Item *it)
{
    if (r->len == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 8;
        Item **items = realloc(r->items, (size_t)r->cap * sizeof *items);
        if (!items) {
            fputs("calc: out of memory\n", stderr);
            exit(1);
        }
        r->items = items;
    }
    memmove(r->items + pos + 1, r->items + pos, (size_t)(r->len - pos) * sizeof *r->items);
    r->items[pos] = it;
    r->len++;
    it->row = r;
}

static Item *row_remove(Row *r, int pos)
{
    Item *it = r->items[pos];
    memmove(r->items + pos, r->items + pos + 1, (size_t)(r->len - pos - 1) * sizeof *r->items);
    r->len--;
    return it;
}

static int item_index(const Item *it)
{
    for (int i = 0; i < it->row->len; i++)
        if (it->row->items[i] == it)
            return i;
    return -1;
}

static int slot_index(const Item *owner, const Row *slot)
{
    for (int i = 0; i < owner->nslots; i++)
        if (owner->slot[i] == slot)
            return i;
    return -1;
}

static int all_empty(const Item *it)
{
    for (int i = 0; i < it->nslots; i++)
        if (it->slot[i]->len)
            return 0;
    return 1;
}

static Item *item_clone(const Item *src)
{
    Item *it = xcalloc(1, sizeof *it);
    it->kind = src->kind;
    it->closes = src->closes;
    memcpy(it->ch, src->ch, sizeof it->ch);
    it->nslots = src->nslots;
    for (int i = 0; i < it->nslots; i++) {
        it->slot[i] = row_clone(src->slot[i]);
        it->slot[i]->owner = it;
    }
    return it;
}

Row *row_clone(const Row *src)
{
    Row *r = row_new(NULL);
    for (int i = 0; i < src->len; i++)
        row_insert(r, r->len, item_clone(src->items[i]));
    return r;
}

/* ---- Editor ---------------------------------------------------------------- */

void ed_init(Editor *e)
{
    e->root = row_new(NULL);
    e->row = e->root;
    e->pos = 0;
    e->fresh = 0;
}

void ed_free(Editor *e)
{
    row_free(e->root);
    e->root = e->row = NULL;
}

void ed_clear(Editor *e)
{
    ed_free(e);
    ed_init(e);
}

int ed_is_empty(const Editor *e)
{
    return e->root->len == 0;
}

void ed_load(Editor *e, const Row *src)
{
    ed_free(e);
    e->root = row_clone(src);
    e->row = e->root;
    e->pos = e->root->len;
    e->fresh = 0;
}

static void insert_item(Editor *e, Item *it)
{
    row_insert(e->row, e->pos, it);
    e->pos++;
}

static void insert_text_into(Row *r, int *pos, const char *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s;
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        Item *it = item_new(IT_CHAR);
        for (int i = 0; i < n && s[i]; i++)
            it->ch[i] = s[i];
        row_insert(r, *pos, it);
        (*pos)++;
        s += strlen(it->ch);
    }
}

void ed_insert_text(Editor *e, const char *utf8)
{
    insert_text_into(e->row, &e->pos, utf8);
}

static int is_char(const Item *it, char c)
{
    return it->kind == IT_CHAR && it->ch[0] == c && it->ch[1] == '\0';
}

/* Can this item be part of an operand pulled into a numerator? */
static int is_operand(const Item *it)
{
    if (it->kind != IT_CHAR)
        return 1;
    unsigned char c = (unsigned char)it->ch[0];
    return isalnum(c) || c == '.' || c == '!' || c >= 0x80;
}

void ed_insert_template(Editor *e, ItemKind kind, int grab)
{
    if (kind == IT_CHAR)
        return;
    Item *it = item_new(kind);

    if (kind == IT_FRAC && grab) {
        int start = e->pos;
        for (;;) {
            if (start > 0 && is_char(e->row->items[start - 1], ')')) {
                /* (1+2)/ or sin(30)/: take the whole group */
                int depth = 0, i = start;
                while (i > 0) {
                    i--;
                    if (is_char(e->row->items[i], ')'))
                        depth++;
                    else if (is_char(e->row->items[i], '(') && --depth == 0)
                        break;
                }
                if (depth != 0)
                    break;
                start = i;
            } else if (start > 0 && is_operand(e->row->items[start - 1])) {
                start--;
            } else {
                break;
            }
        }
        for (int i = start; i < e->pos; i++) {
            Item *moved = row_remove(e->row, start);
            row_insert(it->slot[0], it->slot[0]->len, moved);
        }
        e->pos = start;
    }

    insert_item(e, it);
    int first = 0;
    if (kind == IT_SUM || kind == IT_PROD) {
        int p = 0;
        insert_text_into(it->slot[0], &p, "k");
        first = 1;   /* start in the "from" box */
    } else if (kind == IT_FRAC && it->slot[0]->len) {
        first = 1;   /* numerator was filled by grab: go to the denominator */
    }
    e->row = it->slot[first];
    e->pos = 0;
}

void ed_insert_pow(Editor *e, const char *exponent)
{
    Item *it = item_new(IT_POW);
    insert_item(e, it);
    if (exponent) {
        int p = 0;
        insert_text_into(it->slot[0], &p, exponent);
    } else {
        e->row = it->slot[0];
        e->pos = 0;
    }
}

/* ---- Text-mode typing ---------------------------------------------------------- */

static int is_letter(const Item *it)
{
    return it->kind == IT_CHAR && it->ch[1] == '\0' && isalpha((unsigned char)it->ch[0]);
}

/* Does `word` end right before pos, as a whole word? */
static int word_before(const Row *r, int pos, const char *word)
{
    int n = (int)strlen(word);
    if (pos < n)
        return 0;
    for (int i = 0; i < n; i++)
        if (!is_char(r->items[pos - n + i], word[i]))
            return 0;
    return pos == n || !is_letter(r->items[pos - n - 1]);
}

/* Is there a "(" before pos in this row that isn't closed yet? */
static int open_paren(const Row *r, int pos)
{
    int depth = 0;
    for (int i = 0; i < pos; i++) {
        if (is_char(r->items[i], '('))
            depth++;
        else if (is_char(r->items[i], ')') && depth > 0)
            depth--;
    }
    return depth > 0;
}

/* The box "," moves to, in the order the text form lists them: root(x, n)
 * has the radicand (slot 1) first. -1 when it's the last one. */
static int text_next_slot(const Item *it, int s)
{
    if (it->kind == IT_ROOT)
        return s == 1 ? 0 : -1;
    return s + 1 < it->nslots ? s + 1 : -1;
}

static Item *open_template(Editor *e, ItemKind kind, int slot)
{
    Item *it = item_new(kind);
    it->closes = CLOSE_ON_PAREN;
    insert_item(e, it);
    e->row = it->slot[slot];
    e->pos = 0;
    e->fresh = 1;
    return it;
}

/* Are all the boxes after slot s (in text order) already filled? */
static int rest_filled(const Item *it, int s)
{
    for (int n = text_next_slot(it, s); n >= 0; n = text_next_slot(it, n))
        if (it->slot[n]->len == 0)
            return 0;
    return 1;
}

static void leave(Editor *e, Item *owner)
{
    e->row = owner->row;
    e->pos = item_index(owner) + 1;
}

static const char *const WORDS[] = {"sum", "prod", "sqrt", "root", "cbrt", "abs", "exp", "frac", "pi", "theta"};

void ed_type(Editor *e, const char *ch)
{
    int fresh = e->fresh;
    e->fresh = 0;
    char c = ch[1] == '\0' ? ch[0] : 0;

    if (c == '(' && fresh) {
        /* "sum(" or "1/(": the box is already open; now ")" closes it. */
        if (e->row->owner)
            e->row->owner->closes = CLOSE_ON_PAREN;
        return;
    }

    /* "<=", ">=" and "!=" become the symbols, for checks like 3 ≤ 4. In
     * "2^3!=6" the "!" was in the exponent, so the "≠" leaves it below. */
    if (c == '=' && e->pos > 0) {
        static const char *const SYMBOL[][2] = {
            {"<", "\xE2\x89\xA4"}, {">", "\xE2\x89\xA5"}, {"!", "\xE2\x89\xA0"}};
        for (size_t i = 0; i < sizeof SYMBOL / sizeof *SYMBOL; i++)
            if (is_char(e->row->items[e->pos - 1], SYMBOL[i][0][0])) {
                item_free(row_remove(e->row, e->pos - 1));
                e->pos--;
                ch = SYMBOL[i][1];
                c = '<';   /* an operator, for what follows */
                break;
            }
    }

    /* An operator ends a denominator or exponent that was typed without
     * parentheses ("1/2+3" is a half plus three), possibly several levels. */
    while (c && strchr("+-*/%,)=<>", c) && e->row->owner &&
           e->row->owner->closes == CLOSE_ON_OPERATOR && !open_paren(e->row, e->pos) &&
           !(c == '-' && e->row->len == 0))
        leave(e, e->row->owner);

    if (c == '/') {
        ed_insert_template(e, IT_FRAC, 1);
        e->row->owner->closes = CLOSE_ON_OPERATOR;
        e->fresh = 1;
        return;
    }
    if (c == '^') {
        ed_insert_pow(e, NULL);
        e->row->owner->closes = CLOSE_ON_OPERATOR;
        e->fresh = 1;
        return;
    }

    Item *owner = e->row->owner;
    if (owner && !open_paren(e->row, e->pos)) {
        int s = slot_index(owner, e->row);
        int is_big = owner->kind == IT_SUM || owner->kind == IT_PROD;
        int next = text_next_slot(owner, s);
        if ((c == ',' || (c == '=' && is_big && s == 0)) && next >= 0) {
            e->row = owner->slot[next];
            e->pos = e->row->len;
            return;
        }
        if (c == ')' && e->pos == e->row->len && rest_filled(owner, s)) {
            leave(e, owner);
            return;
        }
    }

    ed_insert_text(e, ch);

    for (size_t w = 0; w < sizeof WORDS / sizeof *WORDS; w++) {
        const char *word = WORDS[w];
        if (!word_before(e->row, e->pos, word))
            continue;
        for (size_t i = 0; i < strlen(word); i++) {
            item_free(row_remove(e->row, e->pos - 1));
            e->pos--;
        }
        if (strcmp(word, "sum") == 0) {
            open_template(e, IT_SUM, 0);
        } else if (strcmp(word, "prod") == 0) {
            open_template(e, IT_PROD, 0);
        } else if (strcmp(word, "sqrt") == 0) {
            open_template(e, IT_SQRT, 0);
        } else if (strcmp(word, "root") == 0) {
            open_template(e, IT_ROOT, 1);
        } else if (strcmp(word, "cbrt") == 0) {
            Item *it = open_template(e, IT_ROOT, 1);
            int p = 0;
            insert_text_into(it->slot[0], &p, "3");
        } else if (strcmp(word, "abs") == 0) {
            open_template(e, IT_ABS, 0);
        } else if (strcmp(word, "frac") == 0) {
            open_template(e, IT_FRAC, 0);
        } else if (strcmp(word, "exp") == 0) {
            ed_insert_text(e, "e");
            ed_insert_pow(e, NULL);
            e->row->owner->closes = CLOSE_ON_PAREN;
            e->fresh = 1;
        } else if (strcmp(word, "theta") == 0) {
            ed_insert_text(e, "\xCE\xB8");   /* θ, for polar graphs */
        } else {
            ed_insert_text(e, "\xCF\x80");   /* pi -> π */
        }
        return;
    }
}

/* Removes "(" and ")" around a lone fraction, in r and every box inside it. */
static void drop_fraction_parens(Row *r)
{
    for (int i = 0; i < r->len; i++)
        for (int k = 0; k < r->items[i]->nslots; k++)
            drop_fraction_parens(r->items[i]->slot[k]);
    for (int i = 0; i + 2 < r->len; i++) {
        if (is_char(r->items[i], '(') && r->items[i + 1]->kind == IT_FRAC && is_char(r->items[i + 2], ')')) {
            item_free(row_remove(r, i + 2));
            item_free(row_remove(r, i));
            i = i > 0 ? i - 2 : -1;   /* ((1/3)) loses both pairs */
        }
    }
}

void ed_from_text(Editor *e, const char *text)
{
    ed_clear(e);
    for (const char *p = text; *p;) {
        unsigned char c = (unsigned char)*p;
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char one[8] = {0};
        for (int k = 0; k < n && p[k]; k++)
            one[k] = p[k];
        if (*p != ' ')
            ed_type(e, one);
        p += strlen(one);
    }
    drop_fraction_parens(e->root);
    e->row = e->root;
    e->pos = e->root->len;
    e->fresh = 0;
}

void ed_tab(Editor *e, int dir)
{
    Item *owner = e->row->owner;
    if (!owner) {
        /* At the top level, jump into the nearest template that way. */
        if (dir > 0) {
            for (int i = e->pos; i < e->row->len; i++) {
                Item *it = e->row->items[i];
                if (it->nslots) {
                    e->row = it->slot[0];
                    e->pos = e->row->len;
                    return;
                }
            }
        } else {
            for (int i = e->pos - 1; i >= 0; i--) {
                Item *it = e->row->items[i];
                if (it->nslots) {
                    e->row = it->slot[it->nslots - 1];
                    e->pos = e->row->len;
                    return;
                }
            }
        }
        return;
    }
    int s = slot_index(owner, e->row) + (dir > 0 ? 1 : -1);
    if (s >= 0 && s < owner->nslots) {
        e->row = owner->slot[s];
        e->pos = e->row->len;
    } else {
        e->row = owner->row;
        e->pos = item_index(owner) + (dir > 0 ? 1 : 0);
    }
}

/* ---- Cursor movement ---------------------------------------------------------- */

void ed_right(Editor *e)
{
    Row *r = e->row;
    if (e->pos < r->len) {
        Item *it = r->items[e->pos];
        if (it->nslots) {
            e->row = it->slot[0];
            e->pos = 0;
        } else {
            e->pos++;
        }
        return;
    }
    Item *owner = r->owner;
    if (!owner)
        return;
    int s = slot_index(owner, r);
    if (s + 1 < owner->nslots) {
        e->row = owner->slot[s + 1];
        e->pos = 0;
    } else {
        e->row = owner->row;
        e->pos = item_index(owner) + 1;
    }
}

void ed_left(Editor *e)
{
    Row *r = e->row;
    if (e->pos > 0) {
        Item *it = r->items[e->pos - 1];
        if (it->nslots) {
            e->row = it->slot[it->nslots - 1];
            e->pos = e->row->len;
        } else {
            e->pos--;
        }
        return;
    }
    Item *owner = r->owner;
    if (!owner)
        return;
    int s = slot_index(owner, r);
    if (s > 0) {
        e->row = owner->slot[s - 1];
        e->pos = e->row->len;
    } else {
        e->row = owner->row;
        e->pos = item_index(owner);
    }
}

void ed_home(Editor *e)
{
    e->row = e->root;
    e->pos = 0;
}

void ed_end(Editor *e)
{
    e->row = e->root;
    e->pos = e->root->len;
}

/* Which box is above (dir < 0) or below (dir > 0) slot s of a template? */
static int vertical_slot(const Item *it, int s, int dir)
{
    switch (it->kind) {
    case IT_FRAC:
        return dir > 0 ? (s == 0 ? 1 : -1) : (s == 1 ? 0 : -1);
    case IT_SUM:
    case IT_PROD:   /* 0, 1: "k=from" below the symbol; 2: "to" above it */
        return dir > 0 ? (s == 2 ? 1 : -1) : (s == 0 || s == 1 ? 2 : -1);
    case IT_ROOT:   /* 0: index, up and left; 1: radicand */
        return dir > 0 ? (s == 0 ? 1 : -1) : (s == 1 ? 0 : -1);
    case IT_LOGB:   /* 0: base, lowered; 1: argument */
        return dir > 0 ? (s == 1 ? 0 : -1) : (s == 0 ? 1 : -1);
    default:
        return -1;
    }
}

int ed_vertical(Editor *e, int dir)
{
    for (Row *r = e->row; r->owner; r = r->owner->row) {
        Item *owner = r->owner;
        int target = vertical_slot(owner, slot_index(owner, r), dir);
        if (target >= 0) {
            e->row = owner->slot[target];
            e->pos = e->row->len;
            return 1;
        }
    }
    return 0;
}

void ed_backspace(Editor *e)
{
    Row *r = e->row;
    if (e->pos > 0) {
        Item *it = r->items[e->pos - 1];
        if (it->nslots == 0 || all_empty(it)) {
            item_free(row_remove(r, e->pos - 1));
            e->pos--;
        } else {
            /* Step into a filled template instead of deleting all of it. */
            e->row = it->slot[it->nslots - 1];
            e->pos = e->row->len;
        }
        return;
    }
    Item *owner = r->owner;
    if (!owner)
        return;
    if (all_empty(owner)) {
        Row *parent = owner->row;
        int i = item_index(owner);
        item_free(row_remove(parent, i));
        e->row = parent;
        e->pos = i;
    } else {
        ed_left(e);
    }
}

/* ---- Serialization -------------------------------------------------------------- */

typedef struct {
    char *s;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 64;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *p = realloc(b->s, cap);
        if (!p) {
            fputs("calc: out of memory\n", stderr);
            exit(1);
        }
        b->s = p;
        b->cap = cap;
    }
    memcpy(b->s + b->len, s, n + 1);
    b->len += n;
}

static void ser_row(const Row *r, Buf *b, int *empty);

static void ser_slot(const Item *it, int i, Buf *b, int *empty)
{
    if (it->slot[i]->len == 0)
        *empty = 1;
    ser_row(it->slot[i], b, empty);
}

/* A box holding just a number or a name, like 2 or x1, needs no parentheses. */
static int simple(const Row *r)
{
    for (int i = 0; i < r->len; i++) {
        const Item *it = r->items[i];
        unsigned char c = (unsigned char)it->ch[0];
        if (it->kind != IT_CHAR || !(isalnum(c) || c == '.' || c >= 0x80))
            return 0;
    }
    return r->len > 0;
}

/* A box's contents, in parentheses unless they're simple. */
static void ser_group(const Item *it, int i, Buf *b, int *empty)
{
    int bare = simple(it->slot[i]);
    put(b, bare ? "" : "(");
    ser_slot(it, i, b, empty);
    put(b, bare ? "" : ")");
}

static void ser_item(const Item *it, Buf *b, int *empty)
{
    switch (it->kind) {
    case IT_CHAR:
        put(b, it->ch);
        break;
    case IT_FRAC:
        put(b, "(");
        ser_group(it, 0, b, empty);
        put(b, "/");
        ser_group(it, 1, b, empty);
        put(b, ")");
        break;
    case IT_POW:
        put(b, "^");
        ser_group(it, 0, b, empty);
        break;
    case IT_SQRT:
        put(b, "sqrt(");
        ser_slot(it, 0, b, empty);
        put(b, ")");
        break;
    case IT_ROOT:
        put(b, "root(");
        ser_slot(it, 1, b, empty);
        put(b, ", ");
        ser_slot(it, 0, b, empty);
        put(b, ")");
        break;
    case IT_SUM:
    case IT_PROD:
        put(b, it->kind == IT_SUM ? "sum(" : "prod(");
        if (it->slot[0]->len)
            ser_row(it->slot[0], b, empty);
        else
            put(b, "k");
        put(b, "=");
        ser_slot(it, 1, b, empty);
        put(b, ", ");
        ser_slot(it, 2, b, empty);
        put(b, ", ");
        ser_slot(it, 3, b, empty);
        put(b, ")");
        break;
    case IT_ABS:
        put(b, "abs(");
        ser_slot(it, 0, b, empty);
        put(b, ")");
        break;
    case IT_LOGB:
        put(b, "log(");
        ser_slot(it, 1, b, empty);
        put(b, ", ");
        ser_slot(it, 0, b, empty);
        put(b, ")");
        break;
    }
}

static void ser_row(const Row *r, Buf *b, int *empty)
{
    for (int i = 0; i < r->len; i++)
        ser_item(r->items[i], b, empty);
}

char *ed_serialize(const Row *r, int *empty_box)
{
    Buf b = {0};
    *empty_box = 0;
    put(&b, "");
    ser_row(r, &b, empty_box);
    return b.s;
}
