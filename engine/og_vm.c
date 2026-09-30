/* ОГОРОД: ОгScript — лексер, однопроходный компилятор в байткод и
 * стековая виртуальная машина с подсчётом ссылок. Синтаксис похож на
 * GDScript: блоки задаются отступами, функции — ключевым словом func.
 *
 * Порядок разбора имени: локальная переменная → глобальная переменная
 * сценария → функция сценария → встроенная функция движка. */
#include "og_vm.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

OgVmHooks og_vm_hooks;

/* ------------------------------------------------------------------ */
/* Встроенные функции (коды NAT_* в og_vm.h)                          */
/* ------------------------------------------------------------------ */

static const struct { const char *name; int id; } BUILTINS[] = {
    { "print", NAT_PRINT }, { "str", NAT_STR }, { "num", NAT_NUM },
    { "len", NAT_LEN }, { "abs", NAT_ABS }, { "floor", NAT_FLOOR },
    { "ceil", NAT_CEIL }, { "round", NAT_ROUND }, { "min", NAT_MIN },
    { "max", NAT_MAX }, { "clamp", NAT_CLAMP }, { "sqrt", NAT_SQRT },
    { "rand", NAT_RAND }, { "randi", NAT_RANDI }, { "time", NAT_TIME },
    { "quit", NAT_QUIT }, { "get_node", NAT_GET_NODE },
    { "has_node", NAT_HAS_NODE }, { "child_count", NAT_CHILD_COUNT },
    { "get_child", NAT_GET_CHILD }, { "spawn", NAT_SPAWN },
    { "free_node", NAT_FREE_NODE }, { "overlaps", NAT_OVERLAPS },
    { "point_in", NAT_POINT_IN }, { "push", NAT_PUSH },
    { "play_sound", NAT_PLAY_SOUND }, { "dist", NAT_DIST },
};

/* ------------------------------------------------------------------ */
/* Лексер                                                              */
/* ------------------------------------------------------------------ */
enum {
    T_EOF = 0, T_NL, T_INDENT, T_DEDENT, T_NUM, T_STR, T_NAME, T_OP,
    T_FUNC, T_VAR, T_IF, T_ELIF, T_ELSE, T_WHILE, T_FOR, T_IN,
    T_BREAK, T_CONTINUE, T_RETURN, T_PASS, T_TRUE, T_FALSE, T_NULL,
    T_AND, T_OR, T_NOT, T_SELF
};

static const char *KW[] = {
    "func", "var", "if", "elif", "else", "while", "for", "in",
    "break", "continue", "return", "pass", "true", "false", "null",
    "and", "or", "not", "self"
};
enum { KW_COUNT = (int)(sizeof KW / sizeof KW[0]) };

typedef struct { int kind, line, op; double num; char *str; } Tok;

/* После каких операторов строка продолжается на следующей. Закрывающие
 * скобки и точка с запятой не продолжают строку. */
static int op_continues(int op) {
    switch (op) {
    case ',': case '(': case '[': case '.':
    case '+': case '-': case '*': case '/': case '%':
    case '=': case '<': case '>':
        return 1;
    default:
        return op == ('=' << 8 | '=') || op == ('!' << 8 | '=') ||
               op == ('<' << 8 | '=') || op == ('>' << 8 | '=') ||
               op == ('+' << 8 | '=') || op == ('-' << 8 | '=') ||
               op == ('*' << 8 | '=') || op == ('/' << 8 | '=');
    }
}

#define MAX_INDENT 64

static void lex_fail(char *err, size_t errsz, int line, const char *fmt, ...) {
    va_list ap;
    char msg[200];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(err, errsz, "строка %d: %s", line, msg);
}

static void free_tokens(Tok *t, int n) {
    if (!t) return;
    for (int i = 0; i < n; i++) free(t[i].str);
    free(t);
}

static int lex(const char *src, Tok **out, int *outn, char *err, size_t errsz) {
    int cap = 256, n = 0;
    Tok *t = (Tok *)malloc((size_t)cap * sizeof(Tok));
    int stack[MAX_INDENT];
    int depth = 0, line = 1, at_line_start = 1, continued = 0;
    const unsigned char *p = (const unsigned char *)src;
    if (!t) { snprintf(err, errsz, "нет памяти"); return -1; }

#define PUSH(tk) do { \
        if (n == cap) { cap *= 2; Tok *nt = (Tok *)realloc(t, (size_t)cap * sizeof(Tok)); \
            if (!nt) { free_tokens(t, n); snprintf(err, errsz, "нет памяти"); return -1; } t = nt; } \
        memset(&t[n], 0, sizeof(Tok)); t[n].line = line; t[n].kind = (tk); n++; \
    } while (0)

    while (*p) {
        if (at_line_start) {
            int spaces = 0;
            /* в продолжении строки отступы не значат отступы блока */
            while (*p == ' ' || (continued && *p == '\t')) { spaces++; p++; }
            if (*p == '\n') { line++; p++; continue; }       /* пустая строка */
            if (*p == '#') { while (*p && *p != '\n') p++; continue; }
            if (continued) {
                continued = 0;
                at_line_start = 0;
                continue;
            }
            if (*p == '\t') {
                lex_fail(err, errsz, line, "табуляция в отступах запрещена, используйте пробелы");
                free_tokens(t, n); return -1;
            }
            if (spaces > (depth ? stack[depth - 1] : 0)) {
                if (depth >= MAX_INDENT) {
                    lex_fail(err, errsz, line, "слишком глубокий отступ");
                    free_tokens(t, n); return -1;
                }
                stack[depth++] = spaces;
                PUSH(T_INDENT);
            } else {
                while (depth > 0 && spaces < stack[depth - 1]) {
                    depth--;
                    PUSH(T_DEDENT);
                }
                if (depth > 0 ? spaces != stack[depth - 1] : spaces != 0) {
                    lex_fail(err, errsz, line, "несогласованный отступ");
                    free_tokens(t, n); return -1;
                }
            }
            at_line_start = 0;
            continue;
        }
        unsigned char c = *p;
        if (c == ' ' || c == '\t' || c == '\r') { p++; continue; }
        if (c == '\n') {
            /* неявное продолжение: строка, кончающаяся оператором, запятой,
             * and/or или открытой скобкой, продолжается на следующей */
            int cont = n > 0 && (t[n - 1].kind == T_AND || t[n - 1].kind == T_OR ||
                                 (t[n - 1].kind == T_OP && op_continues(t[n - 1].op)));
            continued = cont;
            if (n > 0 && !cont &&
                t[n - 1].kind != T_NL && t[n - 1].kind != T_INDENT)
                PUSH(T_NL);
            line++; p++; at_line_start = 1; continue;
        }
        if (c == '#') {
            /* #RRGGBB / #AARRGGBB — цвет как число; иначе это комментарий */
            int digits = 0;
            const unsigned char *h = p + 1;
            while (h[digits] && isxdigit(h[digits]) && digits < 8) digits++;
            if (digits == 6 || digits == 8) {
                /* #RRGGBB пишется как в кадре 0xAABBGGRR */
                unsigned long v = strtoul((const char *)p + 1, NULL, 16);
                unsigned rr, gg, bb, aa = 255;
                if (digits == 8) {
                    aa = (unsigned)((v >> 24) & 0xFF); rr = (unsigned)((v >> 16) & 0xFF);
                    gg = (unsigned)((v >> 8) & 0xFF);  bb = (unsigned)(v & 0xFF);
                } else {
                    rr = (unsigned)((v >> 16) & 0xFF);
                    gg = (unsigned)((v >> 8) & 0xFF);  bb = (unsigned)(v & 0xFF);
                }
                unsigned long packed = ((unsigned long)aa << 24) | (bb << 16) |
                                       (gg << 8) | rr;
                PUSH(T_NUM);
                t[n - 1].num = (double)packed;
                p += 1 + digits;
                continue;
            }
            while (*p && *p != '\n') p++;
            continue;
        }
        if ((c >= '0' && c <= '9') || (c == '.' && p[1] >= '0' && p[1] <= '9')) {
            char *end;
            double v = strtod((const char *)p, &end);
            PUSH(T_NUM);
            t[n - 1].num = v;
            p = (const unsigned char *)end;
            continue;
        }
        /* имена допускают UTF-8: можно писать «вар утка = 1» кириллицей */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '_' || c >= 0x80) {
            const unsigned char *s = p;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '_' || *p >= 0x80) p++;
            int len = (int)(p - s);
            int kw = -1;
            for (int i = 0; i < KW_COUNT; i++)
                if ((int)strlen(KW[i]) == len && memcmp(KW[i], s, (size_t)len) == 0)
                    { kw = i; break; }
            if (kw >= 0) { PUSH(T_FUNC + kw); continue; }
            PUSH(T_NAME);
            t[n - 1].str = (char *)malloc((size_t)len + 1);
            memcpy(t[n - 1].str, s, (size_t)len);
            t[n - 1].str[len] = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            unsigned char quote = c;
            p++;
            char *buf = (char *)malloc(256);
            int bl = 0, bc = 256;
            if (!buf) { snprintf(err, errsz, "нет памяти"); free_tokens(t, n); return -1; }
            while (*p && *p != quote) {
                unsigned char ch = *p;
                if (ch == '\n') break;
                if (ch == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    case '\'': ch = '\''; break;
                    default: ch = *p; break;
                    }
                }
                if (bl + 1 >= bc) {
                    bc *= 2;
                    char *nb = (char *)realloc(buf, (size_t)bc);
                    if (!nb) { free(buf); free_tokens(t, n); snprintf(err, errsz, "нет памяти"); return -1; }
                    buf = nb;
                }
                buf[bl++] = (char)ch;
                p++;
            }
            if (*p != quote) {
                lex_fail(err, errsz, line, "незакрытая строка");
                free(buf); free_tokens(t, n); return -1;
            }
            p++;
            buf[bl] = 0;
            PUSH(T_STR);
            t[n - 1].str = buf;
            continue;
        }
        /* операторы */
        {
            int two = 0;
            if ((c == '=' || c == '!' || c == '<' || c == '>' ||
                 c == '+' || c == '-' || c == '*' || c == '/') &&
                p[1] == '=') two = 1;
            PUSH(T_OP);
            if (two) { t[n - 1].op = ((int)c << 8) | '='; p += 2; }
            else {
                if (strchr("+-*/%()[]{}.,:=<>", c) == NULL) {
                    lex_fail(err, errsz, line, "неизвестный символ '%c'", (char)c);
                    free_tokens(t, n); return -1;
                }
                t[n - 1].op = c;
                p++;
            }
            continue;
        }
    }
    if (n > 0 && t[n - 1].kind != T_NL && t[n - 1].kind != T_INDENT)
        PUSH(T_NL);
    while (depth > 0) { depth--; PUSH(T_DEDENT); }
    PUSH(T_EOF);
