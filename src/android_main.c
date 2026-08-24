/* android_main.c — Android platform layer.
 *
 * native_app_glue + EGL + OpenGL ES 2.0. The game renders into a software
 * framebuffer (see game.c); each frame we upload it to a texture and draw a
 * full-screen quad. Touch events are mapped to the game's virtual resolution.
 */
#include "game.h"

#include <android/log.h>
#include <android/input.h>
#include <android/native_window.h>
#include <android/native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <time.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "PvG3", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PvG3", __VA_ARGS__)

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
    if (!ok) { char b[1024]; glGetShaderInfoLog(s, sizeof b, 0, b); LOGE("shader: %s", b); }
    return s;
}

typedef struct {
    struct android_app *app;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int w, h;
    GLuint program, tex;
    int ready;
} Engine;

static Engine *G;

static void engine_term(Engine *e) {
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

static int engine_init(Engine *e) {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, 0, 0);

    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint num = 0;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &num);
    if (num < 1) { LOGE("no EGL config"); return 0; }

    EGLNativeWindowType win = e->app->window;
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, win, NULL);
    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, NULL, ctxAttr);
    if (eglMakeCurrent(dpy, surf, surf, ctx) == EGL_FALSE) { LOGE("eglMakeCurrent failed"); return 0; }

    e->display = dpy; e->surface = surf; e->context = ctx;
    e->w = ANativeWindow_getWidth(win);
    e->h = ANativeWindow_getHeight(win);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    e->program = prog;

    glGenTextures(1, &e->tex);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GAME_W, GAME_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glViewport(0, 0, e->w, e->h);
    e->ready = 1;
    LOGI("engine ready %dx%d", e->w, e->h);
    return 1;
}

static void engine_draw(Engine *e, const uint32_t *fb) {
    glBindTexture(GL_TEXTURE_2D, e->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GAME_W, GAME_H, GL_RGBA, GL_UNSIGNED_BYTE, fb);

    glUseProgram(e->program);
    GLint uloc = glGetUniformLocation(e->program, "u_tex");
    glUniform1i(uloc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, e->tex);

    static const float quad[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
    GLint aloc = glGetAttribLocation(e->program, "a_pos");
    glVertexAttribPointer(aloc, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(aloc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    eglSwapBuffers(e->display, e->surface);
}

static void on_app_cmd(struct android_app *app, int32_t cmd) {
    (void)app;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (G->app->window) engine_init(G);
        break;
    case APP_CMD_TERM_WINDOW:
        engine_term(G);
        break;
    default: break;
    }
}

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    if (!G->ready) return 0;
    int action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(ev, 0);
    float y = AMotionEvent_getY(ev, 0);
    int vx = (int)(x * GAME_W / G->w);
    int vy = (int)(y * GAME_H / G->h);
    if (action == AMOTION_EVENT_ACTION_DOWN) game_input_press(vx, vy);
    else if (action == AMOTION_EVENT_ACTION_UP) game_input_release(vx, vy);
    return 1;
}

void android_main(struct android_app *app) {
    Engine engine;
    memset(&engine, 0, sizeof engine);
    engine.app = app;
    G = &engine;

    app->onAppCmd = on_app_cmd;
    app->onInputEvent = on_input;

    game_init();

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double last = ts.tv_sec + ts.tv_nsec / 1e9;

    static uint32_t fb[GAME_W * GAME_H];

    while (!app->destroyRequested) {
        int events;
        struct android_poll_source *src;
        /* Block while we have no window; poll every frame once we are drawing. */
        while (ALooper_pollAll(engine.ready ? 0 : -1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested) break;

        if (!engine.ready) continue;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        double t = ts.tv_sec + ts.tv_nsec / 1e9;
        float dt = (float)(t - last);
        last = t;
        if (dt < 0) dt = 0;
        if (dt > 0.05f) dt = 0.05f;

        game_tick(dt, fb);
        engine_draw(&engine, fb);
    }

    engine_term(&engine);
}
