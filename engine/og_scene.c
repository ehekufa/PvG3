/* POSIX: strdup, strtok_r, strcasecmp */
#define _POSIX_C_SOURCE 200809L
/* ОГОРОД: файлы проекта и сцен.
 *
 * project.cfg:
 *   name = Оборона грядки
 *   main_scene = scenes/main.scene
 *   width = 1280 / height = 720 / fps = 60 / seed = 1 / bg = #1a1e24
 *
 * Сцена — секции [node] со свойствами "ключ = значение". Пути родителя
 * считаются от корня: parent = Root/Cards. Ключи вида var_<имя> задают
 * начальные значения переменных сценария (как экспорт-переменные). */
#include "og_runtime.h"

#include "og_image.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>

void og_rt_unlink_free(OgRuntime *rt, OgNode *n);

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

/* Цвет в кадре хранится как 0xAABBGGRR (так же, как в оригинальной
 * игре и в упакованных спрайтах), а в файлах сцен пишется привычным
 * #RRGGBB или #AARRGGBB. */
static uint32_t parse_color(const char *s) {
    if (s[0] != '#') return 0xFFFFFFFFu;
    unsigned long v = strtoul(s + 1, NULL, 16);
    size_t digits = strlen(s + 1);
    unsigned r, g, b, a = 255;
    if (digits == 8) {
        a = (v >> 24) & 0xFF; r = (v >> 16) & 0xFF;
        g = (v >> 8) & 0xFF;  b = v & 0xFF;
    } else {
        r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
    }
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* Значение в сцене: число / true|false / "строка" / голый токен / #цвет.
 * Возвращает тип: 1 число, 2 строка (в *outs malloc), 3 булево, 4 цвет. */
static int parse_value(const char *raw, double *outn, char **outs, uint32_t *outc) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", raw);
    trim(buf);
    if (buf[0] == '"') {
        size_t n = strlen(buf);
        if (n >= 2 && buf[n - 1] == '"') { buf[n - 1] = 0; }
        *outs = strdup(buf + 1);
        return 2;
    }
    if (buf[0] == '#') { *outc = parse_color(buf); return 4; }
    if (strcmp(buf, "true") == 0) { *outn = 1; return 3; }
    if (strcmp(buf, "false") == 0) { *outn = 0; return 3; }
    /* число — только если весь токен числовой: «100 монет» это текст,
     * для чисел с текстом пишите "100 монет" в кавычках */
    char *end = NULL;
    double v = strtod(buf, &end);
    if (end && end != buf && *end == 0) { *outn = v; return 1; }
    *outs = strdup(buf);
    return 2;
}

static int parse_type(const char *s) {
    if (!s) return OG_T_NODE2D;
    if (strcasecmp(s, "Sprite") == 0) return OG_T_SPRITE;
    if (strcasecmp(s, "Label") == 0) return OG_T_LABEL;
    if (strcasecmp(s, "Rect") == 0) return OG_T_RECT;
    if (strcasecmp(s, "Circle") == 0) return OG_T_CIRCLE;
    return OG_T_NODE2D;
}

typedef struct {
    OgRuntime *rt;
    OgNode *root;
    int ok;
} SceneCtx;

static OgNode *node_section(SceneCtx *sc) {
    OgNode *n = og_node_new(OG_T_NODE2D, "Узел");
    (void)sc;
    return n;
}