#undef PUSH
    *out = t;
    *outn = n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Байткод                                                             */
/* ------------------------------------------------------------------ */
enum {
    OP_NIL, OP_TRUE, OP_FALSE, OP_CONST, OP_NATIVE, OP_FUNCV, OP_SELF,
    OP_GETLOCAL, OP_SETLOCAL, OP_GETGLOBAL, OP_SETGLOBAL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_NOT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_JMP, OP_JFALSE, OP_JFALSE_KEEP, OP_JTRUE_KEEP,
    OP_POP, OP_DUP,
    OP_MAKEARRAY, OP_GETINDEX, OP_SETINDEX,
    OP_GETPROP, OP_SETPROP,
    OP_CALL, OP_CALLMETHOD,
    OP_RETURN, OP_RETURN_NIL
};

typedef struct { uint8_t op; int32_t a, b; int32_t line; } Ins;

#define MAX_PARAMS 8
#define MAX_LOCALS 128

typedef struct {
    char name[64];
    char params[MAX_PARAMS][32];
    int nargs;
    Ins *code;
    int ncode, cap;
    OgValue *konst;
    int nkonst, capk;
    char locals[MAX_LOCALS][32];
    int localdepth[MAX_LOCALS];
    int nlocals, maxlocals;
} Fn;

struct OgProgram {
    int refc;
    char path[256];
    Fn *funcs;
    int nfuncs, capf;
    char gnames[MAX_LOCALS][32];
    int nglobals;
    Fn *init; /* инициализация глобальных переменных (или NULL) */
};

typedef struct {
    int start;          /* адрес начала цикла */
    int br[128];        /* переходы break (патчатся на конец) */
    int nbr;
    int co[128];        /* переходы continue (-1 = допатчить позже) */
    int nco;
    int cont;           /* адрес цели continue, если уже известен */
} Loop;

typedef struct {
    Tok *t;
    int nt, pos;
    OgProgram *p;
    Fn *fn; /* текущая функция (или init) */
    char err[320];
    int failed;
    Loop loops[16];
    int nloops;
    int scope_depth;
} Cmp;

/* ---- утилиты компилятора ---- */

static void cfail_at(Cmp *c, int line, const char *fmt, ...) {
    if (c->failed) return;
    va_list ap;
    char msg[220];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(c->err, sizeof c->err, "строка %d: %s", line, msg);
    c->failed = 1;
}

#define CFAIL(c, ...) cfail_at((c), (c)->pos < (c)->nt ? (c)->t[(c)->pos].line : 0, __VA_ARGS__)

static Tok *peek(Cmp *c) { return &c->t[c->pos]; }
static Tok *advance(Cmp *c) { return &c->t[c->pos++]; }

static int eat_op(Cmp *c, int op) {
    if (peek(c)->kind == T_OP && peek(c)->op == op) { c->pos++; return 1; }
    return 0;
}

static int expect_op(Cmp *c, int op, const char *what) {
    if (eat_op(c, op)) return 1;
    CFAIL(c, "ожидалось %s", what);
    return 0;
}

static int emit(Cmp *c, int op, int a, int b) {
    Fn *fn = c->fn;
    if (fn->ncode == fn->cap) {
        fn->cap = fn->cap ? fn->cap * 2 : 32;
        fn->code = (Ins *)realloc(fn->code, (size_t)fn->cap * sizeof(Ins));
    }
    int at = fn->ncode++;
    fn->code[at].op = (uint8_t)op;
    fn->code[at].a = a;
    fn->code[at].b = b;
    fn->code[at].line = peek(c)->line;
    return at;
}

static void patch(Cmp *c, int at, int a) { c->fn->code[at].a = a; }
static int here(Cmp *c) { return c->fn->ncode; }

static int add_konst(Cmp *c, OgValue v) {
    Fn *fn = c->fn;
    for (int i = 0; i < fn->nkonst; i++)
        if (og_equals(fn->konst[i], v)) { og_release(v); return i; }
    if (fn->nkonst == fn->capk) {
        fn->capk = fn->capk ? fn->capk * 2 : 16;
        fn->konst = (OgValue *)realloc(fn->konst, (size_t)fn->capk * sizeof(OgValue));
    }
    fn->konst[fn->nkonst] = v; /* ссылка переходит пулу */
    return fn->nkonst++;
}

static int konst_num(Cmp *c, double v) { return add_konst(c, og_num(v)); }
static int konst_str(Cmp *c, const char *s, int len) {
    return add_konst(c, og_strv(og_str_new(s, len)));
}

static int add_local(Cmp *c, const char *name) {
    Fn *fn = c->fn;
    if (strcmp(name, "self") == 0) {
        CFAIL(c, "имя self зарезервировано");
        return -1;
    }
    for (int i = 0; i < fn->nlocals; i++)
        if (strcmp(fn->locals[i], name) == 0) {
            CFAIL(c, "переменная '%s' уже объявлена в этой области", name);
            return -1;
        }
    if (fn->nlocals >= MAX_LOCALS) {
        CFAIL(c, "слишком много локальных переменных");
        return -1;
    }
    int slot = fn->nlocals;
    snprintf(fn->locals[slot], 32, "%s", name);
    fn->localdepth[slot] = c->scope_depth;
    fn->nlocals++;
    if (fn->nlocals > fn->maxlocals) fn->maxlocals = fn->nlocals;
    return slot;
}

static int find_local(Cmp *c, const char *name) {
    Fn *fn = c->fn;
    for (int i = fn->nlocals - 1; i >= 0; i--)
        if (strcmp(fn->locals[i], name) == 0) return i;
    return -1;
}

static void scope_begin(Cmp *c) { c->scope_depth++; }

static void scope_end(Cmp *c) {
    Fn *fn = c->fn;
    while (fn->nlocals > 0 && fn->localdepth[fn->nlocals - 1] >= c->scope_depth)
        fn->nlocals--;
    c->scope_depth--;
}

