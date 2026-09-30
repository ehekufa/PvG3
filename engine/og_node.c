/* POSIX: strdup, strtok_r, strcasecmp */
#define _POSIX_C_SOURCE 200809L
/* ОГОРОД: реализация узлов сцены. */
#include "og_node.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "og_image.h"

static int g_seq;

OgNode *og_node_new(int type, const char *name) {
    OgNode *n = (OgNode *)calloc(1, sizeof(OgNode));
    if (!n) return NULL;
    snprintf(n->name, sizeof n->name, "%s", name ? name : "Узел");
    n->type = type;
    n->visible = 1;
    n->alpha = 1.0f;
    n->spr = -1;
    n->fsize = 2;
    n->color = 0xFFFFFFFFu;
    n->fill = 0xFFFFFFFFu;
    n->seq = g_seq++;
    return n;
}

void og_node_free_bare(OgNode *n) {
    if (!n) return;
    for (int i = 0; i < n->nkids; i++) og_node_free_bare(n->kids[i]);
    free(n->kids);
    free(n->text);
    if (n->script) og_inst_free(n->script);
    free(n);
}

void og_node_add_child(OgNode *parent, OgNode *child) {
    if (!parent || !child) return;
    if (child->parent) og_node_unlink(child);
    if (parent->nkids == parent->capkids) {
        parent->capkids = parent->capkids ? parent->capkids * 2 : 4;
        parent->kids = (OgNode **)realloc(parent->kids,
            (size_t)parent->capkids * sizeof(OgNode *));
    }
    parent->kids[parent->nkids++] = child;
    child->parent = parent;
    child->in_tree = parent->in_tree;
}

void og_node_unlink(OgNode *n) {
    if (!n || !n->parent) return;
    OgNode *p = n->parent;
    for (int i = 0; i < p->nkids; i++) {
        if (p->kids[i] == n) {
            memmove(&p->kids[i], &p->kids[i + 1],
                    (size_t)(p->nkids - i - 1) * sizeof(OgNode *));
            p->nkids--;
            break;
        }
    }
    n->parent = NULL;
    n->in_tree = 0;
}

OgNode *og_node_child(OgNode *n, int i) {
    if (!n || i < 0 || i >= n->nkids) return NULL;
    return n->kids[i];
}

int og_node_child_count(OgNode *n) { return n ? n->nkids : 0; }

static OgNode *find_child_named(OgNode *n, const char *name, size_t len) {
    for (int i = 0; i < n->nkids; i++)
        if (strlen(n->kids[i]->name) == len &&
            strncmp(n->kids[i]->name, name, len) == 0)
            return n->kids[i];
    return NULL;
}

OgNode *og_node_find(OgNode *from, const char *path) {
    if (!from || !path) return NULL;
    if (strcmp(path, ".") == 0) return from;
    OgNode *cur = from;
    if (path[0] == '/') {
        while (cur->parent) cur = cur->parent;
        path++;
        if (*path == 0) return cur;
        /* первый сегмент абсолютного пути — имя самого корня */
        size_t nl = strlen(cur->name);
        if (strncmp(path, cur->name, nl) == 0 &&
            (path[nl] == '/' || path[nl] == 0)) {
            path += nl;
            if (*path == 0) return cur;
            if (*path == '/') path++;
        }
    }
    char buf[256];
    snprintf(buf, sizeof buf, "%s", path);
    char *save = NULL;
    for (char *seg = strtok_r(buf, "/", &save); seg;
         seg = strtok_r(NULL, "/", &save)) {
        if (strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            if (!cur->parent) return NULL;
            cur = cur->parent;
            continue;
        }
        cur = find_child_named(cur, seg, strlen(seg));
        if (!cur) return NULL;
    }
    return cur;
}

OgNode *og_node_find_any(OgNode *root, const char *name) {
    if (!root || !name) return NULL;
    if (strcmp(root->name, name) == 0) return root;
    for (int i = 0; i < root->nkids; i++) {
        OgNode *r = og_node_find_any(root->kids[i], name);
        if (r) return r;
    }
    return NULL;
}

void og_node_world_pos(const OgNode *n, float *x, float *y) {
    float sx = 0, sy = 0;
    while (n) {
        sx += n->x;
        sy += n->y;
        n = n->parent;
    }
    *x = sx;
    *y = sy;
}

static int label_metrics(OgNode *n, int *w, int *h) {
    font_init();
    *w = n->text ? font_width(n->fsize, n->text) : 0;
    *h = 10 * n->fsize;
    return 1;
}

void og_node_bounds(OgNode *n, float *x, float *y, float *w, float *h) {
    float wx, wy;
    og_node_world_pos(n, &wx, &wy);
    switch (n->type) {
    case OG_T_SPRITE: {
        const OgImage *img = n->spr >= 0 ? og_image_get(n->spr) : NULL;
        float bw = n->w > 0 ? n->w : (img ? img->w : 0);
        float bh = n->h > 0 ? n->h : (img ? img->h : 0);
        *x = wx; *y = wy; *w = bw; *h = bh;
        return;
    }
    case OG_T_LABEL: {
        int tw, th;
        label_metrics(n, &tw, &th);
        *x = n->center ? wx - tw / 2.0f : wx;
        *y = wy;
        *w = tw;
        *h = th;
        return;
    }
    case OG_T_RECT:
        *x = wx; *y = wy; *w = n->rw; *h = n->rh;
        return;
    case OG_T_CIRCLE:
        *x = wx - n->rw; *y = wy - n->rw; *w = n->rw * 2; *h = n->rw * 2;
        return;
    default:
        *x = wx; *y = wy; *w = 0; *h = 0;
        return;
    }
}

int og_node_contains(OgNode *n, float px, float py) {
    float x, y, w, h;
    og_node_bounds(n, &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return 0;
    return px >= x && px < x + w && py >= y && py < y + h;
}

int og_node_overlaps(OgNode *a, OgNode *b) {
    float ax, ay, aw, ah, bx, by, bw, bh;
    og_node_bounds(a, &ax, &ay, &aw, &ah);
    og_node_bounds(b, &bx, &by, &bw, &bh);
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return 0;
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

void og_node_set_text(OgNode *n, const char *text) {
    if (!n) return;
    free(n->text);
    n->text = text ? strdup(text) : NULL;
}

uint32_t og_node_handle(const OgNode *n) { return n ? n->handle : 0; }