static void apply_key(SceneCtx *sc, OgNode *n, const char *key, const char *raw) {
    (void)sc;
    double num = 0;
    char *str = NULL;
    uint32_t color = 0;
    int kind = parse_value(raw, &num, &str, &color);

    if (strcmp(key, "name") == 0) {
        if (kind == 2) snprintf(n->name, sizeof n->name, "%s", str);
        goto done;
    }
    if (strcmp(key, "type") == 0) {
        if (kind == 2) n->type = parse_type(str);
        goto done;
    }
    if (strcmp(key, "parent") == 0 || strcmp(key, "script") == 0) {
        goto done; /* обрабатываются на этапе сборки */
    }
    if (strncmp(key, "var_", 4) == 0) goto done; /* после сценария */

    switch (kind) {
    case 1: case 3:
        if (strcmp(key, "x") == 0) n->x = (float)num;
        else if (strcmp(key, "y") == 0) n->y = (float)num;
        else if (strcmp(key, "z") == 0) n->z = (int)num;
        else if (strcmp(key, "w") == 0) n->w = (float)num;
        else if (strcmp(key, "h") == 0) n->h = (float)num;
        else if (strcmp(key, "sx") == 0) n->sx = (int)num;
        else if (strcmp(key, "sy") == 0) n->sy = (int)num;
        else if (strcmp(key, "sw") == 0) n->sw = (int)num;
        else if (strcmp(key, "sh") == 0) n->sh = (int)num;
        else if (strcmp(key, "rw") == 0) n->rw = (float)num;
        else if (strcmp(key, "rh") == 0) n->rh = (float)num;
        else if (strcmp(key, "size") == 0) n->fsize = (int)num;
        else if (strcmp(key, "alpha") == 0) n->alpha = (float)num;
        else if (strcmp(key, "visible") == 0) n->visible = (int)num != 0;
        else if (strcmp(key, "flip") == 0) n->flip = (int)num != 0;
        else if (strcmp(key, "center") == 0) n->center = (int)num != 0;
        else if (strcmp(key, "outline") == 0) n->outline = (int)num != 0;
        break;
    case 2:
        if (strcmp(key, "sprite") == 0) {
            int idx = og_image_index_by_name(str);
            if (idx >= 0) n->spr = idx;
            else fprintf(stderr, "[ОГОРОД] неизвестный спрайт '%s' у узла '%s'\n",
                         str, n->name);
        } else if (strcmp(key, "text") == 0) {
            og_node_set_text(n, str);
        }
        break;
    case 4:
        if (strcmp(key, "color") == 0) n->color = color;
        else if (strcmp(key, "fill") == 0) n->fill = color;
        break;
    default:
        break;
    }
done:
    free(str);
}

/* Второй проход секции: родитель, сценарий, переменные сценария. */
static void attach_section(SceneCtx *sc, OgNode *n,
                           char keys[][64], char vals[][512], int nk,
                           int is_root) {
    OgRuntime *rt = sc->rt;
    char parent[512] = "";
    char script[512] = "";
    for (int i = 0; i < nk; i++) {
        if (strcmp(keys[i], "parent") == 0) snprintf(parent, sizeof parent, "%s", vals[i]);
        if (strcmp(keys[i], "script") == 0) snprintf(script, sizeof script, "%s", vals[i]);
    }
    if (is_root) {
        if (rt->root) {
            snprintf(rt->err, sizeof rt->err, "в сцене два корня");
            sc->ok = 0;
            return;
        }
        og_rt_register(rt, n);
        rt->root = n;
        n->in_tree = 1;
    } else {
        OgNode *par = NULL;
        if (parent[0]) {
            par = og_node_find(rt->root, parent);
        }
        if (!par) par = rt->root;
        if (!par) {
            snprintf(rt->err, sizeof rt->err,
                     "узел '%s': корень сцены ещё не создан", n->name);
            sc->ok = 0;
            return;
        }
        og_rt_attach(rt, par, n);
    }
    if (script[0]) {
        char bare[512];
        snprintf(bare, sizeof bare, "%s", script);
        /* кавычки в значении уже сняты разборщиком */
        OgProgram *prog = og_rt_script(rt, bare);
        if (!prog) { sc->ok = 0; return; }
        n->script = og_inst_new(prog, n);
        if (n->script && n->script->broken) {
            fprintf(stderr, "[ОГОРОД] %s: %s\n", n->name, n->script->err);
            snprintf(rt->err, sizeof rt->err, "%s", n->script->err);
            sc->ok = 0;
            return;
        }
        for (int i = 0; i < nk; i++) {
            if (strncmp(keys[i], "var_", 4) != 0) continue;
            const char *var = keys[i] + 4;
            double num = 0;
            char *str = NULL;
            uint32_t color = 0;
            int kind = parse_value(vals[i], &num, &str, &color);
            int slot = og_program_global_slot(prog, var);
            if (slot < 0) {
                fprintf(stderr, "[ОГОРОД] узел '%s': в сценарии нет переменной '%s'\n",
                        n->name, var);
                free(str);
                continue;
            }
            OgValue v;
            if (kind == 2) v = og_strv(og_str_new(str, (int)strlen(str)));
            else if (kind == 4) v = og_num((double)color);
            else v = og_num(num);
            og_release(n->script->globals[slot]);
            n->script->globals[slot] = v;
            free(str);
        }
    }
}

