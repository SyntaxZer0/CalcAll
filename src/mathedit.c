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
        while (start > 0 && is_operand(e->row->items[start - 1]))
            start--;
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

static void ser_item(const Item *it, Buf *b, int *empty)
{
    switch (it->kind) {
    case IT_CHAR:
        put(b, it->ch);
        break;
    case IT_FRAC:
        put(b, "((");
        ser_slot(it, 0, b, empty);
        put(b, ")/(");
        ser_slot(it, 1, b, empty);
        put(b, "))");
        break;
    case IT_POW:
        put(b, "^(");
        ser_slot(it, 0, b, empty);
        put(b, ")");
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
        ser_slot(it, 0, b, empty);
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
