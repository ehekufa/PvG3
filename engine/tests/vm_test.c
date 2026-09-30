/* Тесты языка ОгScript: лексер, компилятор, ВМ, сборка значений. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "og_runtime.h"

static OgRuntime *rt;
static int checks;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "ПРОВАЛ: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
        checks++; \
    } while (0)

static OgScriptInst *make(const char *src) {
    char err[512];
    OgProgram *p = og_compile("тест.og", src, err, sizeof err);
    if (!p) {
        fprintf(stderr, "не удалось скомпилировать: %s\n---\n%s\n", err, src);
        exit(1);
    }
    OgScriptInst *inst = og_inst_new(p, NULL);
    og_program_unref(p);
    CHECK(inst && !inst->broken);
    return inst;
}

static OgValue call(OgScriptInst *inst, const char *fn) {
    OgValue out = og_nil();
    int r = og_inst_call(inst, fn, NULL, 0, &out);
    if (r < 0) {
        fprintf(stderr, "ошибка выполнения %s: %s\n", fn, inst->err);
        exit(1);
    }
    CHECK(r == 1);
    return out;
}

static void test_arith(void) {
    OgScriptInst *i = make(
        "func f():\n"
        "    return 2 + 3 * 4\n"
        "func g():\n"
        "    return (2 + 3) * 4 - 10 / 4\n"
        "func h():\n"
        "    return 7 % 3\n"
        "func neg():\n"
        "    return -5 + 3\n");
    OgValue v = call(i, "f");
    CHECK(v.t == OG_NUM && v.u.n == 14);
    og_release(v);
    v = call(i, "g");
    CHECK(v.t == OG_NUM && fabs(v.u.n - 17.5) < 1e-9);
    og_release(v);
    v = call(i, "h");
    CHECK(v.t == OG_NUM && v.u.n == 1);
    og_release(v);
    v = call(i, "neg");
    CHECK(v.t == OG_NUM && v.u.n == -2);
    og_release(v);
    og_inst_free(i);
}

static void test_strings(void) {
    OgScriptInst *i = make(
        "func f():\n"
        "    var s = \"утка\" + \"!\"\n"
        "    return s\n"
        "func mixed():\n"
        "    return \"монет: \" + str(25)\n");
    OgValue v = call(i, "f");
    CHECK(v.t == OG_STR && strcmp(v.u.s->data, "утка!") == 0);
    og_release(v);
    v = call(i, "mixed");
    CHECK(v.t == OG_STR && strcmp(v.u.s->data, "монет: 25") == 0);
    og_release(v);
    og_inst_free(i);
}

static void test_control(void) {
    OgScriptInst *i = make(
        "func sign(x):\n"
        "    if x > 0:\n"
        "        return 1\n"
        "    elif x < 0:\n"
        "        return -1\n"
        "    else:\n"
        "        return 0\n"
        "func while_sum():\n"
        "    var s = 0\n"
        "    var k = 1\n"
        "    while k <= 10:\n"
        "        s += k\n"
        "        k = k + 1\n"
        "    return s\n"
        "func for_sum():\n"
        "    var s = 0\n"
        "    for k in range(10):\n"
        "        s += k\n"
        "    return s\n"
        "func for_range2():\n"
        "    var s = 0\n"
        "    for k in range(2, 6):\n"
        "        s += k\n"
        "    return s\n"
        "func nested():\n"
        "    var s = 0\n"
        "    for r in range(3):\n"
        "        for c in range(3):\n"
        "            if r == c:\n"
        "                continue\n"
        "            s += 1\n"
        "    return s\n"
        "func breaker():\n"
        "    var s = 0\n"
        "    while true:\n"
        "        s += 1\n"
        "        if s >= 5:\n"
        "            break\n"
        "    return s\n");
    OgValue a1 = og_num(3), a2 = og_num(-2), a3 = og_num(0);
    OgValue out = og_nil();
    CHECK(og_inst_call(i, "sign", &a1, 1, &out) == 1 && out.u.n == 1);
    og_release(out);
    CHECK(og_inst_call(i, "sign", &a2, 1, &out) == 1 && out.u.n == -1);
    og_release(out);
    CHECK(og_inst_call(i, "sign", &a3, 1, &out) == 1 && out.u.n == 0);
    og_release(out);
    OgValue v = call(i, "while_sum");
    CHECK(v.u.n == 55);
    og_release(v);
    v = call(i, "for_sum");
    CHECK(v.u.n == 45);
    og_release(v);
    v = call(i, "for_range2");
    CHECK(v.u.n == 14);
    og_release(v);
    v = call(i, "nested");
    CHECK(v.u.n == 6);
    og_release(v);
    v = call(i, "breaker");
    CHECK(v.u.n == 5);
    og_release(v);
    og_inst_free(i);
}

static void test_funcs(void) {
    OgScriptInst *i = make(
        "func fib(n):\n"
        "    if n < 2:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "func ten():\n"
        "    return fib(10)\n"
        "var acc = 0\n"
        "func add_to_acc(x):\n"
        "    acc = acc + x\n"
        "func run_acc():\n"
        "    add_to_acc(5)\n"
        "    add_to_acc(7)\n"
        "    return acc\n");
    OgValue v = call(i, "ten");
    CHECK(v.u.n == 55);
    og_release(v);
    v = call(i, "run_acc");
    CHECK(v.u.n == 12);
    og_release(v);
    og_inst_free(i);
}

static void test_arrays(void) {
    OgScriptInst *i = make(
        "func f():\n"
        "    var a = []\n"
        "    push(a, 10)\n"
        "    push(a, 20)\n"
        "    a.push(30)\n"
        "    return a[0] + a[1] + a[2]\n"
        "func grid():\n"
        "    var g = []\n"
        "    for r in range(3):\n"
        "        var row = []\n"
        "        for c in range(3):\n"
        "            push(row, r * 3 + c)\n"
        "        push(g, row)\n"
        "    return g[2][1]\n"
        "func pop_test():\n"
        "    var a = [1, 2, 3]\n"
        "    var x = a.pop()\n"
        "    return x * 100 + len(a)\n"
        "func logical():\n"
        "    var t = true and false\n"
        "    var u = true or false\n"
        "    var n = not t\n"
        "    if t:\n"
        "        return -1\n"
        "    if not u:\n"
        "        return -2\n"
        "    if not n:\n"
        "        return -3\n"
        "    return 7\n");
    OgValue v = call(i, "f");
    CHECK(v.u.n == 60);
    og_release(v);
    v = call(i, "grid");
    CHECK(v.u.n == 7);
    og_release(v);
    v = call(i, "pop_test");
    CHECK(v.u.n == 302);
    og_release(v);
    v = call(i, "logical");
    CHECK(v.u.n == 7);
    og_release(v);
    og_inst_free(i);
}

static void test_globals_init(void) {
    OgScriptInst *i = make(
        "var x = 5 * 5\n"
        "var s = \"грядка\"\n"
        "var arr = [1, 2]\n"
        "func get_x():\n"
        "    return x\n"
        "func get_len():\n"
        "    return len(s) + len(arr)\n");
    OgValue v = call(i, "get_x");
    CHECK(v.u.n == 25);
    og_release(v);
    v = call(i, "get_len");
    CHECK(v.u.n == 14); /* 12 байт кириллицы + 2 элемента */
    og_release(v);
    og_inst_free(i);
}

