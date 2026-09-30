/* Интеграционные тесты: движок сам играет в обе демо-игры.
 * «Оборона грядки» — башенная оборона, «Кирпичи» — арканоид.
 * Игровая логика обеих игр написана целиком на ОгScript. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "og_runtime.h"

/* msg_out копируется в буфер вызывающего: время жизни узла кончено. */
static int run_project(const char *dir, int max_seconds, char *msg_out, size_t msg_sz) {
    OgRuntime *rt = og_rt_new(0, 0);
    if (!og_rt_load_project(rt, dir)) {
        fprintf(stderr, "не удалось загрузить %s: %s\n", dir, rt->err);
        return 0;
    }
    float dt = 1.0f / (float)rt->fps;
    int frames = rt->fps * max_seconds;
    for (int f = 0; f < frames && !rt->quit; f++) {
        og_rt_apply_events(rt, f);
        og_rt_step(rt, dt);
        if (rt->err[0]) {
            fprintf(stderr, "ошибка сценария на кадре %d: %s\n", f, rt->err);
            og_rt_free(rt);
            return 0;
        }
    }
    OgNode *msg = og_node_find(rt->root, "Msg");
    const char *text = (msg && msg->text) ? msg->text : "";
    snprintf(msg_out, msg_sz, "%s", text);
    if (!rt->quit) {
        fprintf(stderr, "%s: игра не завершилась за %d с\n", dir, max_seconds);
        og_rt_free(rt);
        return 0;
    }
    printf("%-22s финал на %.1f с: «%s»\n", rt->name, rt->time, text);
    og_rt_free(rt);
    return 1;
}

static void test_oborona(void) {
    char msg[128];
    if (!run_project("../projects/oborona", 120, msg, sizeof msg)) exit(1);
    if (strstr(msg, "ПОБЕДА") == NULL) {
        fprintf(stderr, "оборона не выиграна: %s\n", msg);
        exit(1);
    }
    printf("demo_test: оборона грядки — ПОБЕДА, матч отыгран автопилотом\n");

    /* интерактивный режим: повтор ввода сажает растение перетаскиванием */
    OgRuntime *rt = og_rt_new(0, 0);
    if (!og_rt_load_project(rt, "../projects/oborona")) {
        fprintf(stderr, "не удалось загрузить проект: %s\n", rt->err);
        exit(1);
    }
    if (!og_rt_load_events(rt, "../projects/oborona/replay_drag.txt")) {
        fprintf(stderr, "%s\n", rt->err);
        exit(1);
    }
    for (int f = 0; f < 240 && !rt->quit; f++) {
        og_rt_apply_events(rt, f);
        og_rt_step(rt, 1.0f / (float)rt->fps);
        if (rt->err[0]) {
            fprintf(stderr, "ошибка сценария: %s\n", rt->err);
            exit(1);
        }
    }
    /* клетку (3,3) занимает человек: автопилот туда не сажает */
    int planted_by_human = 0;
    int slot = og_program_global_slot(rt->root->script->prog, "grid");
    OgValue grid = rt->root->script->globals[slot];
    if (grid.t == OG_ARR && grid.u.a->len > 3) {
        OgValue row3 = grid.u.a->items[3];
        if (row3.t == OG_ARR && row3.u.a->len > 3 &&
            row3.u.a->items[3].t != OG_NIL)
            planted_by_human = 1;
    }
    if (!planted_by_human) {
        fprintf(stderr, "перетаскивание карточки не посадило растение\n");
        exit(1);
    }
    printf("demo_test: drag-and-drop через повтор ввода работает\n");
    og_rt_free(rt);
}

static void test_kirpichi(void) {
    char msg[128];
    if (!run_project("../projects/kirpichi", 120, msg, sizeof msg)) exit(1);
    if (strstr(msg, "ВСЕ КИРПИЧИ") == NULL) {
        fprintf(stderr, "арканоид не выигран: %s\n", msg);
        exit(1);
    }
    printf("demo_test: арканоид «%s» — ПОБЕДА\n", msg);
}

int main(void) {
    test_oborona();
    test_kirpichi();
    printf("demo_test: обе игры на ОгScript отыграны движком без ошибок\n");
    return 0;
}
