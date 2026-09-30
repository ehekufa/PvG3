/* POSIX: strdup, strtok_r, strcasecmp */
#define _POSIX_C_SOURCE 200809L
/* ОГОРОД: среда выполнения — главный цикл, узлы, ввод, отрисовка. */
#include "og_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "og_image.h"
#include "og_vfs.h"

/* ------------------------------------------------------------------ */

OgRuntime *og_rt_new(int w, int h) {
    OgRuntime *rt = (OgRuntime *)calloc(1, sizeof(OgRuntime));
    if (!rt) return NULL;
    rt->W = w > 0 ? w : 1280;
    rt->H = h > 0 ? h : 720;
    rt->fps = 60;
    rt->seed = 0xC0FFEE11u;
    rt->rng = rt->seed;
    rt->bg = 0xFF1A1E24u;
    rt->fb = (uint32_t *)calloc((size_t)rt->W * rt->H, sizeof(uint32_t));
    font_init();
    return rt;
}

void og_rt_free(OgRuntime *rt) {
    if (!rt) return;
    if (rt->root) {
        /* снять сценарии, чтобы не трогать реестр при рекурсивном освобождении */
        og_node_free_bare(rt->root);
    }
    free(rt->slots);
    for (int i = 0; i < rt->nscripts; i++) {
        free(rt->scripts[i].path);
        og_program_unref(rt->scripts[i].prog);
    }
    free(rt->scripts);
    free(rt->items);
    free(rt->freelist);
    free(rt->events);
    free(rt->fb);
    free(rt);
}

/* ------------------------------------------------------------------ */
/* Реестр узлов (handle с серийным номером против висячих ссылок)    */
/* ------------------------------------------------------------------ */

uint32_t og_rt_register(OgRuntime *rt, OgNode *n) {
    if (!n || n->handle) return n->handle;
    if (rt->nslots == rt->caps) {
        rt->caps = rt->caps ? rt->caps * 2 : 256;
        rt->slots = realloc(rt->slots, (size_t)rt->caps * sizeof *rt->slots);
    }
    int at = rt->nslots++;
    rt->slots[at].node = n;
    rt->slots[at].serial = 1;
    n->handle = ((uint32_t)at << 16) | (rt->slots[at].serial & 0xFFFFu);
    return n->handle;
}

OgNode *og_rt_node(OgRuntime *rt, uint32_t handle) {
    if (!handle) return NULL;
    int at = (int)(handle >> 16);
    if (at < 0 || at >= rt->nslots) return NULL;
    if ((handle & 0xFFFFu) != (rt->slots[at].serial & 0xFFFFu)) return NULL;
    return rt->slots[at].node;
}

static void invalidate_handle(OgRuntime *rt, OgNode *n) {
    if (!n->handle) return;
    int at = (int)(n->handle >> 16);
    if (at >= 0 && at < rt->nslots) {
        rt->slots[at].node = NULL;
        rt->slots[at].serial++;
        if (rt->slots[at].serial > 0xFFFEu) rt->slots[at].serial = 1;
    }
    n->handle = 0;
}

void og_rt_attach(OgRuntime *rt, OgNode *parent, OgNode *child) {
    og_rt_register(rt, child);
    for (int i = 0; i < child->nkids; i++) og_rt_attach(rt, child, child->kids[i]);
    og_node_add_child(parent, child);
    child->in_tree = 1;
}

static void free_subtree(OgRuntime *rt, OgNode *n) {
    for (int i = 0; i < n->nkids; i++) free_subtree(rt, n->kids[i]);
    free(n->kids);
    n->kids = NULL;
    n->nkids = n->capkids = 0;
    invalidate_handle(rt, n);          /* гасим идентификатор */
    if (n->script) {
        og_inst_free(n->script);
        n->script = NULL;
    }
    free(n->text);
    free(n);
}


/* Открепить узел от дерева, снять его со сцены и освободить вместе с
 * потомками. Идентификаторы гасятся, поэтому повторная очистка безопасна. */
void og_rt_unlink_free(OgRuntime *rt, OgNode *n) {
    if (!n) return;
    if (rt->captured == n) rt->captured = NULL;
    og_node_unlink(n);
    free_subtree(rt, n);
}

/* ------------------------------------------------------------------ */
/* Сценарии с кэшем                                                    */
/* ------------------------------------------------------------------ */

/* Сценарий читается через VFS: встроенный или с диска. */
static char *read_whole_file(const char *path) { return og_read_file(path); }

