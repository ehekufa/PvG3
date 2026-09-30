/* Тесты движка: узлы, свойства, ввод, спавн/удаление, сцены, рендер. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "og_runtime.h"
#include "og_image.h"

static int checks;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "ПРОВАЛ: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
        checks++; \
    } while (0)

static void attach(OgRuntime *rt, OgNode *n, const char *src) {
    char err[512];
    OgProgram *p = og_compile("<тест>", src, err, sizeof err);
    if (!p) {
        fprintf(stderr, "компиляция: %s\n", err);
        exit(1);
    }
    OgScriptInst *inst = og_inst_new(p, n);
    og_program_unref(p);
    CHECK(inst != NULL);
    if (inst->broken) {
        fprintf(stderr, "инициализация сценария: %s\n", inst->err);
        exit(1);
    }
    if (n->script) og_inst_free(n->script); /* замена сценария узла */
    n->script = inst;
    (void)rt;
}

static OgNode *make_root(OgRuntime *rt) {
    OgNode *root = og_node_new(OG_T_NODE2D, "Root");
    rt->root = root;
    og_rt_register(rt, root);
    root->in_tree = 1;
    return root;
}

static void test_process_and_props(void) {
    OgRuntime *rt = og_rt_new(320, 180);
    og_bind_install(rt);
    OgNode *root = make_root(rt);

    OgNode *hero = og_node_new(OG_T_SPRITE, "Герой");
    hero->spr = 0;
    og_rt_attach(rt, root, hero);
    attach(rt, hero,
        "var скорость = 50\n"
        "func _ready():\n"
        "    self.x = 10\n"
        "func _process(dt):\n"
        "    self.x = self.x + скорость * dt\n"
        "func сдвиг():\n"
        "    self.x += 100\n"
        "func _press(x, y):\n"
        "    self.y = y\n"
        "    return true\n");
    og_rt_ready(rt);
    CHECK(fabsf(hero->x - 10.0f) < 0.001f); /* _ready отработал */

    for (int i = 0; i < 60; i++) og_rt_step(rt, 1.0f / 60.0f);
    /* 10 + 50 * 1 с = 60 */
    CHECK(fabsf(hero->x - 60.0f) < 0.5f);

    /* вызов функции сценария снаружи (включая кириллическое имя) */
    OgValue out = og_nil();
    CHECK(og_inst_call(hero->script, "сдвиг", NULL, 0, &out) == 1);
    og_release(out);
    CHECK(fabsf(hero->x - 160.0f) < 0.001f);

    /* нажатие внутрь спрайта 100x100 доходит до _press */
    hero->w = 100;
    hero->h = 100;
    og_rt_input_press(rt, (int)hero->x + 5, 30);
    CHECK(fabsf(hero->y - 30.0f) < 0.001f);
    CHECK(rt->captured == hero);
    og_rt_input_release(rt, (int)hero->x + 5, 30);
    CHECK(rt->captured == NULL);
    og_rt_free(rt);
}

static void test_spawn_and_free(void) {
    OgRuntime *rt = og_rt_new(320, 180);
    og_bind_install(rt);
    OgNode *root = make_root(rt);

    OgNode *proto = og_node_new(OG_T_SPRITE, "Утёнок");
    proto->visible = 0;
    proto->w = 40;
    proto->h = 40;
    og_rt_attach(rt, root, proto);
    attach(rt, proto,
        "var жив = 0\n"
        "func _ready():\n"
        "    жив = 1\n"
        "    self.visible = true\n");

    OgNode *game = og_node_new(OG_T_NODE2D, "Игра");
    og_rt_attach(rt, root, game);
    attach(rt, game,
        "var кто = null\n"
        "func роди():\n"
        "    кто = spawn(\"Утёнок\")\n"
        "    кто.x = 42\n"
        "func жив_ли():\n"
        "    if кто == null:\n"
        "        return -1\n"
        "    return кто.жив   # переменная сценария видна как свойство узла\n"
        "func убей():\n"
        "    free_node(кто)\n");

    OgValue out = og_nil();
    CHECK(og_inst_call(game->script, "роди", NULL, 0, &out) == 1);
    og_release(out);
    /* клон появился в дереве, видим, со своим экземпляром сценария */
    CHECK(og_node_child_count(root) == 3);
    OgNode *duck = og_node_child(root, 2);
    CHECK(strcmp(duck->name, "Утёнок") == 0);
    CHECK(duck != proto && duck->visible == 1 && duck->script != NULL);
    CHECK(fabsf(duck->x - 42.0f) < 0.001f);

    /* чтение переменной чужого сценария через свойство */
    CHECK(og_inst_call(game->script, "жив_ли", NULL, 0, &out) == 1);
    CHECK(out.t == OG_NUM && out.u.n == 1);
    og_release(out);

    /* удаление узла: handle клона перестаёт отвечать */
    uint32_t h = duck->handle;
    CHECK(og_rt_node(rt, h) == duck);
    CHECK(og_inst_call(game->script, "убей", NULL, 0, &out) == 1);
    og_release(out);
    og_rt_flush_frees(rt);
    CHECK(og_rt_node(rt, h) == NULL);
    CHECK(og_node_child_count(root) == 2);

    /* сценарий с ошибкой времени выполнения ломает только свой узел */
    attach(rt, game, "func плохо():\n    var a = [1]\n    return a[9]\n");
    OgValue o2 = og_nil();
    CHECK(og_inst_call(game->script, "плохо", NULL, 0, &o2) == -1);
    CHECK(game->script->broken);
    CHECK(game->script->err[0] != 0);
    CHECK(strstr(game->script->err, "вне диапазона") != NULL);
    og_release(o2);

    /* родитель и ребёнок освобождаются в одном кадре — второй не крашит */
    OgNode *box = og_node_new(OG_T_NODE2D, "Коробка");
    og_rt_attach(rt, root, box);
    OgNode *kid = og_node_new(OG_T_SPRITE, "Внутри");
    og_rt_attach(rt, box, kid);
    uint32_t hbox = box->handle, hkid = kid->handle;
    og_rt_queue_free(rt, kid);
    og_rt_queue_free(rt, box);
    og_rt_flush_frees(rt);
    CHECK(og_rt_node(rt, hbox) == NULL);
    CHECK(og_rt_node(rt, hkid) == NULL); /* идентификатор ребёнка погашен */
    og_rt_flush_frees(rt);                /* повторная очередь безопасна */
    CHECK(og_node_child_count(root) == 2);
    og_rt_free(rt);
}

