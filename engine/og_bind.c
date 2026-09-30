/* ОГОРОД: встроенные функции и свойства, доступные сценариям:
 * печать, математика, время, поиск узлов, спавн, геометрия. */
#include "og_runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "og_image.h"

static OgRuntime *RT;

static OgNode *self_node(OgVm *vm) {
    OgScriptInst *inst = og_vm_current_inst(vm);
    return inst ? inst->self : NULL;
}

static OgNode *node_arg(const OgValue *args, int i, int nargs) {
    if (i >= nargs || args[i].t != OG_OBJ) return NULL;
    return og_rt_node(RT, args[i].u.h);
}

static OgValue native_fn(OgVm *vm, int id, const OgValue *args, int nargs) {
    switch (id) {
    case NAT_PRINT: {
        for (int i = 0; i < nargs; i++) {
            OgStr *s = og_to_str(args[i]);
            if (args[i].t == OG_OBJ && args[i].u.h) {
                OgNode *n = og_rt_node(RT, args[i].u.h);
                if (n) printf(i ? " <%s '%s'>" : "<%s '%s'>",
                               n->type == OG_T_SPRITE ? "Спрайт" :
                               n->type == OG_T_LABEL ? "Надпись" : "Узел",
                               n->name);
                else fputs(i ? " <удалённый узел>" : "<удалённый узел>", stdout);
            } else {
                fputs(i ? " " : "", stdout);
                fwrite(s->data, 1, (size_t)s->len, stdout);
            }
            og_release(og_strv(s));
        }
        putchar('\n');
        return og_nil();
    }
    case NAT_STR: return og_strv(og_to_str(args[0]));
    case NAT_NUM: return og_num(nargs ? og_to_num(args[0]) : 0);
    case NAT_LEN:
        if (nargs && args[0].t == OG_STR && args[0].u.s)
            return og_num(args[0].u.s->len);
        if (nargs && args[0].t == OG_ARR && args[0].u.a)
            return og_num(args[0].u.a->len);
        return og_num(0);
    case NAT_ABS: return og_num(fabs(og_to_num(args[0])));
    case NAT_FLOOR: return og_num(floor(og_to_num(args[0])));
    case NAT_CEIL: return og_num(ceil(og_to_num(args[0])));
    case NAT_ROUND: return og_num(round(og_to_num(args[0])));
    case NAT_MIN: {
        double a = og_to_num(args[0]), b = og_to_num(args[1]);
        if (nargs > 2) { double c = og_to_num(args[2]); if (c < b) b = c; }
        return og_num(a < b ? a : b);
    }
    case NAT_MAX: {
        double a = og_to_num(args[0]), b = og_to_num(args[1]);
        if (nargs > 2) { double c = og_to_num(args[2]); if (c > b) b = c; }
        return og_num(a > b ? a : b);
    }
    case NAT_CLAMP: {
        double v = og_to_num(args[0]), a = og_to_num(args[1]), b = og_to_num(args[2]);
        if (v < a) v = a;
        if (v > b) v = b;
        return og_num(v);
    }
    case NAT_SQRT: return og_num(sqrt(og_to_num(args[0])));
    case NAT_RAND: {
        double a = nargs > 0 ? og_to_num(args[0]) : 0;
        double b = nargs > 1 ? og_to_num(args[1]) : 1;
        return og_num(a + og_rt_rngf(RT) * (b - a));
    }
    case NAT_RANDI: {
        int a = (int)og_to_num(args[0]);
        int b = nargs > 1 ? (int)og_to_num(args[1]) : a;
        if (b < a) { int t = a; a = b; b = t; }
        return og_num(a + (int)(og_rt_rng(RT) % (uint32_t)(b - a + 1)));
    }
    case NAT_TIME: return og_num(RT->time);
    case NAT_QUIT: RT->quit = 1; return og_nil();
    case NAT_GET_NODE: {
        OgNode *self = self_node(vm);
        if (!self || !nargs || args[0].t != OG_STR) return og_nil();
        OgNode *n = og_node_find(self, args[0].u.s->data);
        return n && !n->dead ? og_obj(n->handle) : og_nil();
    }
    case NAT_HAS_NODE: {
        OgNode *self = self_node(vm);
        if (!self || !nargs || args[0].t != OG_STR) return og_bool(0);
        return og_bool(og_node_find(self, args[0].u.s->data) != NULL);
    }
    case NAT_CHILD_COUNT: {
        OgNode *n = node_arg(args, 0, nargs);
        return og_num(n ? og_node_child_count(n) : 0);
    }
    case NAT_GET_CHILD: {
        OgNode *n = node_arg(args, 0, nargs);
        if (!n) return og_nil();
        OgNode *c = og_node_child(n, (int)og_to_num(args[1]));
        return c ? og_obj(c->handle) : og_nil();
    }
    case NAT_SPAWN: {
        /* spawn("ИмяПрототипа" [, родитель]) — клон узла из любого места
         * сцены; родитель по умолчанию — родитель вызвавшего узла. */
        if (!nargs || args[0].t != OG_STR) return og_nil();
        OgNode *proto = og_node_find_any(RT->root, args[0].u.s->data);
        if (!proto) return og_nil();
        OgNode *parent = NULL;
        if (nargs > 1 && args[1].t == OG_OBJ) parent = og_rt_node(RT, args[1].u.h);
        if (!parent) {
            OgNode *self = self_node(vm);
            parent = self && self->parent ? self->parent : RT->root;
        }
        OgNode *c = og_rt_clone(RT, proto, parent);
        return c ? og_obj(c->handle) : og_nil();
    }
    case NAT_FREE_NODE: {
        OgNode *n = node_arg(args, 0, nargs);
        if (n) og_rt_queue_free(RT, n);
        return og_nil();
    }
    case NAT_OVERLAPS: {
        OgNode *a = node_arg(args, 0, nargs);
        OgNode *b = node_arg(args, 1, nargs);
        return og_bool(a && b && og_node_overlaps(a, b));
    }
    case NAT_POINT_IN: {
        OgNode *n = node_arg(args, 0, nargs);
        if (!n) return og_bool(0);
        return og_bool(og_node_contains(n, (float)og_to_num(args[1]),
                                        (float)og_to_num(args[2])));
    }
    case NAT_PUSH: {
        if (nargs >= 2 && args[0].t == OG_ARR && args[0].u.a) {
            OgValue v = args[1];
            og_retain(v);
            og_arr_push(args[0].u.a, v);
        }
        return og_nil();
    }
    case NAT_PLAY_SOUND:
        RT->sound_calls++;
        return og_nil();
    case NAT_DIST: {
        double dx = og_to_num(args[2]) - og_to_num(args[0]);
        double dy = og_to_num(args[3]) - og_to_num(args[1]);
        return og_num(sqrt(dx * dx + dy * dy));
    }
    default:
        return og_nil();
    }
}