OgProgram *og_rt_script(OgRuntime *rt, const char *relpath) {
    for (int i = 0; i < rt->nscripts; i++)
        if (strcmp(rt->scripts[i].path, relpath) == 0)
            return rt->scripts[i].prog;
    char full[768];
    snprintf(full, sizeof full, "%s/%s", rt->dir, relpath);
    char *src = read_whole_file(full);
    if (!src) {
        snprintf(rt->err, sizeof rt->err, "не найден сценарий %.250s", full);
        return NULL;
    }
    char cerr[320];
    OgProgram *p = og_compile(relpath, src, cerr, sizeof cerr);
    free(src);
    if (!p) {
        snprintf(rt->err, sizeof rt->err, "%s", cerr);
        return NULL;
    }
    if (rt->nscripts == rt->capscripts) {
        rt->capscripts = rt->capscripts ? rt->capscripts * 2 : 8;
        rt->scripts = realloc(rt->scripts, (size_t)rt->capscripts * sizeof *rt->scripts);
    }
    rt->scripts[rt->nscripts].path = strdup(relpath);
    rt->scripts[rt->nscripts].prog = p;
    rt->nscripts++;
    return p;
}

/* ------------------------------------------------------------------ */
/* Освобождение узлов (отложенное, как в Godot)                        */
/* ------------------------------------------------------------------ */

void og_rt_queue_free(OgRuntime *rt, OgNode *n) {
    if (!n || n->dead || !n->handle) return;
    n->dead = 1;
    if (rt->nfreelist == rt->capfreelist) {
        rt->capfreelist = rt->capfreelist ? rt->capfreelist * 2 : 64;
        rt->freelist = realloc(rt->freelist, (size_t)rt->capfreelist * sizeof(uint32_t));
    }
    rt->freelist[rt->nfreelist++] = n->handle;
}

void og_rt_flush_frees(OgRuntime *rt) {
    for (int i = 0; i < rt->nfreelist; i++) {
        OgNode *n = og_rt_node(rt, rt->freelist[i]);
        if (!n || !n->dead) continue; /* уже освобождён вместе с родителем */
        if (rt->captured == n) rt->captured = NULL;
        og_node_unlink(n);
        free_subtree(rt, n);
    }
    rt->nfreelist = 0;
}

/* ------------------------------------------------------------------ */
/* Клонирование (префабы)                                              */
/* ------------------------------------------------------------------ */

static OgNode *clone_node(OgRuntime *rt, OgNode *src, OgNode *parent) {
    OgNode *c = og_node_new(src->type, src->name);
    c->x = src->x; c->y = src->y; c->z = src->z;
    c->visible = src->visible;
    c->alpha = src->alpha;
    c->spr = src->spr; c->w = src->w; c->h = src->h; c->flip = src->flip;
    c->sx = src->sx; c->sy = src->sy; c->sw = src->sw; c->sh = src->sh;
    c->fsize = src->fsize; c->color = src->color; c->center = src->center;
    c->rw = src->rw; c->rh = src->rh; c->fill = src->fill;
    c->outline = src->outline;
    if (src->text) og_node_set_text(c, src->text);
    if (src->script) c->script = og_inst_new(src->script->prog, c);
    og_rt_attach(rt, parent, c);
    for (int i = 0; i < src->nkids; i++) clone_node(rt, src->kids[i], c);
    return c;
}

static void ready_subtree(OgRuntime *rt, OgNode *n);

OgNode *og_rt_clone(OgRuntime *rt, OgNode *src, OgNode *parent) {
    if (!src) return NULL;
    if (!parent) parent = src->parent ? src->parent : rt->root;
    OgNode *c = clone_node(rt, src, parent);
    ready_subtree(rt, c);
    return c;
}

/* ------------------------------------------------------------------ */
/* Жизненный цикл сценариев                                            */
/* ------------------------------------------------------------------ */

static void report_script_error(OgRuntime *rt, OgNode *n) {
    if (!n->script || n->script->err[0] == 0) return;
    fprintf(stderr, "[ОГОРОД] ошибка сценария узла '%s': %s\n",
            n->name, n->script->err);
    snprintf(rt->err, sizeof rt->err, "%s", n->script->err);
}

static int call_hook(OgRuntime *rt, OgNode *n, const char *func,
                     const OgValue *args, int nargs, OgValue *out) {
    if (!n->script || n->script->broken || n->dead) return 0;
    int r = og_inst_call(n->script, func, args, nargs, out);
    if (r < 0) report_script_error(rt, n);
    return r;
}

static void ready_subtree(OgRuntime *rt, OgNode *n) {
    /* как в Godot: сначала дети, потом сам узел */
    for (int i = 0; i < n->nkids; i++) {
        ready_subtree(rt, n->kids[i]);
        if (n->dead) return;
    }
    if (n->script) call_hook(rt, n, "_ready", NULL, 0, NULL);
}

