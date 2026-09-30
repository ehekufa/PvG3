/* ОГОРОД: значения ОгScript и подсчёт ссылок. */
#include "og_common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

OgValue og_nil(void) { OgValue v; v.t = OG_NIL; v.u.n = 0; return v; }
OgValue og_bool(int b) { OgValue v; v.t = OG_BOOL; v.u.b = !!b; return v; }
OgValue og_num(double n) { OgValue v; v.t = OG_NUM; v.u.n = n; return v; }
OgValue og_strv(OgStr *s) { OgValue v; v.t = OG_STR; v.u.s = s; return v; }
OgValue og_arrv(OgArr *a) { OgValue v; v.t = OG_ARR; v.u.a = a; return v; }
OgValue og_obj(uint32_t h) { OgValue v; v.t = OG_OBJ; v.u.h = h; return v; }
OgValue og_func(int fn) { OgValue v; v.t = OG_FUNC; v.u.fn = fn; return v; }
OgValue og_native(int id) { OgValue v; v.t = OG_NATIVE; v.u.nat = id; return v; }

OgStr *og_str_new(const char *s, int len) {
    OgStr *r = (OgStr *)malloc(sizeof(OgStr) + (size_t)len + 1);
    if (!r) return NULL;
    r->refc = 1;
    r->len = len;
    if (len > 0) memcpy(r->data, s, (size_t)len);
    r->data[len] = 0;
    return r;
}

OgStr *og_str_copy(const OgStr *s) {
    return og_str_new(s->data, s->len);
}

OgArr *og_arr_new(void) {
    OgArr *a = (OgArr *)calloc(1, sizeof(OgArr));
    if (!a) return NULL;
    a->refc = 1;
    return a;
}

void og_arr_push(OgArr *a, OgValue v) {
    if (a->len == a->cap) {
        int nc = a->cap ? a->cap * 2 : 8;
        OgValue *ni = (OgValue *)realloc(a->items, (size_t)nc * sizeof(OgValue));
        if (!ni) { og_release(v); return; }
        a->items = ni;
        a->cap = nc;
    }
    a->items[a->len++] = v; /* ссылка перешла массиву */
}

void og_retain(OgValue v) {
    if (v.t == OG_STR && v.u.s) v.u.s->refc++;
    else if (v.t == OG_ARR && v.u.a) v.u.a->refc++;
}

void og_release(OgValue v) {
    if (v.t == OG_STR && v.u.s) {
        if (--v.u.s->refc <= 0) free(v.u.s);
    } else if (v.t == OG_ARR && v.u.a) {
        if (--v.u.a->refc <= 0) {
            for (int i = 0; i < v.u.a->len; i++) og_release(v.u.a->items[i]);
            free(v.u.a->items);
            free(v.u.a);
        }
    }
}

int og_truthy(OgValue v) {
    switch (v.t) {
    case OG_NIL: return 0;
    case OG_BOOL: return v.u.b;
    case OG_NUM: return v.u.n != 0.0;
    case OG_STR: return v.u.s && v.u.s->len > 0;
    case OG_ARR: return v.u.a && v.u.a->len > 0;
    default: return 1;
    }
}

int og_equals(OgValue a, OgValue b) {
    if (a.t != b.t) {
        /* число и булево не смешиваем; строка с числом тоже не равны */
        return 0;
    }
    switch (a.t) {
    case OG_NIL: return 1;
    case OG_BOOL: return a.u.b == b.u.b;
    case OG_NUM: return a.u.n == b.u.n;
    case OG_STR:
        return a.u.s == b.u.s ||
               (a.u.s && b.u.s && a.u.s->len == b.u.s->len &&
                memcmp(a.u.s->data, b.u.s->data, (size_t)a.u.s->len) == 0);
    case OG_ARR: return a.u.a == b.u.a;
    case OG_OBJ: return a.u.h == b.u.h;
    case OG_FUNC: return a.u.fn == b.u.fn;
    case OG_NATIVE: return a.u.nat == b.u.nat;
    default: return 0;
    }
}

OgStr *og_to_str(OgValue v) {
    char buf[64];
    switch (v.t) {
    case OG_NIL: return og_str_new("null", 4);
    case OG_BOOL: return og_str_new(v.u.b ? "true" : "false", v.u.b ? 4 : 5);
    case OG_NUM: {
        double n = v.u.n;
        if (n == floor(n) && fabs(n) < 9.0e15)
            snprintf(buf, sizeof buf, "%lld", (long long)n);
        else
            snprintf(buf, sizeof buf, "%.10g", n);
        return og_str_new(buf, (int)strlen(buf));
    }
    case OG_STR: return v.u.s ? og_str_copy(v.u.s) : og_str_new("", 0);
    case OG_ARR: {
        /* Короткое представление: длина и первые элементы. */
        char tmp[96];
        int len = v.u.a ? v.u.a->len : 0;
        snprintf(tmp, sizeof tmp, "[массив:%d]", len);
        return og_str_new(tmp, (int)strlen(tmp));
    }
    case OG_OBJ: { const char *s = "<узел>"; return og_str_new(s, (int)strlen(s)); }
    case OG_FUNC: { const char *s = "<функция>"; return og_str_new(s, (int)strlen(s)); }
    case OG_NATIVE: { const char *s = "<встроенная>"; return og_str_new(s, (int)strlen(s)); }
    default: return og_str_new("?", 1);
    }
}

double og_to_num(OgValue v) {
    switch (v.t) {
    case OG_BOOL: return v.u.b;
    case OG_NUM: return v.u.n;
    case OG_STR: return v.u.s ? strtod(v.u.s->data, NULL) : 0.0;
    default: return 0.0;
    }
}
