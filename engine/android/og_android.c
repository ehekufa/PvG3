/* ОГОРОД: Android-слой движка.
 *
 * NativeActivity + native_app_glue + EGL/OpenGL ES 2.0 — так же, как в
 * src/android_main.c оригинальной игры. Движок рисует кадр в свой
 * программный буфер (og_rt_render), мы заливаем его в текстуру и
 * показываем полноэкранный квад. Касания пересчитываются в координаты
 * проекта.
 *
 * Файлы игр встроены в библиотеку (og_project_data.h), поэтому APK
 * ничего не распаковывает: движок читает их из виртуальной ФС. */
/* clock_gettime нужен и на POSIX для теста хоста. */
#define _POSIX_C_SOURCE 200809L

#include <android/log.h>
#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "font.h"
#include "og_image.h"
#include "og_runtime.h"
#include "og_vfs.h"
#include "og_project_data.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Ogorod", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Ogorod", __VA_ARGS__)

static const char *VS =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *FS =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "void main(){ gl_FragColor = texture2D(u_tex, vec2(v_uv.x, 1.0 - v_uv.y)); }\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof buf, 0, buf);
        LOGE("shader: %s", buf);
    }
    return s;
}

/* Две игры, встроенные в APK. */
static const struct { const char *dir; const char *title; const char *sub; } GAMES[] = {
    { "projects/oborona", "ОБОРОНА ГРЯДКИ", "утки, растения и монеты" },
    { "projects/kirpichi", "КИРПИЧИ",        "арканоид про кирпичи" },
};
#define GAME_COUNT ((int)(sizeof GAMES / sizeof GAMES[0]))

#define MENU_W 800
#define MENU_H 600

typedef struct {
    struct android_app *app;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int w, h;
    GLuint program, tex;
    int ready, resumed, focused;

    OgRuntime *rt;
    int menu;          /* показан выбор игры */
    int picked;        /* индекс выбранной игры, -1 если игра идёт */
    uint32_t *menu_fb;
    double t0;
    long frames;      /* сколько кадров показано (для тестов и отладки) */
} Og;

static Og G;

#ifdef OG_HOST_TEST
/* Снимок состояния на момент выхода: тест спрашивает уже после того,
 * как хост освободил движок. */
static int last_in_game, last_menu, last_index;
static long last_frames;
#endif

static void engine_term(Og *e) {
    if (e->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e->context != EGL_NO_CONTEXT) eglDestroyContext(e->display, e->context);
        if (e->surface != EGL_NO_SURFACE) eglDestroySurface(e->display, e->surface);
        eglTerminate(e->display);
    }
    e->display = EGL_NO_DISPLAY;
    e->context = EGL_NO_CONTEXT;
    e->surface = EGL_NO_SURFACE;
    e->ready = 0;
}

static int engine_init(Og *e) {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, 0, 0)) { LOGE("eglInitialize failed"); return 0; }
    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint num = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &num) || num < 1) {
        LOGE("no EGL config");
        return 0;
    }
    EGLNativeWindowType win = e->app->window;
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, win, NULL);
    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, NULL, ctxAttr);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE ||
        eglMakeCurrent(dpy, surf, surf, ctx) == EGL_FALSE) {
        LOGE("EGL surface/context failed");
        return 0;
    }
    e->display = dpy;
    e->surface = surf;
    e->context = ctx;
    e->w = ANativeWindow_getWidth(win);
    e->h = ANativeWindow_getHeight(win);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS);
    e->program = glCreateProgram();
    glAttachShader(e->program, vs);
    glAttachShader(e->program, fs);
    glLinkProgram(e->program);

    glGenTextures(1, &e->tex);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glViewport(0, 0, e->w, e->h);
    e->ready = 1;
    LOGI("Ogorod ready: window %dx%d", e->w, e->h);
    return 1;
}

/* Залить произвольный кадр и показать его (кадр может быть любого
 * размера — текстура пересоздаётся при смене размера). */