void og_rt_ready(OgRuntime *rt) {
    if (rt->root) ready_subtree(rt, rt->root);
    og_rt_flush_frees(rt);
}

/* Снимок узлов со сценариями, чтобы спавн/удаление по ходу кадра не
 * ломали обход. */
static OgNode **g_snap;
static int g_nsnap, g_capsnap;

static void snapshot_scripts(OgNode *n) {
    if (!n->visible && n->parent) {
        /* невидимый узел не обрабатывается, но дети могут быть видимы?
         * Как в рендерере: невидимость родителя скрывает поддерево. */
        return;
    }
    if (n->script && !n->dead) {
        if (g_nsnap == g_capsnap) {
            g_capsnap = g_capsnap ? g_capsnap * 2 : 128;
            g_snap = (OgNode **)realloc(g_snap, (size_t)g_capsnap * sizeof(OgNode *));
        }
        g_snap[g_nsnap++] = n;
    }
    for (int i = 0; i < n->nkids; i++) snapshot_scripts(n->kids[i]);
}

void og_rt_step(OgRuntime *rt, float dt) {
    g_nsnap = 0;
    if (rt->root) snapshot_scripts(rt->root);
    for (int i = 0; i < g_nsnap && !rt->quit; i++) {
        OgNode *n = g_snap[i];
        if (n->dead || !n->script || n->script->broken) continue;
        OgValue a = og_num(dt);
        call_hook(rt, n, "_process", &a, 1, NULL);
    }
    og_rt_flush_frees(rt);
    rt->time += dt;
}

/* ------------------------------------------------------------------ */
/* Ввод                                                                */
/* ------------------------------------------------------------------ */

static void build_drawlist(OgRuntime *rt);

static OgNode *pick_node(OgRuntime *rt, int x, int y) {
    build_drawlist(rt);
    for (int i = rt->nitems - 1; i >= 0; i--) {
        OgNode *n = rt->items[i].n;
        if (!n->script || n->dead) continue;
        if (og_node_contains(n, (float)x, (float)y)) return n;
    }
    return NULL;
}

void og_rt_input_press(OgRuntime *rt, int x, int y) {
    rt->px = (float)x;
    rt->py = (float)y;
    build_drawlist(rt);
    for (int i = rt->nitems - 1; i >= 0; i--) {
        OgNode *n = rt->items[i].n;
        if (!n->script || n->dead) continue;
        if (!og_node_contains(n, (float)x, (float)y)) continue;
        OgValue args[2] = { og_num(x), og_num(y) };
        OgValue out = og_nil();
        int r = call_hook(rt, n, "_press", args, 2, &out);
        int hit = r == 1 && og_truthy(out);
        og_release(out);
        if (n->dead) continue;
        if (hit) {
            rt->captured = n;
            return;
        }
    }
}

void og_rt_input_move(OgRuntime *rt, int x, int y) {
    rt->px = (float)x;
    rt->py = (float)y;
    if (!rt->captured || rt->captured->dead) return;
    OgValue args[2] = { og_num(x), og_num(y) };
    call_hook(rt, rt->captured, "_move", args, 2, NULL);
}

void og_rt_input_release(OgRuntime *rt, int x, int y) {
    rt->px = (float)x;
    rt->py = (float)y;
    OgNode *target = rt->captured;
    rt->captured = NULL;
    if (!target || target->dead) target = pick_node(rt, x, y);
    if (!target) return;
    OgValue args[2] = { og_num(x), og_num(y) };
    call_hook(rt, target, "_release", args, 2, NULL);
}

/* ------------------------------------------------------------------ */
/* Отрисовка                                                           */
/* ------------------------------------------------------------------ */

static void gather(OgRuntime *rt, OgNode *n) {
    if (!n->visible) return;
    if (n->type != OG_T_NODE2D || n->script) {
        if (rt->nitems == rt->capitems) {
            rt->capitems = rt->capitems ? rt->capitems * 2 : 128;
            rt->items = realloc(rt->items, (size_t)rt->capitems * sizeof *rt->items);
        }
        rt->items[rt->nitems].n = n;
        rt->items[rt->nitems].z = n->z;
        rt->items[rt->nitems].seq = n->seq;
        rt->nitems++;
    }
    for (int i = 0; i < n->nkids; i++) gather(rt, n->kids[i]);
}

static int draw_cmp(const void *pa, const void *pb) {
    const struct { OgNode *n; int z; int seq; } *a = pa, *b = pb;
    if (a->z != b->z) return a->z < b->z ? -1 : 1;
    return a->seq < b->seq ? -1 : a->seq > b->seq ? 1 : 0;
}