static int find_global(Cmp *c, const char *name) {
    for (int i = 0; i < c->p->nglobals; i++)
        if (strcmp(c->p->gnames[i], name) == 0) return i;
    return -1;
}

static int find_func(Cmp *c, const char *name) {
    for (int i = 0; i < c->p->nfuncs; i++)
        if (strcmp(c->p->funcs[i].name, name) == 0) return i;
    return -1;
}

static int find_builtin(const char *name) {
    for (int i = 0; i < NAT_COUNT; i++)
        if (strcmp(BUILTINS[i].name, name) == 0) return BUILTINS[i].id;
    return -1;
}

/* Выпустить код чтения имени как значения. */
static int emit_name_get(Cmp *c, const char *name) {
    int slot = find_local(c, name);
    if (slot >= 0) { emit(c, OP_GETLOCAL, slot, 0); return 1; }
    slot = find_global(c, name);
    if (slot >= 0) { emit(c, OP_GETGLOBAL, slot, 0); return 1; }
    slot = find_func(c, name);
    if (slot >= 0) { emit(c, OP_FUNCV, slot, 0); return 1; }
    int nat = find_builtin(name);
    if (nat >= 0) { emit(c, OP_NATIVE, nat, 0); return 1; }
    CFAIL(c, "неизвестное имя '%s'", name);
    return 0;
}

/* ---- выражения ---- */

static void expr(Cmp *c);

static void parse_args(Cmp *c, int *argc) {
    *argc = 0;
    expect_op(c, '(', "'('");
    if (c->failed) return;
    if (eat_op(c, ')')) return;
    for (;;) {
        expr(c);
        if (c->failed) return;
        (*argc)++;
        if (eat_op(c, ',')) continue;
        expect_op(c, ')', "')'");
        return;
    }
}

static void primary(Cmp *c) {
    Tok *tk = peek(c);
    switch (tk->kind) {
    case T_NUM: advance(c); emit(c, OP_CONST, konst_num(c, tk->num), 0); return;
    case T_STR: advance(c);
        emit(c, OP_CONST, konst_str(c, tk->str, (int)strlen(tk->str)), 0);
        return;
    case T_TRUE: advance(c); emit(c, OP_TRUE, 0, 0); return;
    case T_FALSE: advance(c); emit(c, OP_FALSE, 0, 0); return;
    case T_NULL: advance(c); emit(c, OP_NIL, 0, 0); return;
    case T_SELF: advance(c); emit(c, OP_SELF, 0, 0); return;
    case T_NAME: advance(c); emit_name_get(c, tk->str); return;
    case T_OP:
        if (tk->op == '(') {
            advance(c);
            expr(c);
            expect_op(c, ')', "')'");
            return;
        }
        if (tk->op == '[') {
            advance(c);
            int n = 0;
            if (!eat_op(c, ']')) {
                for (;;) {
                    expr(c);
                    if (c->failed) return;
                    n++;
                    if (eat_op(c, ',')) continue;
                    expect_op(c, ']', "']'");
                    break;
                }
            }
            emit(c, OP_MAKEARRAY, n, 0);
            return;
        }
        break;
    default:
        break;
    }
    CFAIL(c, "ожидалось выражение");
}

static void postfix(Cmp *c) {
    primary(c);
    while (!c->failed) {
        Tok *tk = peek(c);
        if (tk->kind == T_OP && tk->op == '.') {
            advance(c);
            if (peek(c)->kind != T_NAME) { CFAIL(c, "ожидалось имя после '.'"); return; }
            char name[64];
            snprintf(name, sizeof name, "%s", peek(c)->str);
            advance(c);
            if (peek(c)->kind == T_OP && peek(c)->op == '(') {
                int argc;
                parse_args(c, &argc);
                if (c->failed) return;
                emit(c, OP_CALLMETHOD, konst_str(c, name, (int)strlen(name)), argc);
            } else {
                emit(c, OP_GETPROP, konst_str(c, name, (int)strlen(name)), 0);
            }
        } else if (tk->kind == T_OP && tk->op == '[') {
            advance(c);
            expr(c);
            if (c->failed) return;
            expect_op(c, ']', "']'");
            emit(c, OP_GETINDEX, 0, 0);
        } else if (tk->kind == T_OP && tk->op == '(') {
            int argc;
            parse_args(c, &argc);
            if (c->failed) return;
            emit(c, OP_CALL, argc, 0);
        } else {
            return;
        }
    }
}

static void unary(Cmp *c) {
    if (peek(c)->kind == T_OP && peek(c)->op == '-') {
        advance(c);
        unary(c);
        emit(c, OP_NEG, 0, 0);
        return;
    }
    postfix(c);
}

static void mul_expr(Cmp *c) {
    unary(c);
    while (!c->failed && peek(c)->kind == T_OP) {
        int op = peek(c)->op;
        if (op != '*' && op != '/' && op != '%') return;
        advance(c);
        unary(c);
        emit(c, op == '*' ? OP_MUL : op == '/' ? OP_DIV : OP_MOD, 0, 0);
    }
}

static void add_expr(Cmp *c) {
    mul_expr(c);
    while (!c->failed && peek(c)->kind == T_OP) {
        int op = peek(c)->op;
        if (op != '+' && op != '-') return;
        advance(c);
        mul_expr(c);
        emit(c, op == '+' ? OP_ADD : OP_SUB, 0, 0);
    }
}

static void cmp_expr(Cmp *c) {
    add_expr(c);
    while (!c->failed && peek(c)->kind == T_OP) {
        int op = peek(c)->op;
        int opcode = -1;
        if (op == ('=' << 8 | '=')) opcode = OP_EQ;
        else if (op == ('!' << 8 | '=')) opcode = OP_NE;
        else if (op == '<') opcode = OP_LT;
        else if (op == ('<' << 8 | '=')) opcode = OP_LE;
        else if (op == '>') opcode = OP_GT;
        else if (op == ('>' << 8 | '=')) opcode = OP_GE;
        else return;
        advance(c);
        add_expr(c);
        emit(c, opcode, 0, 0);
    }
}

static void not_expr(Cmp *c) {
    if (peek(c)->kind == T_NOT) {
        advance(c);
        not_expr(c);
        emit(c, OP_NOT, 0, 0);
        return;
    }
    cmp_expr(c);
}

static void and_expr(Cmp *c) {
    not_expr(c);
    while (!c->failed && peek(c)->kind == T_AND) {
        advance(c);
        int jf = emit(c, OP_JFALSE_KEEP, 0, 0);
        emit(c, OP_POP, 0, 0);
        not_expr(c);
        patch(c, jf, here(c));
    }
}

static void expr(Cmp *c) {
    and_expr(c);
    while (!c->failed && peek(c)->kind == T_OR) {
        advance(c);
        int jt = emit(c, OP_JTRUE_KEEP, 0, 0);
        emit(c, OP_POP, 0, 0);
        and_expr(c);
        patch(c, jt, here(c));
    }
}

/* ---- операторы ---- */

static void block(Cmp *c);
static void stmt(Cmp *c);

static void block(Cmp *c) {
    if (peek(c)->kind == T_NL) {
        advance(c);
        if (peek(c)->kind != T_INDENT) {
            CFAIL(c, "ожидался блок с отступом");
            return;
        }
        advance(c);
        scope_begin(c);
        while (!c->failed && peek(c)->kind != T_DEDENT && peek(c)->kind != T_EOF)
            stmt(c);
        scope_end(c);
        if (peek(c)->kind == T_DEDENT) advance(c);
        else CFAIL(c, "незакрытый блок (не хватает отступа назад)");
    } else {
        stmt(c); /* однострочный вариант: если x: return 1 */
    }
}

static void loop_begin(Cmp *c, int cont_at_start) {
    if (c->nloops >= 16) { CFAIL(c, "слишком глубокая вложенность циклов"); return; }
    Loop *l = &c->loops[c->nloops++];
    l->start = here(c);
    l->nbr = 0;
    l->nco = 0;
    l->cont = cont_at_start ? l->start : -1;
}

static void loop_end(Cmp *c, int end_addr) {
    Loop *l = &c->loops[--c->nloops];
    for (int i = 0; i < l->nbr; i++) patch(c, l->br[i], end_addr);
    for (int i = 0; i < l->nco; i++)
        if (c->fn->code[l->co[i]].a == -1) patch(c, l->co[i], end_addr);
}