static void present(Og *e, const uint32_t *fb, int fw, int fh) {
    static int tex_w = 0, tex_h = 0;
    glBindTexture(GL_TEXTURE_2D, e->tex);
    if (tex_w != fw || tex_h != fh) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        tex_w = fw;
        tex_h = fh;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, fb);
    glUseProgram(e->program);
    glUniform1i(glGetUniformLocation(e->program, "u_tex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLint aloc = glGetAttribLocation(e->program, "a_pos");
    glVertexAttribPointer(aloc, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(aloc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(e->display, e->surface);
}

/* Кадр меню рисуется теми же примитивами движка. */
static void menu_render(Og *e) {
    uint32_t *fb = e->menu_fb;
    for (int i = 0; i < MENU_W * MENU_H; i++) fb[i] = 0xFF161A20u;
    const char *title = "ДВИЖОК ОГОРОД";
    font_draw(fb, MENU_W, MENU_H, MENU_W / 2 - font_width(6, title) / 2, 60, 6,
              0xFFFFFFFFu, title);
    const char *hint = "выбери игру";
    font_draw(fb, MENU_W, MENU_H, MENU_W / 2 - font_width(3, hint) / 2, 130, 3,
              0xFF8892A6u, hint);
    for (int i = 0; i < GAME_COUNT; i++) {
        int y = 200 + i * 160;
        uint32_t body = 0xFF232830u;
        og_fill(fb, MENU_W, MENU_H, 60, y, MENU_W - 120, 120, body);
        /* золотая рамка */
        og_fill(fb, MENU_W, MENU_H, 60, y, MENU_W - 120, 3, 0xFFF7C948u);
        og_fill(fb, MENU_W, MENU_H, 60, y + 117, MENU_W - 120, 3, 0xFFF7C948u);
        font_draw(fb, MENU_W, MENU_H, 90, y + 25, 4, 0xFFFFFFFFu, GAMES[i].title);
        font_draw(fb, MENU_W, MENU_H, 90, y + 75, 2, 0xFF8892A6u, GAMES[i].sub);
    }
    const char *foot = "игра написана на ОгScript и встроена в этот APK";
    font_draw(fb, MENU_W, MENU_H, MENU_W / 2 - font_width(2, foot) / 2,
              MENU_H - 40, 2, 0xFF6B7280u, foot);
    present(e, fb, MENU_W, MENU_H);
}

static int start_game(Og *e, int index) {
    if (index < 0 || index >= GAME_COUNT) return 0;
    if (e->rt) og_rt_free(e->rt);
    e->rt = og_rt_new(0, 0);
    if (!e->rt) return 0;
    if (!og_rt_load_project(e->rt, GAMES[index].dir)) {
        LOGE("не удалось загрузить %s: %s", GAMES[index].dir, e->rt->err);
        og_rt_free(e->rt);
        e->rt = NULL;
        return 0;
    }
    e->menu = 0;
    e->picked = index;
    LOGI("запущена игра %s (%dx%d)", GAMES[index].dir, e->rt->W, e->rt->H);
    return 1;
}

static void back_to_menu(Og *e) {
    if (e->rt) {
        og_rt_free(e->rt);
        e->rt = NULL;
    }
    e->menu = 1;
    e->picked = -1;
}

static void on_app_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) engine_init(&G);
        break;
    case APP_CMD_RESUME:   G.resumed = 1; break;
    case APP_CMD_PAUSE:    G.resumed = 0; break;
    case APP_CMD_GAINED_FOCUS: G.focused = 1; break;
    case APP_CMD_LOST_FOCUS:  G.focused = 0; break;
    case APP_CMD_TERM_WINDOW:
        G.ready = 0;
        engine_term(&G);
        break;
    default: break;
    }
}

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    if (!G.ready || !G.resumed || !G.focused) return 0;
    int action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float fx = AMotionEvent_getX(ev, 0);
    float fy = AMotionEvent_getY(ev, 0);
    if (G.menu || !G.rt) {
        /* координаты меню: окно 800x600, вписываем с сохранением пропорций */
        float scale_x = (float)MENU_W / (float)(G.w > 0 ? G.w : 1);
        float scale_y = (float)MENU_H / (float)(G.h > 0 ? G.h : 1);
        int mx = (int)(fx * scale_x);
        int my = (int)(fy * scale_y);
        if (action != AMOTION_EVENT_ACTION_DOWN) return 1;
        for (int i = 0; i < GAME_COUNT; i++) {
            int y = 200 + i * 160;
            if (mx >= 60 && mx < MENU_W - 60 && my >= y && my < y + 120) {
                if (!start_game(&G, i)) back_to_menu(&G);
                return 1;
            }
        }
        return 1;
    }
    int vx = (int)(fx * (float)G.rt->W / (float)(G.w > 0 ? G.w : 1));
    int vy = (int)(fy * (float)G.rt->H / (float)(G.h > 0 ? G.h : 1));
    if (action == AMOTION_EVENT_ACTION_DOWN)
        og_rt_input_press(G.rt, vx, vy);
    else if (action == AMOTION_EVENT_ACTION_MOVE)
        og_rt_input_move(G.rt, vx, vy);
    else if (action == AMOTION_EVENT_ACTION_UP)
        og_rt_input_release(G.rt, vx, vy);
    else if (action == AMOTION_EVENT_ACTION_CANCEL)
        og_rt_input_release(G.rt, vx, vy);
    return 1;
}