static void build_drawlist(OgRuntime *rt) {
    rt->nitems = 0;
    if (!rt->root) return;
    gather(rt, rt->root);
    qsort(rt->items, (size_t)rt->nitems, sizeof *rt->items, draw_cmp);
}

static uint32_t apply_alpha(uint32_t color, float alpha) {
    if (alpha >= 1.0f) return color;
    if (alpha <= 0.0f) return color & 0x00FFFFFFu;
    int a = (int)((color >> 24) * alpha);
    return (color & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

void og_rt_render(OgRuntime *rt) {
    if (!rt->fb) return;
    build_drawlist(rt);
    for (int i = 0; i < rt->W * rt->H; i++) rt->fb[i] = rt->bg;
    for (int i = 0; i < rt->nitems; i++) {
        OgNode *n = rt->items[i].n;
        float wx, wy;
        og_node_world_pos(n, &wx, &wy);
        switch (n->type) {
        case OG_T_SPRITE: {
            const OgImage *img = n->spr >= 0 ? og_image_get(n->spr) : NULL;
            if (!img) break;
            int dw = n->w > 0 ? (int)n->w : (n->sw > 0 ? n->sw : img->w);
            int dh = n->h > 0 ? (int)n->h : (n->sh > 0 ? n->sh : img->h);
            og_blit(rt->fb, rt->W, rt->H, img, (int)wx, (int)wy,
                    dw, dh, n->flip, (int)(n->alpha * 255),
                    n->sx, n->sy, n->sw, n->sh);
            break;
        }
        case OG_T_LABEL: {
            if (!n->text || !n->text[0]) break;
            int x = (int)wx;
            if (n->center) x -= font_width(n->fsize, n->text) / 2;
            uint32_t c = apply_alpha(n->color, n->alpha);
            font_draw(rt->fb, rt->W, rt->H, x, (int)wy, n->fsize, c, n->text);
            break;
        }
        case OG_T_RECT: {
            uint32_t c = apply_alpha(n->fill, n->alpha);
            og_fill(rt->fb, rt->W, rt->H, (int)wx, (int)wy,
                    (int)n->rw, (int)n->rh, c);
            break;
        }
        case OG_T_CIRCLE: {
            uint32_t c = apply_alpha(n->fill, n->alpha);
            og_circle(rt->fb, rt->W, rt->H, (int)wx, (int)wy,
                      (int)n->rw, c, n->outline);
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Случайные числа (детерминированные от сида проекта)                 */
/* ------------------------------------------------------------------ */

uint32_t og_rt_rng(OgRuntime *rt) {
    rt->rng ^= rt->rng << 13;
    rt->rng ^= rt->rng >> 17;
    rt->rng ^= rt->rng << 5;
    return rt->rng;
}

double og_rt_rngf(OgRuntime *rt) {
    return (double)(og_rt_rng(rt) & 0xFFFFFFu) / (double)0x1000000;
}

/* ------------------------------------------------------------------ */
/* Повтор ввода                                                        */
/* ------------------------------------------------------------------ */

int og_rt_load_events(OgRuntime *rt, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(rt->err, sizeof rt->err, "нет файла событий %s", path);
        return 0;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        int frame, x, y;
        char kind[16];
        if (sscanf(line, "%d %15s %d %d", &frame, kind, &x, &y) != 4) continue;
        int k = strcmp(kind, "press") == 0 ? 0 :
                strcmp(kind, "move") == 0 ? 1 :
                strcmp(kind, "release") == 0 ? 2 : -1;
        if (k < 0) continue;
        if (rt->nevents == rt->capevents) {
            rt->capevents = rt->capevents ? rt->capevents * 2 : 64;
            rt->events = realloc(rt->events, (size_t)rt->capevents * sizeof(OgEvent));
        }
        rt->events[rt->nevents].frame = frame;
        rt->events[rt->nevents].kind = k;
        rt->events[rt->nevents].x = x;
        rt->events[rt->nevents].y = y;
        rt->nevents++;
    }
    fclose(f);
    return 1;
}

void og_rt_apply_events(OgRuntime *rt, int frame) {
    while (rt->next_event < rt->nevents &&
           rt->events[rt->next_event].frame <= frame) {
        OgEvent *e = &rt->events[rt->next_event++];
        if (e->kind == 0) og_rt_input_press(rt, e->x, e->y);
        else if (e->kind == 1) og_rt_input_move(rt, e->x, e->y);
        else og_rt_input_release(rt, e->x, e->y);
    }
}