static void if_stmt(Cmp *c) {
    advance(c); /* if */
    expr(c);
    if (c->failed) return;
    if (!expect_op(c, ':', "':' после условия")) return;
    int jf = emit(c, OP_JFALSE, 0, 0);
    block(c);
    int ends[48];
    int ne = 0;
    ends[ne++] = emit(c, OP_JMP, 0, 0);
    patch(c, jf, here(c));
    while (!c->failed && peek(c)->kind == T_ELIF) {
        advance(c);
        expr(c);
        if (c->failed) return;
        if (!expect_op(c, ':', "':' после условия")) return;
        jf = emit(c, OP_JFALSE, 0, 0);
        block(c);
        if (ne < 48) ends[ne++] = emit(c, OP_JMP, 0, 0);
        patch(c, jf, here(c));
    }
    if (!c->failed && peek(c)->kind == T_ELSE) {
        advance(c);
        if (!expect_op(c, ':', "':' после else")) return;
        block(c);
    }
    for (int i = 0; i < ne; i++) patch(c, ends[i], here(c));
}

static void while_stmt(Cmp *c) {
    advance(c);
    loop_begin(c, 1);
    if (c->failed) return;
    expr(c);
    if (c->failed) return;
    if (!expect_op(c, ':', "':' после условия")) return;
    int jf = emit(c, OP_JFALSE, 0, 0);
    block(c);
    emit(c, OP_JMP, c->loops[c->nloops - 1].start, 0);
    patch(c, jf, here(c));
    loop_end(c, here(c));
}

static void for_stmt(Cmp *c) {
    advance(c); /* for */
    if (peek(c)->kind != T_NAME) { CFAIL(c, "ожидалось имя переменной цикла"); return; }
    char var[32];
    snprintf(var, sizeof var, "%s", peek(c)->str);
    advance(c);
    if (peek(c)->kind != T_IN) { CFAIL(c, "ожидалось 'in'"); return; }
    advance(c);
    if (peek(c)->kind != T_NAME || strcmp(peek(c)->str, "range") != 0) {
        CFAIL(c, "цикл for поддерживает только range(...)");
        return;
    }
    advance(c);
    if (!expect_op(c, '(', "'(' после range")) return;
    scope_begin(c);
    /* скрытые имена уникальны для каждого цикла — так не конфликтуют
     * вложенные for-циклы */
    char hs[32], he[32];
    snprintf(hs, sizeof hs, "_start%d", c->nloops);
    snprintf(he, sizeof he, "_end%d", c->nloops);
    int s = add_local(c, hs);
    int e = add_local(c, he);
    if (c->failed) { scope_end(c); return; }
    expr(c); /* первый аргумент */
    if (c->failed) return;
    int two_args = 0;
    if (eat_op(c, ',')) { two_args = 1; expr(c); if (c->failed) return; }
    if (!expect_op(c, ')', "')'")) { scope_end(c); return; }
    if (two_args) {
        /* стек: начало, конец */
        emit(c, OP_SETLOCAL, e, 0);
        emit(c, OP_POP, 0, 0);
        emit(c, OP_SETLOCAL, s, 0);
        emit(c, OP_POP, 0, 0);
    } else {
        /* стек: конец; начало = 0 */
        emit(c, OP_SETLOCAL, e, 0);
        emit(c, OP_POP, 0, 0);
        emit(c, OP_CONST, konst_num(c, 0), 0);
        emit(c, OP_SETLOCAL, s, 0);
        emit(c, OP_POP, 0, 0);
    }
    int i = add_local(c, var);
    if (c->failed) { scope_end(c); return; }
    emit(c, OP_GETLOCAL, s, 0);
    emit(c, OP_SETLOCAL, i, 0);
    emit(c, OP_POP, 0, 0);
    loop_begin(c, 0);
    if (c->failed) { scope_end(c); return; }
    int start = here(c);
    c->loops[c->nloops - 1].start = start;
    emit(c, OP_GETLOCAL, i, 0);
    emit(c, OP_GETLOCAL, e, 0);
    emit(c, OP_LT, 0, 0);
    int jf = emit(c, OP_JFALSE, 0, 0);
    if (!expect_op(c, ':', "':' после for")) { scope_end(c); return; }
    block(c);
    int step = here(c);
    c->loops[c->nloops - 1].cont = step;
    /* continue, встретившиеся до этого места, ещё не знают шаг: в цикле
     * for они патчатся на step в момент встречи через cont ниже. */
    emit(c, OP_GETLOCAL, i, 0);
    emit(c, OP_CONST, konst_num(c, 1), 0);
    emit(c, OP_ADD, 0, 0);
    emit(c, OP_SETLOCAL, i, 0);
    emit(c, OP_POP, 0, 0);
    emit(c, OP_JMP, start, 0);
    patch(c, jf, here(c));
    /* continue из тела цикла ведут на шаг инкремента */
    for (int k = 0; k < c->loops[c->nloops - 1].nco; k++)
        if (c->fn->code[c->loops[c->nloops - 1].co[k]].a == -1)
            patch(c, c->loops[c->nloops - 1].co[k], step);
    loop_end(c, here(c));
    scope_end(c);
}

/* Разбор цели присваивания. Основание — имя, self или любое выражение
 * (например, вызов функции), последняя операция — '.свойство' или
 * '[индекс]. Возвращает 1, если получилось; код контейнера уже выпущен
 * (кроме простых имён и одиночного self). */
enum { LV_SIMPLE_LOCAL, LV_SIMPLE_GLOBAL, LV_PROP, LV_INDEX, LV_SELF_BASE };
typedef struct { int kind; int slot; int namek; } Lv;

static int try_lvalue(Cmp *c, Lv *lv) {
    Tok *tk = peek(c);
    int base_self = 0, base_local = -1, base_global = -1, base_value = 0;
    if (tk->kind == T_SELF) {
        advance(c);
        base_self = 1;
    } else if (tk->kind == T_NAME) {
        advance(c);
        Tok *q = peek(c);
        if (q->kind == T_OP && q->op == '(') {
            /* вызов как основание: get_node("X").text = ... */
            if (!emit_name_get(c, tk->str)) return 0;
            int argc;
            parse_args(c, &argc);
            if (c->failed) return 0;
            emit(c, OP_CALL, argc, 0);
            base_value = 1;
        } else {
            base_local = find_local(c, tk->str);
            if (base_local < 0) base_global = find_global(c, tk->str);
            if (base_local < 0 && base_global < 0) return 0;
        }
    } else {
        return 0;
    }

#define EMIT_BASE() do { \
        if (base_self) { emit(c, OP_SELF, 0, 0); base_self = 0; base_value = 1; } \
        else if (!base_value) { \
            emit(c, base_local >= 0 ? OP_GETLOCAL : OP_GETGLOBAL, \
                 base_local >= 0 ? base_local : base_global, 0); \
            base_local = base_global = -1; \
            base_value = 1; \
        } \
    } while (0)

    for (;;) {
        Tok *p = peek(c);
        if (p->kind == T_OP && p->op == '.') {
            if (c->pos + 1 >= c->nt || c->t[c->pos + 1].kind != T_NAME) return 0;
            EMIT_BASE();
            advance(c); /* . */
            char name[64];
            snprintf(name, sizeof name, "%s", peek(c)->str);
            advance(c);
            int nk = konst_str(c, name, (int)strlen(name));
            Tok *q = peek(c);
            if (q->kind == T_OP && q->op == '(') {
                /* метод в середине цепочки: результат можно дальше менять */
                int argc;
                parse_args(c, &argc);
                if (c->failed) return 0;
                emit(c, OP_CALLMETHOD, nk, argc);
                continue;
            }
            if (q->kind == T_OP && (q->op == '[' || q->op == '.')) {
                emit(c, OP_GETPROP, nk, 0);
                continue;
            }
            lv->kind = LV_PROP;
            lv->namek = nk;
            return 1;
        } else if (p->kind == T_OP && p->op == '[') {
            EMIT_BASE();
            advance(c); /* [ */
            expr(c);
            if (c->failed) return 0;
            if (!expect_op(c, ']', "']'")) return 0;
            Tok *q = peek(c);
            if (q->kind == T_OP && (q->op == '[' || q->op == '.')) {
                emit(c, OP_GETINDEX, 0, 0);
                continue;
            }
            lv->kind = LV_INDEX;
            return 1;
        } else {
            break;
        }
    }