static void test_colors(void) {
    /* #RRGGBB в сценарии даёт тот же порядок байт, что и цвета в сцене */
    OgScriptInst *i = make("func f():\n    return #ff8000\n");
    OgValue v = call(i, "f");
    CHECK(v.t == OG_NUM && (uint32_t)v.u.n == 0xFF0080FFu);
    og_release(v);
    og_inst_free(i);
}

static void test_continuation(void) {
    /* перенос строки внутри выражения по оператору */
    OgScriptInst *i = make(
        "func f():\n"
        "    var s = 1 +\n"
        "            2 +\n"
        "            3\n"
        "    if s == 6 and\n"
        "       s > 5:\n"
        "        return s\n"
        "    return -1\n");
    OgValue v = call(i, "f");
    CHECK(v.t == OG_NUM && v.u.n == 6);
    og_release(v);
    og_inst_free(i);
}

static void test_errors(void) {
    char err[512];
    /* ошибка компиляции с номером строки */
    OgProgram *p = og_compile("плохой.og",
        "func f():\n    return неизвестная_переменная\n", err, sizeof err);
    CHECK(p == NULL);
    CHECK(strstr(err, "строка 2") != NULL);
    CHECK(strstr(err, "плохой.og") != NULL);

    /* ошибка времени выполнения: индекс вне диапазона */
    OgScriptInst *i = make(
        "func boom():\n"
        "    var a = [1]\n"
        "    return a[5]\n"
        "func after():\n"
        "    return 1\n");
    OgValue out = og_nil();
    int r = og_inst_call(i, "boom", NULL, 0, &out);
    CHECK(r == -1);
    CHECK(i->broken == 1);
    CHECK(strstr(i->err, "вне диапазона") != NULL);
    og_release(out);
    og_inst_free(i);

    /* деление на ноль */
    OgScriptInst *j = make("func z():\n    return 1 / 0\n");
    r = og_inst_call(j, "z", NULL, 0, &out);
    CHECK(r == -1 && strstr(j->err, "деление на ноль") != NULL);
    og_release(out);
    og_inst_free(j);

    /* табуляция запрещена */
    p = og_compile("т.og", "func f():\n\treturn 1\n", err, sizeof err);
    CHECK(p == NULL);
    CHECK(strstr(err, "табуляция") != NULL);
}

int main(void) {
    rt = og_rt_new(64, 64);
    og_bind_install(rt);
    test_arith();
    test_strings();
    test_control();
    test_funcs();
    test_arrays();
    test_globals_init();
    test_colors();
    test_continuation();
    test_errors();
    og_rt_free(rt);
    printf("vm_test: все проверки пройдены (%d)\n", checks);
    return 0;
}