/* Методы узлов: свободные и встроенные, остальное — функции сценария. */
static int method_fn(OgVm *vm, uint32_t handle, const char *name,
                     const OgValue *args, int nargs, OgValue *out) {
    OgNode *n = og_rt_node(RT, handle);
    if (!n) return 0;
    if (strcmp(name, "free") == 0) {
        og_rt_queue_free(RT, n);
        *out = og_nil();
        return 1;
    }
    if (strcmp(name, "dup") == 0) {
        OgNode *c = og_rt_clone(RT, n, n->parent ? n->parent : RT->root);
        *out = c ? og_obj(c->handle) : og_nil();
        return 1;
    }
    if (n->script) {
        int fi = og_program_find_func(n->script->prog, name);
        if (fi >= 0) {
            int r = og_vm_call_instance(vm, n->script, fi, args, nargs, out);
            if (r < 0) {
                fprintf(stderr, "[ОГОРОД] ошибка сценария узла '%s': %s\n",
                        n->name, n->script->err);
                snprintf(RT->err, sizeof RT->err, "%s", n->script->err);
                og_vm_fail(vm, n->script->err); /* корректная раскрутка стека */
            }
            return r == 0 ? 0 : r;
        }
    }
    return 0;
}

static int get_prop(uint32_t handle, const char *name, OgValue *out) {
    OgNode *n = og_rt_node(RT, handle);
    if (!n) return 0;
    if (strcmp(name, "x") == 0) { *out = og_num(n->x); return 1; }
    if (strcmp(name, "y") == 0) { *out = og_num(n->y); return 1; }
    if (strcmp(name, "z") == 0) { *out = og_num(n->z); return 1; }
    if (strcmp(name, "visible") == 0) { *out = og_bool(n->visible); return 1; }
    /* dead — узел поставлен на удаление (только чтение) */
    if (strcmp(name, "dead") == 0) { *out = og_bool(n->dead); return 1; }
    if (strcmp(name, "alpha") == 0) { *out = og_num(n->alpha); return 1; }
    if (strcmp(name, "name") == 0) {
        *out = og_strv(og_str_new(n->name, (int)strlen(n->name)));
        return 1;
    }
    if (strcmp(name, "w") == 0 || strcmp(name, "h") == 0) {
        float v = name[0] == 'w' ? n->w : n->h;
        if (v <= 0 && n->type == OG_T_SPRITE) {
            const OgImage *img = n->spr >= 0 ? og_image_get(n->spr) : NULL;
            if (img) v = name[0] == 'w' ? img->w : img->h;
        }
        *out = og_num(v);
        return 1;
    }
    if (strcmp(name, "flip") == 0) { *out = og_bool(n->flip); return 1; }
    if (strcmp(name, "sprite") == 0) {
        const OgImage *img = n->spr >= 0 ? og_image_get(n->spr) : NULL;
        *out = og_strv(og_str_new(img ? img->name : "", img ? (int)strlen(img->name) : 0));
        return 1;
    }
    if (strcmp(name, "text") == 0) {
        const char *t = n->text ? n->text : "";
        *out = og_strv(og_str_new(t, (int)strlen(t)));
        return 1;
    }
    if (strcmp(name, "size") == 0) { *out = og_num(n->fsize); return 1; }
    if (strcmp(name, "color") == 0) { *out = og_num(n->color); return 1; }
    if (strcmp(name, "center") == 0) { *out = og_bool(n->center); return 1; }
    if (strcmp(name, "rw") == 0) { *out = og_num(n->rw); return 1; }
    if (strcmp(name, "rh") == 0) { *out = og_num(n->rh); return 1; }
    if (strcmp(name, "fill") == 0) { *out = og_num(n->fill); return 1; }
    if (strcmp(name, "outline") == 0) { *out = og_bool(n->outline); return 1; }
    /* переменные сценария тоже видны как свойства */
    if (n->script) {
        int slot = og_program_global_slot(n->script->prog, name);
        if (slot >= 0) {
            *out = n->script->globals[slot];
            og_retain(*out);
            return 1;
        }
    }
    return 0;
}