#undef EMIT_BASE
    if (base_value) return 0; /* прочитали значение — присваивать некуда */
    if (base_self) { lv->kind = LV_SELF_BASE; return 1; }
    lv->kind = base_local >= 0 ? LV_SIMPLE_LOCAL : LV_SIMPLE_GLOBAL;
    lv->slot = base_local >= 0 ? base_local : base_global;
    return 1;
}

static void assign_stmt(Cmp *c) {
    int saved_pos = c->pos;
    int saved_code = c->fn->ncode;
    Lv lv;
    memset(&lv, 0, sizeof lv);
    int ok = try_lvalue(c, &lv);
    if (c->failed) return; /* настоящая ошибка разбора — не откатываться */
    if (ok) {
        int op = peek(c)->kind == T_OP ? peek(c)->op : 0;
        int plain = (op == '=');
        int compound = (op == ('+' << 8 | '=') || op == ('-' << 8 | '=') ||
                        op == ('*' << 8 | '=') || op == ('/' << 8 | '='));
        if (plain || compound) {
            advance(c);
            int arith = compound ? (op == ('+' << 8 | '=') ? OP_ADD :
                                    op == ('-' << 8 | '=') ? OP_SUB :
                                    op == ('*' << 8 | '=') ? OP_MUL : OP_DIV) : 0;
            if (lv.kind == LV_SIMPLE_LOCAL || lv.kind == LV_SIMPLE_GLOBAL) {
                int getop = lv.kind == LV_SIMPLE_LOCAL ? OP_GETLOCAL : OP_GETGLOBAL;
                int setop = lv.kind == LV_SIMPLE_LOCAL ? OP_SETLOCAL : OP_SETGLOBAL;
                if (compound) emit(c, getop, lv.slot, 0);
                expr(c);
                if (c->failed) return;
                if (compound) emit(c, arith, 0, 0);
                emit(c, setop, lv.slot, 0);
            } else if (lv.kind == LV_PROP) {
                /* контейнер уже в стеке */
                if (compound) {
                    if (lv.kind == LV_PROP) emit(c, OP_DUP, 0, 0);
                    emit(c, OP_GETPROP, lv.namek, 0);
                    expr(c);
                    if (c->failed) return;
                    emit(c, arith, 0, 0);
                } else {
                    expr(c);
                    if (c->failed) return;
                }
                emit(c, OP_SETPROP, lv.namek, 0);
            } else if (lv.kind == LV_INDEX) {
                if (compound) {
                    CFAIL(c, "составное присваивание по индексу не поддерживается");
                    return;
                }
                expr(c);
                if (c->failed) return;
                emit(c, OP_SETINDEX, 0, 0);
            } else { /* LV_SELF_BASE без цепочки: присваивать self нельзя */
                CFAIL(c, "нельзя присвоить значение self");
                return;
            }
            emit(c, OP_POP, 0, 0);
            if (peek(c)->kind == T_NL) advance(c);
            return;
        }
    }
    /* не присваивание — откатываемся и разбираем выражение-оператор */
    c->failed = 0;
    c->err[0] = 0;
    c->pos = saved_pos;
    c->fn->ncode = saved_code;
    expr(c);
    if (c->failed) return;
    emit(c, OP_POP, 0, 0);
    if (peek(c)->kind == T_NL) advance(c);
    else if (peek(c)->kind != T_EOF && peek(c)->kind != T_DEDENT)
        CFAIL(c, "ожидался перевод строки");
}

static void stmt(Cmp *c) {
    if (c->failed) return;
    Tok *tk = peek(c);
    switch (tk->kind) {
    case T_NL: advance(c); return; /* пустая строка внутри блока */
    case T_VAR: {
        advance(c);
        if (peek(c)->kind != T_NAME) { CFAIL(c, "ожидалось имя переменной"); return; }
        char name[32];
        snprintf(name, sizeof name, "%s", peek(c)->str);
        advance(c);
        int slot = add_local(c, name);
        if (c->failed) return;
        if (eat_op(c, '=')) {
            expr(c);
            if (c->failed) return;
            emit(c, OP_SETLOCAL, slot, 0);
            emit(c, OP_POP, 0, 0);
        } else {
            emit(c, OP_NIL, 0, 0);
            emit(c, OP_SETLOCAL, slot, 0);
            emit(c, OP_POP, 0, 0);
        }
        if (peek(c)->kind == T_NL) advance(c);
        return;
    }
    case T_IF: if_stmt(c); return;
    case T_WHILE: while_stmt(c); return;
    case T_FOR: for_stmt(c); return;
    case T_RETURN: {
        if (c->fn == NULL || c->fn->name[0] == 0) {
            CFAIL(c, "return вне функции");
            return;
        }
        advance(c);
        if (peek(c)->kind == T_NL || peek(c)->kind == T_EOF) {
            emit(c, OP_RETURN_NIL, 0, 0);
        } else {
            expr(c);
            if (c->failed) return;
            emit(c, OP_RETURN, 0, 0);
        }
        if (peek(c)->kind == T_NL) advance(c);
        return;
    }
    case T_BREAK: {
        advance(c);
        if (c->nloops == 0) { CFAIL(c, "break вне цикла"); return; }
        Loop *l = &c->loops[c->nloops - 1];
        if (l->nbr < 128) l->br[l->nbr++] = emit(c, OP_JMP, 0, 0);
        if (peek(c)->kind == T_NL) advance(c);
        return;
    }
    case T_CONTINUE: {
        advance(c);
        if (c->nloops == 0) { CFAIL(c, "continue вне цикла"); return; }
        Loop *l = &c->loops[c->nloops - 1];
        int target = l->cont >= 0 ? l->cont : -1; /* -1 допатчится в for */
        if (l->nco < 128) l->co[l->nco++] = emit(c, OP_JMP, target, 0);
        if (peek(c)->kind == T_NL) advance(c);
        return;
    }
    case T_PASS:
        advance(c);
        if (peek(c)->kind == T_NL) advance(c);
        return;
    case T_FUNC:
        CFAIL(c, "func объявляется только на верхнем уровне сценария");
        return;
    default:
        assign_stmt(c);
        return;
    }
}

/* ---- программа: два прохода ---- */

static Fn *new_fn(OgProgram *p, const char *name) {
    if (p->nfuncs == p->capf) {
        p->capf = p->capf ? p->capf * 2 : 8;
        p->funcs = (Fn *)realloc(p->funcs, (size_t)p->capf * sizeof(Fn));
    }
    Fn *fn = &p->funcs[p->nfuncs++];
    memset(fn, 0, sizeof *fn);
    snprintf(fn->name, sizeof fn->name, "%s", name);
    return fn;
}

static void fn_free(Fn *fn) {
    free(fn->code);
    for (int i = 0; i < fn->nkonst; i++) og_release(fn->konst[i]);
    free(fn->konst);
}

/* Регистрация: собрать имена функций и глобальных переменных. */
static void pass_register(Cmp *c) {
    int depth = 0;
    for (int i = 0; i < c->nt; i++) {
        Tok *tk = &c->t[i];
        if (tk->kind == T_INDENT) depth++;
        else if (tk->kind == T_DEDENT) depth--;
        else if (depth == 0 && tk->kind == T_FUNC) {
            if (i + 1 >= c->nt || c->t[i + 1].kind != T_NAME) {
                cfail_at(c, tk->line, "ожидалось имя функции после func");
                return;
            }
            Fn *fn = new_fn(c->p, c->t[i + 1].str);
            int j = i + 2;
            if (j < c->nt && c->t[j].kind == T_OP && c->t[j].op == '(') {
                j++;
                while (j < c->nt && !(c->t[j].kind == T_OP && c->t[j].op == ')')) {
                    if (c->t[j].kind == T_NAME) {
                        if (fn->nargs < MAX_PARAMS)
                            snprintf(fn->params[fn->nargs++], 32, "%s", c->t[j].str);
                        else {
                            cfail_at(c, c->t[j].line, "слишком много параметров");
                            return;
                        }
                    }
                    j++;
                }
            }
        } else if (depth == 0 && tk->kind == T_VAR) {
            if (i + 1 >= c->nt || c->t[i + 1].kind != T_NAME) {
                cfail_at(c, tk->line, "ожидалось имя переменной после var");
                return;
            }
            if (c->p->nglobals >= MAX_LOCALS) {
                cfail_at(c, tk->line, "слишком много глобальных переменных");
                return;
            }
            snprintf(c->p->gnames[c->p->nglobals++], 32, "%s", c->t[i + 1].str);
        }
    }
}

