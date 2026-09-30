/* ОГОРОД: узлы сцены — аналог Node/Sprite/Label из Godot.
 * Координаты узла относительны родителя (мировая позиция — сумма
 * смещений по цепочке предков). */
#ifndef OG_NODE_H
#define OG_NODE_H

#include <stdint.h>
#include "og_common.h"
#include "og_vm.h"

enum {
    OG_T_NODE2D = 0, /* пустой узел-контейнер */
    OG_T_SPRITE,     /* картинка из реестра спрайтов */
    OG_T_LABEL,      /* текст встроенным PT Sans */
    OG_T_RECT,       /* закрашенный прямоугольник */
    OG_T_CIRCLE      /* закрашенный круг */
};

typedef struct OgNode OgNode;
struct OgRuntime;

struct OgNode {
    char name[64];
    int type;

    OgNode *parent;
    OgNode **kids;
    int nkids, capkids;

    /* трансформ и видимость */
    float x, y;
    int visible;
    int z;         /* порядок отрисовки: меньше — дальше */
    float alpha;   /* 0..1, влияет на спрайты и фигуры */

    /* спрайт */
    int spr;       /* id картинки, -1 если не задан */
    float w, h;    /* размер отрисовки; 0 = родной размер */
    int flip;
    int sx, sy, sw, sh; /* исходная область картинки; 0 = вся */

    /* текст */
    char *text;
    int fsize;     /* 1..8 как в оригинальной игре */
    uint32_t color;
    int center;    /* центрировать по горизонтали в точке x */

    /* прямоугольник и круг */
    float rw, rh;
    uint32_t fill;
    int outline;   /* для круга: только контур */

    OgScriptInst *script;
    uint32_t handle; /* 0 до регистрации в среде выполнения */
    int seq;         /* порядок создания для стабильной сортировки */
    int in_tree;
    int dead;        /* ждёт удаления в конце кадра */
};

OgNode *og_node_new(int type, const char *name);
void og_node_free_bare(OgNode *n); /* без регистрации: только память */

void og_node_add_child(OgNode *parent, OgNode *child);
void og_node_unlink(OgNode *n); /* убрать из списка родителя */

OgNode *og_node_child(OgNode *n, int i);
int og_node_child_count(OgNode *n);

/* Пути: "." — сам узел, ".." — родитель, "Имя", "А/Б", "/Корень/А". */
OgNode *og_node_find(OgNode *from, const char *path);
OgNode *og_node_find_any(OgNode *root, const char *name); /* поиск по имени */

void og_node_world_pos(const OgNode *n, float *x, float *y);
/* Границы узла в мировых координатах (для попаданий и пересечений). */
void og_node_bounds(OgNode *n, float *x, float *y, float *w, float *h);
int og_node_contains(OgNode *n, float px, float py);
int og_node_overlaps(OgNode *a, OgNode *b);

void og_node_set_text(OgNode *n, const char *text);
uint32_t og_node_handle(const OgNode *n);

#endif /* OG_NODE_H */
