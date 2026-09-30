/* ОГОРОД: ОгScript — скриптовый язык движка (лексер, компилятор в байткод,
 * стековая виртуальная машина). Синтаксис вдохновлён GDScript: отступы,
 * func / var / if / while / for, динамическая типизация. */
#ifndef OG_VM_H
#define OG_VM_H

#include "og_common.h"

typedef struct OgProgram OgProgram;
typedef struct OgVm OgVm;
struct OgNode;

/* Встроенные функции, доступные сценариям. */
enum {
    NAT_PRINT, NAT_STR, NAT_NUM, NAT_LEN,
    NAT_ABS, NAT_FLOOR, NAT_CEIL, NAT_ROUND, NAT_MIN, NAT_MAX, NAT_CLAMP,
    NAT_SQRT, NAT_RAND, NAT_RANDI, NAT_TIME, NAT_QUIT,
    NAT_GET_NODE, NAT_HAS_NODE, NAT_CHILD_COUNT, NAT_GET_CHILD,
    NAT_SPAWN, NAT_FREE_NODE, NAT_OVERLAPS, NAT_POINT_IN,
    NAT_PUSH, NAT_PLAY_SOUND, NAT_DIST, NAT_COUNT
};

/* Компиляция исходника. При ошибке возвращает NULL и пишет сообщение в
 * err (вместе с номером строки). path используется только в сообщениях. */
OgProgram *og_compile(const char *path, const char *src, char *err, size_t errsz);
OgProgram *og_program_ref(OgProgram *p);
void og_program_unref(OgProgram *p);
int og_program_find_func(OgProgram *p, const char *name); /* -1 если нет */
int og_program_global_slot(OgProgram *p, const char *name); /* -1 если нет */
int og_program_num_globals(OgProgram *p);
const char *og_program_path(OgProgram *p);

/* Экземпляр сценария, привязанный к узлу сцены (у каждого узла свой
 * экземпляр со своими переменными, код программы общий). */
typedef struct OgScriptInst {
    OgProgram *prog;
    OgValue *globals;
    int nglobals;
    struct OgNode *self;
    int broken;      /* ошибка выполнения: экземпляр остановлен */
    char err[256];
} OgScriptInst;

OgScriptInst *og_inst_new(OgProgram *p, struct OgNode *self);
void og_inst_free(OgScriptInst *inst);

/* Вызов функции сценария. Возврат: 1 — выполнена, 0 — функции нет
 * (не ошибка), -1 — ошибка выполнения (сообщение в inst->err). */
int og_inst_call(OgScriptInst *inst, const char *func,
                 const OgValue *args, int nargs, OgValue *out);

/* Хуки, которые подсистемы движка (узлы, время, случайные числа)
 * предоставляют сценариям. Устанавливаются средой выполнения один раз. */
typedef struct OgVmHooks {
    void *ud; /* OgRuntime* */
    OgValue (*native)(OgVm *vm, int id, const OgValue *args, int nargs);
    /* метод объекта-узла; вернуть 1 если обработан, 0 если такой функции
     * нет у сценария узла, -1 при ошибке выполнения */
    int (*method)(OgVm *vm, uint32_t handle, const char *name,
                  const OgValue *args, int nargs, OgValue *out);
    int (*getprop)(OgVm *vm, uint32_t handle, const char *name, OgValue *out);
    int (*setprop)(OgVm *vm, uint32_t handle, const char *name, OgValue v);
    OgStr *(*describe)(void *ud, uint32_t handle); /* для отладочной печати */
} OgVmHooks;

extern OgVmHooks og_vm_hooks;

/* Служебное: рекурсивный вызов функции другого экземпляра той же ВМ. */
int og_vm_call_instance(OgVm *vm, OgScriptInst *inst, int fi,
                        const OgValue *args, int nargs, OgValue *out);
OgScriptInst *og_vm_current_inst(OgVm *vm);
const char *og_vm_error(OgVm *vm);
/* Пометить текущее выполнение как ошибочное (используют хуки движка). */
void og_vm_fail(OgVm *vm, const char *msg);

#endif /* OG_VM_H */