static void compile_func_body(Cmp *c, int fi) {
    Fn *fn = &c->p->funcs[fi];
    c->fn = fn;
    c->scope_depth = 1;
    fn->nlocals = 0;
    fn->maxlocals = 0;
    for (int i = 0; i < fn->nargs; i++) {
        snprintf(fn->locals[fn->nlocals], 32, "%s", fn->params[i]);
        fn->localdepth[fn->nlocals] = 1;
        fn->nlocals++;
    }
    fn->maxlocals = fn->nlocals;
    /* позиция уже стоит после ':' заголовка */
    block(c);
    if (c->failed) return;
    if (fn->ncode == 0 ||
        (fn->code[fn->ncode - 1].op != OP_RETURN &&
         fn->code[fn->ncode - 1].op != OP_RETURN_NIL))
        emit(c, OP_RETURN_NIL, 0, 0);
    c->fn = NULL;
}

/* Пропустить заголовочную часть func ... : (позиция на T_FUNC). */
static void skip_func_header(Cmp *c) {
    advance(c);                    /* func */
    if (peek(c)->kind == T_NAME) advance(c);
    if (peek(c)->kind == T_OP && peek(c)->op == '(') {
        while (peek(c)->kind != T_EOF &&
               !(peek(c)->kind == T_OP && peek(c)->op == ')'))
            advance(c);
        if (peek(c)->kind == T_OP) advance(c); /* ) */
    }
    expect_op(c, ':', "':' после заголовка функции");
}

static void pass_compile(Cmp *c) {
    int depth = 0;
    while (c->pos < c->nt && !c->failed) {
        Tok *tk = peek(c);
        if (tk->kind == T_INDENT) { depth++; advance(c); continue; }
        if (tk->kind == T_DEDENT) { depth--; advance(c); continue; }
        if (depth != 0) { advance(c); continue; }
        if (tk->kind == T_EOF || tk->kind == T_NL) { advance(c); continue; }
        if (tk->kind == T_FUNC) {
            char name[64];
            snprintf(name, sizeof name, "%s", c->t[c->pos + 1].str);
            int fi = find_func(c, name);
            skip_func_header(c);
            if (c->failed) return;
            compile_func_body(c, fi);
            continue;
        }
        if (tk->kind == T_VAR) {
            /* инициализатор глобальной переменной — в служебную функцию */
            advance(c);
            if (peek(c)->kind != T_NAME) { CFAIL(c, "ожидалось имя переменной"); return; }
            char name[32];
            snprintf(name, sizeof name, "%s", peek(c)->str);
            advance(c);
            int slot = find_global(c, name);
            if (eat_op(c, '=')) {
                if (!c->p->init) {
                    c->p->init = (Fn *)calloc(1, sizeof(Fn));
                    snprintf(c->p->init->name, 64, "@init");
                }
                c->fn = c->p->init;
                c->scope_depth = 1;
                expr(c);
                if (c->failed) return;
                emit(c, OP_SETGLOBAL, slot, 0);
                emit(c, OP_POP, 0, 0);
                c->fn = NULL;
            }
            if (peek(c)->kind == T_NL) advance(c);
            continue;
        }
        CFAIL(c, "на верхнем уровне допустимы только func и var");
        return;
    }
}

OgProgram *og_compile(const char *path, const char *src, char *err, size_t errsz) {
    Tok *t = NULL;
    int nt = 0;
    if (errsz > 0) err[0] = 0;
    if (lex(src, &t, &nt, err, errsz) != 0) {
        char tmp[320];
        snprintf(tmp, sizeof tmp, "%s", err);
        snprintf(err, errsz, "%.120s: %s", path ? path : "<сценарий>", tmp);
        return NULL;
    }

    OgProgram *p = (OgProgram *)calloc(1, sizeof(OgProgram));
    if (!p) { snprintf(err, errsz, "нет памяти"); return NULL; }
    p->refc = 1;
    snprintf(p->path, sizeof p->path, "%s", path ? path : "<сценарий>");

    Cmp c;
    memset(&c, 0, sizeof c);
    c.t = t;
    c.nt = nt;
    c.p = p;

    pass_register(&c);
    if (!c.failed) {
        c.pos = 0;
        pass_compile(&c);
    }
    if (c.failed) {
        snprintf(err, errsz, "%s: %s", p->path, c.err);
        free_tokens(t, nt);
        og_program_unref(p);
        return NULL;
    }
    free_tokens(t, nt);
    return p;
}

OgProgram *og_program_ref(OgProgram *p) { if (p) p->refc++; return p; }

void og_program_unref(OgProgram *p) {
    if (!p || --p->refc > 0) return;
    for (int i = 0; i < p->nfuncs; i++) fn_free(&p->funcs[i]);
    free(p->funcs);
    if (p->init) { fn_free(p->init); free(p->init); }
    free(p);
}

int og_program_find_func(OgProgram *p, const char *name) {
    if (!p) return -1;
    for (int i = 0; i < p->nfuncs; i++)
        if (strcmp(p->funcs[i].name, name) == 0) return i;
    return -1;
}

int og_program_global_slot(OgProgram *p, const char *name) {
    if (!p) return -1;
    for (int i = 0; i < p->nglobals; i++)
        if (strcmp(p->gnames[i], name) == 0) return i;
    return -1;
}

int og_program_num_globals(OgProgram *p) { return p ? p->nglobals : 0; }
const char *og_program_path(OgProgram *p) { return p ? p->path : ""; }

/* ------------------------------------------------------------------ */
/* Виртуальная машина                                                  */
/* ------------------------------------------------------------------ */

#define OG_STACK 1024
#define OG_FRAMES 128

typedef struct {
    Fn *fn;
    int ip;
    int base;
    OgScriptInst *inst;
} Frame;

struct OgVm {
    OgValue stack[OG_STACK];
    int sp;
    Frame frames[OG_FRAMES];
    int fp;
    char err[256];
    int failed;
    int cur_line;
};

static OgVm g_vm;

static void vm_panic(OgVm *vm, const char *fmt, ...) {
    if (vm->failed) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(vm->err, sizeof vm->err, fmt, ap);
    va_end(ap);
    vm->failed = 1;
}

const char *og_vm_error(OgVm *vm) { return vm->err; }

void og_vm_fail(OgVm *vm, const char *msg) {
    if (vm->failed) return;
    snprintf(vm->err, sizeof vm->err, "%s", msg);
    vm->failed = 1;
}

static void vm_push(OgVm *vm, OgValue v) {
    if (vm->sp >= OG_STACK) { vm_panic(vm, "переполнение стека"); return; }
    vm->stack[vm->sp++] = v;
}

static OgValue vm_pop(OgVm *vm) {
    if (vm->sp <= 0) { vm_panic(vm, "внутренняя ошибка стека"); return og_nil(); }
    return vm->stack[--vm->sp];
}

static OgValue run_loop(OgVm *vm, int entry_fp);

static int vm_run_fn(OgVm *vm, OgScriptInst *inst, Fn *fn,
                     const OgValue *args, int nargs, OgValue *out) {
    if (vm->fp >= OG_FRAMES) {
        vm_panic(vm, "слишком глубокая рекурсия");
        return -1;
    }
    if (nargs != fn->nargs) {
        vm_panic(vm, "функция '%s' ждёт %d аргументов, передано %d",
                 fn->name, fn->nargs, nargs);
        return -1;
    }
    for (int i = 0; i < nargs; i++) {
        OgValue v = args[i];
        og_retain(v);
        vm_push(vm, v);
    }
    for (int i = nargs; i < fn->maxlocals; i++) vm_push(vm, og_nil());
    if (vm->failed) return -1;
    Frame *fr = &vm->frames[vm->fp++];
    fr->fn = fn;
    fr->ip = 0;
    fr->base = vm->sp - fn->maxlocals;
    fr->inst = inst;
    int entry = vm->fp - 1;
    OgValue ret = run_loop(vm, entry);
    if (vm->failed) { og_release(ret); return -1; }
    if (out) *out = ret;
    else og_release(ret);
    return 1;
}