static int set_prop(uint32_t handle, const char *name, OgValue v) {
    OgNode *n = og_rt_node(RT, handle);
    if (!n) return 0;
    if (strcmp(name, "x") == 0) { n->x = (float)og_to_num(v); return 1; }
    if (strcmp(name, "y") == 0) { n->y = (float)og_to_num(v); return 1; }
    if (strcmp(name, "z") == 0) { n->z = (int)og_to_num(v); return 1; }
    if (strcmp(name, "visible") == 0) { n->visible = og_truthy(v); return 1; }
    if (strcmp(name, "alpha") == 0) {
        n->alpha = (float)og_to_num(v);
        if (n->alpha < 0) n->alpha = 0;
        if (n->alpha > 1) n->alpha = 1;
        return 1;
    }
    if (strcmp(name, "w") == 0) { n->w = (float)og_to_num(v); return 1; }
    if (strcmp(name, "h") == 0) { n->h = (float)og_to_num(v); return 1; }
    if (strcmp(name, "sx") == 0) { n->sx = (int)og_to_num(v); return 1; }
    if (strcmp(name, "sy") == 0) { n->sy = (int)og_to_num(v); return 1; }
    if (strcmp(name, "sw") == 0) { n->sw = (int)og_to_num(v); return 1; }
    if (strcmp(name, "sh") == 0) { n->sh = (int)og_to_num(v); return 1; }
    if (strcmp(name, "flip") == 0) { n->flip = og_truthy(v); return 1; }
    if (strcmp(name, "sprite") == 0) {
        if (v.t != OG_STR || !v.u.s) return 0;
        int idx = og_image_index_by_name(v.u.s->data);
        if (idx < 0) return 0;
        n->spr = idx;
        return 1;
    }
    if (strcmp(name, "text") == 0) {
        if (v.t != OG_STR || !v.u.s) return 0;
        og_node_set_text(n, v.u.s->data);
        return 1;
    }
    if (strcmp(name, "size") == 0) {
        int s = (int)og_to_num(v);
        if (s < 1) s = 1;
        if (s > 8) s = 8;
        n->fsize = s;
        return 1;
    }
    if (strcmp(name, "color") == 0) {
        n->color = (uint32_t)og_to_num(v) | 0xFF000000u;
        return 1;
    }
    if (strcmp(name, "center") == 0) { n->center = og_truthy(v); return 1; }
    if (strcmp(name, "rw") == 0) { n->rw = (float)og_to_num(v); return 1; }
    if (strcmp(name, "rh") == 0) { n->rh = (float)og_to_num(v); return 1; }
    if (strcmp(name, "fill") == 0) {
        n->fill = (uint32_t)og_to_num(v) | 0xFF000000u;
        return 1;
    }
    if (strcmp(name, "outline") == 0) { n->outline = og_truthy(v); return 1; }
    if (n->script) {
        int slot = og_program_global_slot(n->script->prog, name);
        if (slot >= 0) {
            og_retain(v);
            og_release(n->script->globals[slot]);
            n->script->globals[slot] = v;
            return 1;
        }
    }
    return 0;
}

static int get_prop_hook(OgVm *vm, uint32_t handle, const char *name, OgValue *out) {
    (void)vm;
    return get_prop(handle, name, out);
}

static int set_prop_hook(OgVm *vm, uint32_t handle, const char *name, OgValue v) {
    (void)vm;
    return set_prop(handle, name, v);
}

static OgStr *describe_hook(void *ud, uint32_t handle) {
    OgRuntime *rt = (OgRuntime *)ud;
    OgNode *n = og_rt_node(rt, handle);
    char buf[96];
    if (!n) {
        const char *gone = "<удалённый узел>";
        return og_str_new(gone, (int)strlen(gone));
    }
    snprintf(buf, sizeof buf, "<узел '%s'>", n->name);
    return og_str_new(buf, (int)strlen(buf));
}

void og_bind_install(OgRuntime *rt) {
    RT = rt;
    og_vm_hooks.ud = rt;
    og_vm_hooks.native = native_fn;
    og_vm_hooks.method = method_fn;
    og_vm_hooks.getprop = get_prop_hook;
    og_vm_hooks.setprop = set_prop_hook;
    og_vm_hooks.describe = describe_hook;
}