static int load_scene(OgRuntime *rt, const char *scene_rel) {
    char full[1024];
    snprintf(full, sizeof full, "%s/%s", rt->dir, scene_rel);
    char *src = read_file(full);
    if (!src) {
        snprintf(rt->err, sizeof rt->err, "не найдена сцена %.200s", full);
        return 0;
    }
    SceneCtx sc;
    sc.rt = rt;
    sc.root = NULL;
    sc.ok = 1;

    OgNode *cur = NULL;
    char keys[64][64];
    char vals[64][512];
    int nk = 0;
    int pending_is_root = 1; /* первый узел файла становится корнем */

    char *line = src;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char buf[600];
        snprintf(buf, sizeof buf, "%s", line);
        trim(buf);
        line = nl ? nl + 1 : NULL;

        if (buf[0] == 0 || buf[0] == '#') continue;
        if (strcmp(buf, "[node]") == 0) {
            if (cur) {
                attach_section(&sc, cur, keys, vals, nk, pending_is_root);
                pending_is_root = 0;
            }
            if (!sc.ok) break;
            cur = node_section(&sc);
            nk = 0;
            continue;
        }
        if (!cur) {
            snprintf(rt->err, sizeof rt->err, "%.200s: свойства вне секции [node]", full);
            sc.ok = 0;
            break;
        }
        char *eq = strchr(buf, '=');
        if (!eq || nk >= 64) continue;
        *eq = 0;
        char *k = buf, *v = eq + 1;
        trim(k);
        trim(v);
        snprintf(keys[nk], 64, "%.60s", k);
        snprintf(vals[nk], 512, "%s", v);
        nk++;
        apply_key(&sc, cur, k, v);
        if (!sc.ok) break;
    }
    if (sc.ok && cur) attach_section(&sc, cur, keys, vals, nk, pending_is_root);
    if (sc.ok && !rt->root) {
        snprintf(rt->err, sizeof rt->err, "%.200s: в сцене нет узлов", full);
        sc.ok = 0;
    }
    if (!sc.ok && cur) {
        /* аккуратно: узел мог уже прикрепиться к дереву */
        int attached = (cur->parent != NULL) || rt->root == cur;
        if (attached) {
            og_rt_unlink_free(rt, cur);
        } else {
            og_node_free_bare(cur);
        }
    }
    free(src);
    return sc.ok;
}

static int parse_project_cfg(OgRuntime *rt) {
    char full[1024];
    snprintf(full, sizeof full, "%s/project.cfg", rt->dir);
    char *src = read_file(full);
    if (!src) {
        snprintf(rt->err, sizeof rt->err, "нет файла %.200s", full);
        return 0;
    }
    char main_scene[512] = "main.scene";
    char *line = src;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char buf[600];
        snprintf(buf, sizeof buf, "%s", line);
        trim(buf);
        line = nl ? nl + 1 : NULL;
        if (buf[0] == 0 || buf[0] == '#') continue;
        char *eq = strchr(buf, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = buf, *v = eq + 1;
        trim(k);
        trim(v);
        double num = 0;
        char *str = NULL;
        uint32_t color = 0;
        int kind = parse_value(v, &num, &str, &color);
        if (strcmp(k, "name") == 0 && kind == 2)
            snprintf(rt->name, sizeof rt->name, "%s", str);
        else if (strcmp(k, "main_scene") == 0 && kind == 2)
            snprintf(main_scene, sizeof main_scene, "%s", str);
        else if (strcmp(k, "width") == 0) rt->W = (int)num;
        else if (strcmp(k, "height") == 0) rt->H = (int)num;
        else if (strcmp(k, "fps") == 0 && num > 0) rt->fps = (int)num;
        else if (strcmp(k, "seed") == 0) { rt->seed = (uint32_t)num; rt->rng = rt->seed; }
        else if (strcmp(k, "bg") == 0 && kind == 4) rt->bg = color;
        free(str);
    }
    free(src);
    /* переаллоцировать буфер кадра под размеры проекта */
    free(rt->fb);
    rt->fb = (uint32_t *)calloc((size_t)rt->W * rt->H, sizeof(uint32_t));
    if (!rt->fb) {
        snprintf(rt->err, sizeof rt->err, "нет памяти на кадр %dx%d", rt->W, rt->H);
        return 0;
    }
    return load_scene(rt, main_scene);
}

int og_rt_load_project(OgRuntime *rt, const char *dir) {
    snprintf(rt->dir, sizeof rt->dir, "%s", dir);
    og_bind_install(rt);
    if (!parse_project_cfg(rt)) return 0;
    og_rt_ready(rt);
    return 1;
}