/* Завершить верхний фрейм: освободить локальные, вернуть результат. */
static OgValue vm_do_return(OgVm *vm, OgValue ret) {
    Frame *fr = &vm->frames[vm->fp - 1];
    for (int i = fr->base; i < vm->sp; i++) og_release(vm->stack[i]);
    vm->sp = fr->base;
    vm->fp--;
    vm_push(vm, ret); /* результат: ссылка принадлежит стеку */
    return ret;
}

static OgValue run_loop(OgVm *vm, int entry_fp) {
    for (;;) {
        if (vm->failed) {
            /* раскрутить фреймы до входного */
            while (vm->fp > entry_fp) vm_do_return(vm, og_nil());
            return og_nil();
        }
        Frame *fr = &vm->frames[vm->fp - 1];
        Fn *fn = fr->fn;
        if (fr->ip >= fn->ncode) {
            vm_do_return(vm, og_nil());
            if (vm->fp <= entry_fp) return vm_pop(vm);
            continue;
        }
        Ins in = fn->code[fr->ip++];
        vm->cur_line = in.line;
        switch (in.op) {
        case OP_NIL: vm_push(vm, og_nil()); break;
        case OP_TRUE: vm_push(vm, og_bool(1)); break;
        case OP_FALSE: vm_push(vm, og_bool(0)); break;
        case OP_CONST: {
            OgValue v = fn->konst[in.a];
            og_retain(v);
            vm_push(vm, v);
            break;
        }
        case OP_NATIVE: vm_push(vm, og_native(in.a)); break;
        case OP_FUNCV: vm_push(vm, og_func(in.a)); break;
        case OP_SELF: {
            if (!fr->inst || !fr->inst->self) {
                vm_panic(vm, "self доступен только в сценарии узла");
                break;
            }
            extern uint32_t og_node_handle(const struct OgNode *n);
            vm_push(vm, og_obj(og_node_handle(fr->inst->self)));
            break;
        }
        case OP_GETLOCAL: {
            OgValue v = vm->stack[fr->base + in.a];
            og_retain(v);
            vm_push(vm, v);
            break;
        }
        case OP_SETLOCAL: {
            OgValue v = vm->stack[vm->sp - 1]; /* не снимаем: присваивание — выражение */
            og_retain(v);
            og_release(vm->stack[fr->base + in.a]);
            vm->stack[fr->base + in.a] = v;
            break;
        }
        case OP_GETGLOBAL: {
            OgScriptInst *inst = fr->inst;
            if (!inst || in.a >= inst->nglobals) { vm_panic(vm, "нет глобальной переменной"); break; }
            OgValue v = inst->globals[in.a];
            og_retain(v);
            vm_push(vm, v);
            break;
        }
        case OP_SETGLOBAL: {
            OgScriptInst *inst = fr->inst;
            if (!inst || in.a >= inst->nglobals) { vm_panic(vm, "нет глобальной переменной"); break; }
            OgValue v = vm->stack[vm->sp - 1];
            og_retain(v);
            og_release(inst->globals[in.a]);
            inst->globals[in.a] = v;
            break;
        }
        case OP_ADD: {
            OgValue b = vm_pop(vm), a = vm_pop(vm);
            if (a.t == OG_NUM && b.t == OG_NUM) vm_push(vm, og_num(a.u.n + b.u.n));
            else if (a.t == OG_STR && b.t == OG_STR) {
                /* склейка строк: сначала копируем в буфер, потом строка */
                int len = a.u.s->len + b.u.s->len;
                char *buf = (char *)malloc((size_t)len + 1);
                OgStr *ns = buf ? og_str_new(buf, len) : NULL;
                if (ns) {
                    memcpy(ns->data, a.u.s->data, (size_t)a.u.s->len);
                    memcpy(ns->data + a.u.s->len, b.u.s->data, (size_t)b.u.s->len);
                }
                free(buf);
                vm_push(vm, ns ? og_strv(ns) : og_nil());
            } else {
                vm_panic(vm, "складывать можно только числа с числами и строки со строками");
            }
            og_release(a);
            og_release(b);
            break;
        }
        case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
            OgValue b = vm_pop(vm), a = vm_pop(vm);
            if (a.t != OG_NUM || b.t != OG_NUM) {
                vm_panic(vm, "арифметика работает только с числами");
            } else if ((in.op == OP_DIV || in.op == OP_MOD) && b.u.n == 0.0) {
                vm_panic(vm, "деление на ноль");
            } else {
                double r = in.op == OP_SUB ? a.u.n - b.u.n :
                           in.op == OP_MUL ? a.u.n * b.u.n :
                           in.op == OP_DIV ? a.u.n / b.u.n :
                           fmod(a.u.n, b.u.n);
                vm_push(vm, og_num(r));
            }
            og_release(a);
            og_release(b);
            break;
        }
        case OP_NEG: {
            OgValue v = vm_pop(vm);
            if (v.t == OG_NUM) vm_push(vm, og_num(-v.u.n));
            else vm_panic(vm, "унарный минус работает только с числами");
            og_release(v);
            break;
        }
        case OP_NOT: {
            OgValue v = vm_pop(vm);
            vm_push(vm, og_bool(!og_truthy(v)));
            og_release(v);
            break;
        }
        case OP_EQ: case OP_NE: {
            OgValue b = vm_pop(vm), a = vm_pop(vm);
            int eq = og_equals(a, b);
            vm_push(vm, og_bool(in.op == OP_EQ ? eq : !eq));
            og_release(a);
            og_release(b);
            break;
        }
        case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            OgValue b = vm_pop(vm), a = vm_pop(vm);
            if (a.t != OG_NUM || b.t != OG_NUM) {
                vm_panic(vm, "сравнивать по порядку можно только числа");
                vm_push(vm, og_bool(0));
            } else {
                int r = in.op == OP_LT ? a.u.n < b.u.n :
                        in.op == OP_LE ? a.u.n <= b.u.n :
                        in.op == OP_GT ? a.u.n > b.u.n : a.u.n >= b.u.n;
                vm_push(vm, og_bool(r));
            }
            og_release(a);
            og_release(b);
            break;
        }
        case OP_JMP: fr->ip = in.a; break;
        case OP_JFALSE: {
            OgValue v = vm_pop(vm);
            if (!og_truthy(v)) fr->ip = in.a;
            og_release(v);
            break;
        }
        case OP_JFALSE_KEEP: {
            if (!og_truthy(vm->stack[vm->sp - 1])) fr->ip = in.a;
            break;
        }
        case OP_JTRUE_KEEP: {
            if (og_truthy(vm->stack[vm->sp - 1])) fr->ip = in.a;
            break;
        }
        case OP_POP: { OgValue v = vm_pop(vm); og_release(v); break; }
        case OP_DUP: {
            OgValue v = vm->stack[vm->sp - 1];
            og_retain(v);
            vm_push(vm, v);
            break;
        }
        case OP_MAKEARRAY: {
            OgArr *a = og_arr_new();
            if (!a) { vm_panic(vm, "нет памяти"); break; }
            /* элементы лежат в стеке в прямом порядке; забираем их ссылки */
            if (a->cap < in.a) {
                a->items = (OgValue *)malloc(sizeof(OgValue) *
                                           (size_t)(in.a > 0 ? in.a : 1));
                if (!a->items) { free(a); vm_panic(vm, "нет памяти"); break; }
                a->cap = in.a;
            }
            for (int i = in.a - 1; i >= 0; i--) a->items[i] = vm_pop(vm);
            a->len = in.a;
            vm_push(vm, og_arrv(a));
            break;
        }
        case OP_GETINDEX: {
            OgValue idx = vm_pop(vm), arr = vm_pop(vm);
            if (arr.t != OG_ARR) vm_panic(vm, "индексация доступна только для массивов");
            else if (idx.t != OG_NUM) vm_panic(vm, "индекс должен быть числом");
            else {
                int i = (int)idx.u.n;
                if (i < 0 || i >= arr.u.a->len)
                    vm_panic(vm, "индекс %d вне диапазона (длина %d)", i, arr.u.a->len);
                else {
                    OgValue v = arr.u.a->items[i];
                    og_retain(v);
                    vm_push(vm, v);
                }
            }
            og_release(arr);
            og_release(idx);
            break;
        }
        case OP_SETINDEX: {
            OgValue val = vm_pop(vm), idx = vm_pop(vm), arr = vm_pop(vm);
            if (arr.t != OG_ARR) vm_panic(vm, "индексация доступна только для массивов");
            else if (idx.t != OG_NUM) vm_panic(vm, "индекс должен быть числом");
            else {
                int i = (int)idx.u.n;
                if (i < 0 || i >= arr.u.a->len)
                    vm_panic(vm, "индекс %d вне диапазона (длина %d)", i, arr.u.a->len);
                else {
                    og_release(arr.u.a->items[i]);
                    arr.u.a->items[i] = val; /* ссылка перешла массиву */
                    og_retain(val);
                    vm_push(vm, val);
                    val = og_nil(); /* ссылка уже в массиве и в стеке */
                }
            }
            og_release(arr);
            og_release(idx);
            og_release(val);
            break;
        }
        case OP_GETPROP: {
            const char *name = fn->konst[in.a].u.s->data;
            OgValue obj = vm_pop(vm);
            if (obj.t != OG_OBJ || !og_vm_hooks.getprop) {
                vm_panic(vm, "свойство '%s' можно читать только у узла", name);
            } else {
                OgValue out = og_nil();
                if (og_vm_hooks.getprop(vm, obj.u.h, name, &out)) vm_push(vm, out);
                else vm_panic(vm, "у узла нет свойства '%s'", name);
            }
            og_release(obj);
            break;
        }
        case OP_SETPROP: {
            const char *name = fn->konst[in.a].u.s->data;
            OgValue val = vm_pop(vm), obj = vm_pop(vm);
            if (obj.t != OG_OBJ || !og_vm_hooks.setprop) {
                vm_panic(vm, "свойство '%s' можно менять только у узла", name);
            } else {
                OgValue copy = val;
                og_retain(copy); /* хук занимает значение, копия наша */
                int ok = og_vm_hooks.setprop(vm, obj.u.h, name, copy);
                og_release(copy);
                if (!ok) vm_panic(vm, "узел не принимает свойство '%s'", name);
                vm_push(vm, val);
                val = og_nil();
            }
            og_release(obj);
            og_release(val);
            break;
        }
        case OP_CALL: {
            int argc = in.a;
            OgValue args[16];
            if (argc > 16) { vm_panic(vm, "слишком много аргументов"); argc = 16; }
            for (int i = argc - 1; i >= 0; i--) args[i] = vm_pop(vm);
            OgValue callee = vm_pop(vm);
            OgValue result = og_nil();
            if (callee.t == OG_NATIVE) {
                if (og_vm_hooks.native)
                    result = og_vm_hooks.native(vm, callee.u.nat, args, argc);
            } else if (callee.t == OG_FUNC) {
                if (!fr->inst) vm_panic(vm, "нельзя вызвать функцию без экземпляра");
                else vm_run_fn(vm, fr->inst, &fr->inst->prog->funcs[callee.u.fn],
                               args, argc, &result);
            } else {
                vm_panic(vm, "это значение нельзя вызвать как функцию");
            }
            for (int i = 0; i < argc; i++) og_release(args[i]);
            og_release(callee);
            if (!vm->failed) vm_push(vm, result);
            break;
        }
        case OP_CALLMETHOD: {
            const char *name = fn->konst[in.a].u.s->data;
            int argc = in.b;
            OgValue args[16];
            if (argc > 16) { vm_panic(vm, "слишком много аргументов"); argc = 16; }
            for (int i = argc - 1; i >= 0; i--) args[i] = vm_pop(vm);
            OgValue obj = vm_pop(vm);
            OgValue result = og_nil();
            int handled = 0;
            if (obj.t == OG_OBJ && og_vm_hooks.method) {
                handled = og_vm_hooks.method(vm, obj.u.h, name, args, argc, &result);
                if (handled == 0) vm_panic(vm, "у узла нет метода '%s'", name);
            } else if (obj.t == OG_ARR) {
                if (strcmp(name, "len") == 0) { result = og_num(obj.u.a->len); handled = 1; }
                else if (strcmp(name, "push") == 0 && argc == 1) {
                    og_arr_push(obj.u.a, args[0]);
                    args[0] = og_nil(); /* ссылка ушла в массив */
                    handled = 1;
                } else if (strcmp(name, "pop") == 0 && argc == 0) {
                    if (obj.u.a->len > 0) result = obj.u.a->items[--obj.u.a->len];
                    else result = og_nil();
                    handled = 1;
                }
                if (!handled) vm_panic(vm, "у массива нет метода '%s'", name);
            } else if (obj.t == OG_STR) {
                if (strcmp(name, "len") == 0) { result = og_num(obj.u.s->len); handled = 1; }
                else vm_panic(vm, "у строки нет метода '%s'", name);
            } else {
                vm_panic(vm, "метод '%s' можно вызывать только у узла, массива или строки",
                         name);
            }
            for (int i = 0; i < argc; i++) og_release(args[i]);
            og_release(obj);
            if (!vm->failed && handled > 0) vm_push(vm, result);
            break;
        }
        case OP_RETURN: {
            OgValue v = vm_pop(vm);
            vm_do_return(vm, v);
            if (vm->fp <= entry_fp) return vm_pop(vm);
            break;
        }
        case OP_RETURN_NIL: {
            vm_do_return(vm, og_nil());
            if (vm->fp <= entry_fp) return vm_pop(vm);
            break;
        }
        default:
            vm_panic(vm, "неизвестный опкод %d", (int)in.op);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Экземпляры сценариев                                                */
/* ------------------------------------------------------------------ */

OgScriptInst *og_inst_new(OgProgram *p, struct OgNode *self) {
    if (!p) return NULL;
    OgScriptInst *inst = (OgScriptInst *)calloc(1, sizeof(OgScriptInst));
    if (!inst) return NULL;
    inst->prog = og_program_ref(p);
    inst->self = self;
    inst->nglobals = p->nglobals;
    inst->globals = (OgValue *)calloc((size_t)(p->nglobals > 0 ? p->nglobals : 1),
                                      sizeof(OgValue));
    for (int i = 0; i < inst->nglobals; i++) inst->globals[i] = og_nil();
    if (p->init) {
        OgVm *vm = &g_vm;
        vm->failed = 0;
        vm->err[0] = 0;
        vm_run_fn(vm, inst, p->init, NULL, 0, NULL);
        if (vm->failed) {
            inst->broken = 1;
            snprintf(inst->err, sizeof inst->err, "%.100s: строка %d: %.120s",
                     p->path, vm->cur_line, vm->err);
        }
    }
    return inst;
}

void og_inst_free(OgScriptInst *inst) {
    if (!inst) return;
    for (int i = 0; i < inst->nglobals; i++) og_release(inst->globals[i]);
    free(inst->globals);
    og_program_unref(inst->prog);
    free(inst);
}

int og_vm_call_instance(OgVm *vm, OgScriptInst *inst, int fi,
                        const OgValue *args, int nargs, OgValue *out) {
    if (!inst) return -1;
    if (inst->broken) {
        og_vm_fail(vm, "сценарий узла сломан более ранней ошибкой");
        return -1;
    }
    if (fi < 0 || fi >= inst->prog->nfuncs) return 0;
    int sp0 = vm->sp, fp0 = vm->fp;
    vm->failed = 0;
    vm->err[0] = 0;
    int r = vm_run_fn(vm, inst, &inst->prog->funcs[fi], args, nargs, out);
    if (r < 0 && vm->failed) {
        inst->broken = 1;
        snprintf(inst->err, sizeof inst->err, "%.100s: строка %d: %.120s",
                 inst->prog->path, vm->cur_line, vm->err);
        vm->sp = sp0;
        vm->fp = fp0;
    }
    return r;
}

int og_inst_call(OgScriptInst *inst, const char *func,
                 const OgValue *args, int nargs, OgValue *out) {
    if (!inst) return -1;
    int fi = og_program_find_func(inst->prog, func);
    if (fi < 0) return 0; /* такой функции у сценария нет — не ошибка */
    if (out) *out = og_nil();
    return og_vm_call_instance(&g_vm, inst, fi, args, nargs, out);
}

OgScriptInst *og_vm_current_inst(OgVm *vm) {
    if (vm->fp <= 0) return NULL;
    return vm->frames[vm->fp - 1].inst;
}
