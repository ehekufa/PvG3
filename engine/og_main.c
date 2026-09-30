/* ОГОРОД: консольный запускатор проектов.
 *
 *   ogorod run <проект> [--frames N] [--input повтор.txt]
 *            [--shot КАДР=файл.bmp ...] [--every N --shots-dir каталог]
 *            [--scale S] [--headless]
 *   ogorod check <сценарий.og>   — проверить сценарий без запуска
 *   ogorod tree <проект>         — показать дерево сцены
 *   ogorod version
 *
 * Движок полностью детерминирован: один и тот же проект, сид и файл
 * повтора ввода всегда дают одинаковый результат — это и есть
 * «режим тестирования», как в серьёзных движках. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "og_runtime.h"
#include "og_image.h"

typedef struct { int frame; char path[256]; } Shot;

static void print_tree(OgNode *n, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", stdout);
    const char *t = n->type == OG_T_SPRITE ? "Sprite" :
                    n->type == OG_T_LABEL ? "Label" :
                    n->type == OG_T_RECT ? "Rect" :
                    n->type == OG_T_CIRCLE ? "Circle" : "Node2D";
    printf("%s [%s]%s%s\n", n->name, t,
           n->script ? " *" : "", n->visible ? "" : " (скрыт)");
    for (int i = 0; i < og_node_child_count(n); i++)
        print_tree(og_node_child(n, i), depth + 1);
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "version") == 0) {
        printf("%s %s — движок на C из репозитория PvG3\n",
               OG_ENGINE_NAME, OG_ENGINE_VERSION);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "check") == 0) {
        char *src = read_all(argv[2]);
        if (!src) {
            fprintf(stderr, "не найден файл %s\n", argv[2]);
            return 1;
        }
        char err[512];
        OgProgram *p = og_compile(argv[2], src, err, sizeof err);
        free(src);
        if (!p) {
            fprintf(stderr, "ОШИБКА: %s\n", err);
            return 1;
        }
        int nf = 0, ng = og_program_num_globals(p);
        for (const char *fn = "_ready"; fn; fn = NULL)
            if (og_program_find_func(p, fn) >= 0) nf++;
        printf("сценарий %s: OK (глобальных переменных: %d)\n", argv[2], ng);
        og_program_unref(p);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "tree") == 0) {
        OgRuntime *rt = og_rt_new(0, 0);
        if (!og_rt_load_project(rt, argv[2])) {
            fprintf(stderr, "ОШИБКА: %s\n", rt->err);
            og_rt_free(rt);
            return 1;
        }
        printf("проект '%s'\n", rt->name);
        print_tree(rt->root, 0);
        og_rt_free(rt);
        return 0;
    }
    if (argc < 3 || strcmp(argv[1], "run") != 0) {
        printf("%s %s — игровой движок на C\n", OG_ENGINE_NAME, OG_ENGINE_VERSION);
        printf("использование:\n");
        printf("  ogorod run <проект> [--frames N] [--input повтор.txt]\n");
        printf("       [--shot КАДР=файл.bmp ...] [--every N --shots-dir каталог]\n");
        printf("       [--scale S] [--headless]\n");
        printf("  ogorod check <сценарий.og>\n");
        printf("  ogorod tree <проект>\n");
        return argc >= 2 && strcmp(argv[1], "version") == 0 ? 0 : 2;
    }

    int frames = -1, scale = 1, every = 0, headless = 0;
    const char *input = NULL, *shots_dir = NULL;
    Shot shots[64];
    int nshots = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc)
            input = argv[++i];
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--every") == 0 && i + 1 < argc)
            every = atoi(argv[++i]);
        else if (strcmp(argv[i], "--shots-dir") == 0 && i + 1 < argc)
            shots_dir = argv[++i];
        else if (strcmp(argv[i], "--headless") == 0)
            headless = 1;
        else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc && nshots < 64) {
            char spec[300];
            snprintf(spec, sizeof spec, "%s", argv[++i]);
            char *eq = strchr(spec, '=');
            if (eq) {
                *eq = 0;
                shots[nshots].frame = atoi(spec);
                snprintf(shots[nshots].path, sizeof shots->path, "%s", eq + 1);
                nshots++;
            }
        } else {
            fprintf(stderr, "неизвестный параметр %s\n", argv[i]);
            return 2;
        }
    }
    if (scale < 1) scale = 1;

    OgRuntime *rt = og_rt_new(0, 0);
    if (!og_rt_load_project(rt, argv[2])) {
        fprintf(stderr, "ОШИБКА: %s\n", rt->err);
        og_rt_free(rt);
        return 1;
    }
    if (input && !og_rt_load_events(rt, input)) {
        fprintf(stderr, "ОШИБКА: %s\n", rt->err);
        og_rt_free(rt);
        return 1;
    }
    if (frames < 0) frames = rt->fps * 10;
    float dt = 1.0f / (float)rt->fps;

    printf("[%s] проект '%s', %dx%d, %d fps, %d кадров симуляции\n",
           OG_ENGINE_NAME, rt->name, rt->W, rt->H, rt->fps, frames);

    for (int f = 0; f < frames && !rt->quit; f++) {
        rt->frame = f;
        og_rt_apply_events(rt, f);
        og_rt_step(rt, dt);
        int need_shot = 0;
        for (int s = 0; s < nshots; s++)
            if (shots[s].frame == f) need_shot = 1;
        if (every > 0 && shots_dir && f % every == 0) need_shot = 1;
        if ((need_shot || f == frames - 1) && !headless) {
            og_rt_render(rt);
            for (int s = 0; s < nshots; s++) {
                if (shots[s].frame == f) {
                    og_write_bmp_scaled(shots[s].path, rt->fb, rt->W, rt->H, scale);
                    printf("кадр %5d -> %s\n", f, shots[s].path);
                }
            }
            if (every > 0 && shots_dir && f % every == 0) {
                char p[512];
                snprintf(p, sizeof p, "%s/frame_%05d.bmp", shots_dir, f);
                og_write_bmp_scaled(p, rt->fb, rt->W, rt->H, scale);
            }
        }
    }
    printf("[%s] готово: время %.2f с, событий звука %lu, выход по quit=%d\n",
           OG_ENGINE_NAME, rt->time, rt->sound_calls, rt->quit);
    if (rt->err[0]) fprintf(stderr, "[%s] последняя ошибка: %s\n", OG_ENGINE_NAME, rt->err);
    int code = rt->err[0] ? 1 : 0;
    og_rt_free(rt);
    return code;
}
