/* Реализация заглушек Android/EGL/GLES2 для теста Android-слоя движка.
 * Ничего не рисует: проверяется логика хоста, а картинка — содержимое
 * программного кадра движка. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "android/log.h"
#include "android/input.h"
#include "android/native_window.h"
#include "android_native_app_glue.h"
#include "EGL/egl.h"
#include "GLES2/gl2.h"

/* ---- лог ---- */
static int log_errors;

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)tag;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (prio >= ANDROID_LOG_ERROR) log_errors++;
    return 0;
}

void og_fake_log_reset(void) { log_errors = 0; }
int og_fake_log_errors(void) { return log_errors; }

/* ---- окно ---- */
static int win_w = 1280, win_h = 720;

int ANativeWindow_getWidth(void *win) { (void)win; return win_w; }
int ANativeWindow_getHeight(void *win) { (void)win; return win_h; }
void og_fake_window_set_size(int w, int h) { win_w = w; win_h = h; }

/* ---- ввод ---- */
#define FAKE_EVENTS 8
static struct { int type, action; float x, y; } evq[FAKE_EVENTS];
static int ev_count, ev_taken, ev_cur;   /* ev_cur — событие, о котором спрашивают */

int AInputEvent_getType(const AInputEvent *ev) {
    (void)ev;
    return AINPUT_EVENT_TYPE_MOTION;
}
int AMotionEvent_getAction(const AInputEvent *ev) {
    (void)ev;
    return evq[ev_cur].action;
}
float AMotionEvent_getX(const AInputEvent *ev, int i) {
    (void)ev; (void)i;
    return evq[ev_cur].x;
}
float AMotionEvent_getY(const AInputEvent *ev, int i) {
    (void)ev; (void)i;
    return evq[ev_cur].y;
}
void og_fake_input_push(int action, float x, float y) {
    if (ev_count >= FAKE_EVENTS) return;
    evq[ev_count].type = AINPUT_EVENT_TYPE_MOTION;
    evq[ev_count].action = action;
    evq[ev_count].x = x;
    evq[ev_count].y = y;
    ev_count++;
}
int og_fake_input_count(void) { return ev_count; }
const AInputEvent *og_fake_input_next(void) {
    if (ev_taken >= ev_count) return NULL;
    ev_taken++;
    return (const AInputEvent *)&evq[ev_taken - 1];
}
void og_fake_input_clear(void) { ev_count = ev_taken = ev_cur = 0; }

/* ---- цикл событий: проигрываем заданный сценарий и закрываем окно ----
 * Каждый вызов ALooper_pollOnce отдаёт один «событие»: окно и фокус,
 * затем кадры, касание и в конце запрос на закрытие. Когда сценарий
 * закончен, pollOnce возвращает -1, и хост крутит кадры сам. */
static int cfg_w = 1280, cfg_h = 720;
static int cfg_frames_before = 2, cfg_frames_after = 4;
static float cfg_x, cfg_y;
static int cfg_action = AMOTION_EVENT_ACTION_DOWN;
static int started, frames, touch_sent, finished, polls;

void og_fake_looper_configure(int w, int h, int frames_before_touch,
                              float touch_x, float touch_y, int touch_action,
                              int frames_after_touch) {
    cfg_w = w;
    cfg_h = h;
    cfg_frames_before = frames_before_touch;
    cfg_frames_after = frames_after_touch;
    cfg_x = touch_x;
    cfg_y = touch_y;
    cfg_action = touch_action;
    started = 0;
    frames = 0;
    touch_sent = 0;
    finished = 0;
    polls = 0;
    og_fake_window_set_size(w, h);
    og_fake_input_clear();
}

int og_fake_looper_started(void) { return started; }

static int fake_window_marker;

static void fake_step(struct android_app *app) {
    if (!started) {
        started = 1;
        app->window = (void *)&fake_window_marker;
        app->onAppCmd(app, APP_CMD_INIT_WINDOW);
        app->onAppCmd(app, APP_CMD_RESUME);
        app->onAppCmd(app, APP_CMD_GAINED_FOCUS);
        return;
    }
    if (frames == cfg_frames_before && !touch_sent) {
        touch_sent = 1;
        if (app->onInputEvent) {
            og_fake_input_push(cfg_action, cfg_x, cfg_y);
            const AInputEvent *ev = og_fake_input_next();
            if (ev) app->onInputEvent(app, (AInputEvent *)(intptr_t)ev);
        }
    }
    if (frames >= cfg_frames_before + cfg_frames_after) {
        app->destroyRequested = 1;
        finished = 1;
        return;
    }
    frames++;
}

static void fake_process(struct android_app *app, struct android_poll_source *src) {
    (void)src;
    fake_step(app);
}

