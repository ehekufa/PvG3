/* ОГОРОД (OGOROD) — игровой движок на C, выросший из PvG3.
 *
 * og_common.h: базовые значения скриптового языка ОгScript
 * (nil / bool / число / строка / массив / объект / функция) и работа с
 * памятью (подсчёт ссылок для строк и массивов). */
#ifndef OG_COMMON_H
#define OG_COMMON_H

#include <stddef.h>
#include <stdint.h>

#define OG_ENGINE_NAME "ОГОРОД"
#define OG_ENGINE_VERSION "0.1.0"

typedef struct OgStr {
    int refc;
    int len; /* байты, без завершающего нуля (он всегда есть после них) */
    char data[];
} OgStr;

struct OgValue;
typedef struct OgArr {
    int refc;
    int len, cap;
    struct OgValue *items;
} OgArr;

enum {
    OG_NIL,     /* null */
    OG_BOOL,    /* true / false */
    OG_NUM,     /* double */
    OG_STR,     /* OgStr* */
    OG_ARR,     /* OgArr* */
    OG_OBJ,     /* дескриптор узла сцены (handle) */
    OG_FUNC,    /* индекс функции сценария в программе */
    OG_NATIVE   /* индекс встроенной функции движка */
};

typedef struct OgValue {
    int t;
    union {
        int b;
        double n;
        OgStr *s;
        OgArr *a;
        uint32_t h; /* handle узла */
        int fn;     /* индекс функции */
        int nat;    /* id встроенной функции */
    } u;
} OgValue;

OgValue og_nil(void);
OgValue og_bool(int b);
OgValue og_num(double n);
OgValue og_strv(OgStr *s); /* забирает ссылку */
OgValue og_arrv(OgArr *a); /* забирает ссылку */
OgValue og_obj(uint32_t handle);
OgValue og_func(int fn);
OgValue og_native(int id);

OgStr *og_str_new(const char *s, int len);
OgStr *og_str_copy(const OgStr *s);
OgArr *og_arr_new(void);
void og_arr_push(OgArr *a, OgValue v); /* забирает ссылку у v */

void og_retain(OgValue v);
void og_release(OgValue v);
int og_truthy(OgValue v);
int og_equals(OgValue a, OgValue b);
OgStr *og_to_str(OgValue v); /* новая ссылка */
double og_to_num(OgValue v);

#endif /* OG_COMMON_H */
