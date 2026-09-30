/* ОГОРОД: среда выполнения проекта — главный цикл, реестр узлов,
 * ввод, отрисовка, загрузка сцен и сценариев, клонирование. */
#ifndef OG_RUNTIME_H
#define OG_RUNTIME_H

#include <stdint.h>
#include "og_node.h"

typedef struct { int frame, kind, x, y; } OgEvent; /* kind: 0 press 1 move 2 release */

typedef struct OgRuntime {
    char dir[512];
    char name[128];
    int W, H, fps;
    uint32_t seed;
    uint32_t bg;

    uint32_t *fb;
    OgNode *root;

    double time;
    int frame;
    int quit;
    uint32_t rng;

    /* реестр handle → узел */
    struct { OgNode *node; uint32_t serial; } *slots;
    int nslots, caps;

    /* кэш сценариев */
    struct { char *path; OgProgram *prog; } *scripts;
    int nscripts, capscripts;

    /* список отрисовки */
    struct { OgNode *n; int z; int seq; } *items;
    int nitems, capitems;

    OgNode *captured;
    float px, py;

    /* очередь освобождения хранит идентификаторы, а не указатели:
     * если родителя освободили вместе с ребёнком, идентификатор ребёнка
     * уже недействителен и запись просто пропускается */
    uint32_t *freelist;
    int nfreelist, capfreelist;

    OgEvent *events;
    int nevents, capevents;
    int next_event;

    char err[320];
    unsigned long sound_calls;
} OgRuntime;

OgRuntime *og_rt_new(int w, int h);
void og_rt_free(OgRuntime *rt);

/* Подключить встроенные функции и свойства к ВМ (делает загрузка проекта). */
void og_bind_install(OgRuntime *rt);

/* Загрузка проекта (project.cfg + главная сцена). 1 = успех. */
int og_rt_load_project(OgRuntime *rt, const char *dir);

/* Компиляция сценария по пути относительно проекта (с кэшем). */
OgProgram *og_rt_script(OgRuntime *rt, const char *relpath);

/* Регистрация узла и подключение его к дереву. */
uint32_t og_rt_register(OgRuntime *rt, OgNode *n);
OgNode *og_rt_node(OgRuntime *rt, uint32_t handle);
void og_rt_attach(OgRuntime *rt, OgNode *parent, OgNode *child);

/* Клонирование поддерева (прототипы/префабы). Возврат: корень клона. */
OgNode *og_rt_clone(OgRuntime *rt, OgNode *src, OgNode *parent);

void og_rt_queue_free(OgRuntime *rt, OgNode *n);
void og_rt_unlink_free(OgRuntime *rt, OgNode *n);
void og_rt_flush_frees(OgRuntime *rt);

/* Кадр симуляции: _process у всех узлов со сценариями. */
void og_rt_step(OgRuntime *rt, float dt);
/* Готовность дерева: _ready сверху вниз. */
void og_rt_ready(OgRuntime *rt);

void og_rt_input_press(OgRuntime *rt, int x, int y);
void og_rt_input_move(OgRuntime *rt, int x, int y);
void og_rt_input_release(OgRuntime *rt, int x, int y);

void og_rt_render(OgRuntime *rt);

uint32_t og_rt_rng(OgRuntime *rt);        /* xorshift32 */
double og_rt_rngf(OgRuntime *rt);          /* [0, 1) */

/* Загрузка файла событий (повтор ввода): "<кадр> press|move|release x y". */
int og_rt_load_events(OgRuntime *rt, const char *path);
void og_rt_apply_events(OgRuntime *rt, int frame);

#endif /* OG_RUNTIME_H */