static struct android_poll_source source = { fake_process };

int ALooper_pollOnce(int timeout, int *outFd, int *events, void **outData) {
    (void)timeout; (void)outFd; (void)events;
    if (finished) return -1;      /* событий больше нет — крутим кадры */
    /* между событиями сообщаем «событий нет», чтобы хост успел нарисовать
     * кадр: так же работает настоящий looper на Android */
    if (polls++ & 1) return -1;
    if (outData) *outData = &source;
    return 1;
}

/* ---- EGL ---- */
static int egl_surface, egl_context;
static int swap_calls;

EGLDisplay eglGetDisplay(void *dpy) { (void)dpy; return (EGLDisplay)(intptr_t)1; }
int eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    (void)dpy;
    if (major) *major = 1;
    if (minor) *minor = 4;
    return 1;
}
int eglChooseConfig(EGLDisplay dpy, const EGLint *attribs, EGLConfig *cfg,
                    int size, EGLint *num) {
    (void)dpy; (void)attribs; (void)size;
    if (num) *num = 1;
    if (cfg) *cfg = (EGLConfig)(intptr_t)2;
    return 1;
}
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg,
                                  EGLNativeWindowType win, const EGLint *attrs) {
    (void)dpy; (void)cfg; (void)win; (void)attrs;
    return (EGLSurface)(intptr_t)++egl_surface;
}
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share,
                            const EGLint *attrs) {
    (void)dpy; (void)cfg; (void)share; (void)attrs;
    return (EGLContext)(intptr_t)++egl_context;
}
int eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                   EGLContext ctx) {
    (void)dpy; (void)draw; (void)read; (void)ctx;
    return 1;
}
int eglDestroyContext(EGLDisplay dpy, EGLContext ctx) { (void)dpy; (void)ctx; return 1; }
int eglDestroySurface(EGLDisplay dpy, EGLSurface surf) { (void)dpy; (void)surf; return 1; }
int eglTerminate(EGLDisplay dpy) { (void)dpy; return 1; }
int eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    (void)dpy; (void)surf;
    swap_calls++;
    return 1;
}
int og_fake_swap_calls(void) { return swap_calls; }

/* ---- GLES2 ---- */
static GLuint next_object = 1;

GLuint glCreateShader(GLenum type) { (void)type; return next_object++; }
void glShaderSource(GLuint s, int n, const char *const *src, const int *len) {
    (void)s; (void)n; (void)src; (void)len;
}
void glCompileShader(GLuint s) { (void)s; }
void glGetShaderiv(GLuint s, GLenum pname, GLint *out) {
    (void)s; (void)pname;
    if (out) *out = 1;
}
void glGetShaderInfoLog(GLuint s, GLsizei n, GLsizei *len, char *log) {
    (void)s;
    if (log && n > 0) log[0] = 0;
    if (len) *len = 0;
}
GLuint glCreateProgram(void) { return next_object++; }
void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
void glLinkProgram(GLuint p) { (void)p; }
void glGenTextures(int n, GLuint *out) {
    if (out) *out = next_object++;
    (void)n;
}
void glBindTexture(GLenum t, GLuint tex) { (void)t; (void)tex; }
void glTexParameteri(GLenum t, GLenum pname, GLint param) {
    (void)t; (void)pname; (void)param;
}
void glTexImage2D(GLenum t, GLint level, GLint fmt, int w, int h, int b,
                  GLenum fmt2, GLenum type, const void *px) {
    (void)t; (void)level; (void)fmt; (void)w; (void)h; (void)b;
    (void)fmt2; (void)type; (void)px;
}
void glTexSubImage2D(GLenum t, GLint level, int x, int y, int w, int h,
                     GLenum fmt, GLenum type, const void *px) {
    (void)t; (void)level; (void)x; (void)y; (void)w; (void)h;
    (void)fmt; (void)type; (void)px;
}
void glUseProgram(GLuint p) { (void)p; }
GLint glGetUniformLocation(GLuint p, const char *name) { (void)p; (void)name; return 0; }
GLint glGetAttribLocation(GLuint p, const char *name) { (void)p; (void)name; return 0; }
void glUniform1i(GLint loc, GLint v) { (void)loc; (void)v; }
void glActiveTexture(GLenum t) { (void)t; }
void glVertexAttribPointer(GLuint i, int size, GLenum type, GLboolean n,
                           int stride, const void *ptr) {
    (void)i; (void)size; (void)type; (void)n; (void)stride; (void)ptr;
}
void glEnableVertexAttribArray(GLuint i) { (void)i; }
void glDrawArrays(GLenum mode, GLint first, GLint count) {
    (void)mode; (void)first; (void)count;
}
void glViewport(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