static void write_file_str(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "не создать %s: %s\n", path, strerror(errno)); exit(1); }
    fputs(body, f);
    fclose(f);
}

static void test_project_and_scene(void) {
    mkdir("tmp_proj", 0755);
    mkdir("tmp_proj/scenes", 0755);
    mkdir("tmp_proj/scripts", 0755);
    write_file_str("tmp_proj/project.cfg",
        "# тестовый проект\n"
        "name = Тестовый проект\n"
        "main_scene = scenes/main.scene\n"
        "width = 320\n"
        "height = 180\n"
        "fps = 60\n"
        "seed = 7\n"
        "bg = #112233\n");
    write_file_str("tmp_proj/scripts/duck.og",
        "var скорость = 30\n"
        "func _ready():\n"
        "    self.visible = true\n"
        "func _process(dt):\n"
        "    self.x -= скорость * dt\n");
    write_file_str("tmp_proj/scenes/main.scene",
        "[node]\n"
        "name = Root\n"
        "[node]\n"
        "name = Фон\n"
        "parent = Root\n"
        "type = Sprite\n"
        "sprite = map\n"
        "x = 0\n"
        "y = 0\n"
        "w = 320\n"
        "h = 180\n"
        "[node]\n"
        "name = Утка\n"
        "parent = Root\n"
        "type = Sprite\n"
        "sprite = duck\n"
        "x = 200\n"
        "y = 60\n"
        "w = 40\n"
        "h = 40\n"
        "script = scripts/duck.og\n"
        "var_скорость = 60\n"
        "[node]\n"
        "name = Счёт\n"
        "parent = Root\n"
        "type = Label\n"
        "x = 10\n"
        "y = 10\n"
        "size = 2\n"
        "color = #ffffff\n"
        "text = Монеты: 0\n");

    OgRuntime *rt = og_rt_new(0, 0);
    CHECK(og_rt_load_project(rt, "tmp_proj") == 1);
    CHECK(rt->W == 320 && rt->H == 180);
    CHECK(strcmp(rt->name, "Тестовый проект") == 0);
    /* #RRGGBB из файла кладётся в кадр как 0xAABBGGRR */
    CHECK(rt->bg == 0xFF332211u);
    CHECK(og_node_child_count(rt->root) == 3);

    /* цвет из сцены и цвет из скрипта совпадают по порядку байт */
    OgNode *label = og_node_find(rt->root, "Счёт");
    CHECK(label && label->color == 0xFFFFFFFFu);

    OgNode *duck = og_node_find(rt->root, "Утка");
    CHECK(duck != NULL && duck->type == OG_T_SPRITE);
    CHECK(duck->visible == 1);
    /* var_скорость переопределила значение из сценария */
    float x0 = duck->x;
    for (int i = 0; i < 60; i++) og_rt_step(rt, 1.0f / 60.0f);
    CHECK(fabsf((x0 - duck->x) - 60.0f) < 1.0f);

    /* поиск путей */
    CHECK(og_node_find(duck, "../Счёт") != NULL);
    CHECK(og_node_find(duck, "/Root/Фон") != NULL);

    /* рендер: кадр не пустой, надпись и спрайт на месте */
    og_rt_render(rt);
    int painted = 0;
    for (int i = 0; i < rt->W * rt->H; i++)
        if (rt->fb[i] != rt->bg) painted++;
    CHECK(painted > 1000);
    CHECK(og_write_bmp("/tmp/ogorod_engine_test.bmp", rt->fb, rt->W, rt->H));
    og_rt_free(rt);
}

int main(void) {
    test_process_and_props();
    test_spawn_and_free();
    test_project_and_scene();
    printf("engine_test: все проверки пройдены (%d)\n", checks);
    return 0;
}