void android_main(struct android_app *app) {
    memset(&G, 0, sizeof G);
    G.app = app;
    G.picked = -1;
    G.menu = 1;
    app->onAppCmd = on_app_cmd;
    app->onInputEvent = on_input;

    og_project_install();
    font_init();
    G.menu_fb = (uint32_t *)calloc((size_t)MENU_W * MENU_H, sizeof(uint32_t));
    if (!G.menu_fb) {
        LOGE("нет памяти под кадр меню");
        return;
    }
    LOGI("встроено файлов игр: %d", og_vfs_count());

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    G.t0 = ts.tv_sec + ts.tv_nsec / 1e9;

    while (!app->destroyRequested) {
        int events;
        struct android_poll_source *src;
        int blocking = (G.ready && G.resumed && G.focused) ? 0 : -1;
        while (ALooper_pollOnce(blocking, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested) break;
        if (!G.ready || !G.resumed || !G.focused) continue;

        if (G.menu || !G.rt) {
            menu_render(&G);
            G.frames++;
            continue;
        }
        OgRuntime *rt = G.rt;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double t = ts.tv_sec + ts.tv_nsec / 1e9;
        float dt = (float)(t - G.t0);
        G.t0 = t;
        if (dt < 0) dt = 0;
        if (dt > 0.05f) dt = 0.05f;

        og_rt_step(rt, dt);
        if (rt->err[0]) {
            LOGE("ошибка сценария: %s", rt->err);
            rt->err[0] = 0;   /* не спамим в лог каждый кадр */
        }
        /* Игра вышла через quit(): показываем меню, чтобы можно было
         * запустить вторую игру, не переустанавливая APK. */
        if (rt->quit) back_to_menu(&G);
        og_rt_render(rt);
        present(&G, rt->fb, rt->W, rt->H);
        G.frames++;
    }

#ifdef OG_HOST_TEST
    /* снимок состояния до очистки: тест спрашивает уже после выхода */
    last_in_game = G.rt != NULL;
    last_menu = G.menu;
    last_index = G.picked;
    last_frames = G.frames;
#endif
    if (G.rt) {
        og_rt_free(G.rt);
        G.rt = NULL;
    }
    free(G.menu_fb);
    G.menu_fb = NULL;
    engine_term(&G);
    LOGI("Ogorod остановлен: показано кадров %ld", G.frames);
}

#ifdef OG_HOST_TEST
int og_host_test_in_game(void) { return last_in_game; }
int og_host_test_menu(void) { return last_menu; }
long og_host_test_frames(void) { return last_frames; }
const char *og_host_test_game(void) {
    return last_index >= 0 && last_index < GAME_COUNT ? GAMES[last_index].dir : "";
}
#endif
