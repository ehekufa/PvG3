/* android_main.c — Android platform layer.
 *
 * native_app_glue + EGL + OpenGL ES 2.0. The game renders into a software
 * framebuffer (see game.c); each frame we upload it to a texture and draw a
 * full-screen quad. Touch events are mapped to the game's virtual resolution.
 */
#include "game.h"
#include "android_music.h"

#include <jni.h>
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
#include <limits.h>

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
    int ready, resumed, focused;
} Engine;

static Engine *G;
static void campaign_save(struct android_app *app);
static void garden_save(struct android_app *app);

/* No playback from the background, even if the screen/game is still alive. */
static void update_music(void) {
    android_music_set_playing(G->ready && G->resumed && G->focused);
}

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
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) engine_init(G);
        update_music();
        break;
    case APP_CMD_RESUME:
        G->resumed = 1;
        update_music();
        break;
    case APP_CMD_GAINED_FOCUS:
        G->focused = 1;
        update_music();
        break;
    case APP_CMD_LOST_FOCUS:
        G->focused = 0;
        update_music();
        campaign_save(app);
        break;
    case APP_CMD_PAUSE:
    case APP_CMD_STOP:
        G->resumed = 0;
        update_music();
        campaign_save(app);
        garden_save(app);
        break;
    case APP_CMD_SAVE_STATE:
        campaign_save(app);
        garden_save(app);
        break;
    case APP_CMD_TERM_WINDOW:
        campaign_save(app);
        G->ready = 0;
        update_music();
        engine_term(G);
        break;
    default: break;
    }
}

/* Garden plants survive a normal app restart. Kirill's five plant IDs retain
 * their stable byte values; invalid/truncated files are ignored. */
static int garden_path(struct android_app *app, char path[PATH_MAX]) {
    const char *dir = app->activity ? app->activity->internalDataPath : NULL;
    if (!dir) return 0;
    int n = snprintf(path, PATH_MAX, "%s/pvg3-garden.v1", dir);
    return n > 0 && n < PATH_MAX;
}

static void garden_load(struct android_app *app) {
    char path[PATH_MAX];
    if (!garden_path(app, path)) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    unsigned char bytes[4 + GAME_GARDEN_CELLS];
    size_t count = fread(bytes, 1, sizeof bytes, f);
    int extra = fgetc(f);
    fclose(f);
    if (count == sizeof bytes && extra == EOF && memcmp(bytes, "PVG1", 4) == 0)
        game_garden_import(bytes + 4);
}

static void garden_save(struct android_app *app) {
    char path[PATH_MAX], tmp[PATH_MAX];
    if (!garden_path(app, path)) return;
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n <= 0 || n >= (int)sizeof tmp) return;
    unsigned char bytes[4 + GAME_GARDEN_CELLS] = { 'P', 'V', 'G', '1' };
    game_garden_export(bytes + 4);
    FILE *f = fopen(tmp, "wb");
    if (!f) { LOGE("cannot open garden save file"); return; }
    size_t count = fwrite(bytes, 1, sizeof bytes, f);
    int closed = fclose(f);
    if (count != sizeof bytes || closed != 0 || rename(tmp, path) != 0) {
        LOGE("cannot save Zen Garden");
        remove(tmp);
    }
}

/* Campaign saves are separate from the existing garden format. Write to a
 * temporary file and rename on success so app termination during a write
 * cannot replace the previous good snapshot with a partial one. The game
 * layer checks the version, exact size, checksum and board values. */
static int campaign_path(struct android_app *app, char path[PATH_MAX]) {
    const char *dir = app->activity ? app->activity->internalDataPath : NULL;
    if (!dir) return 0;
    int n = snprintf(path, PATH_MAX, "%s/pvg3-campaign.v1", dir);
    return n > 0 && n < PATH_MAX;
}

static void campaign_load(struct android_app *app) {
    char path[PATH_MAX];
    if (!campaign_path(app, path)) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t len = game_save_size();
    if (len > 65536) { fclose(f); return; }
    uint8_t *bytes = (uint8_t *)malloc(len);
    if (!bytes) { fclose(f); return; }
    size_t count = fread(bytes, 1, len, f);
    int extra = fgetc(f);
    fclose(f);
    /* Old V1-V4 saves are shorter than V5/V6. Pass the actual file length to
     * game_save_import, which validates size, version and checksum together. */
    if (extra != EOF || !game_save_import(bytes, count))
        LOGE("campaign save rejected (incomplete or incompatible)");
    free(bytes);
}

static void campaign_save(struct android_app *app) {
    char path[PATH_MAX], tmp[PATH_MAX];
    if (!campaign_path(app, path)) return;
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n <= 0 || n >= (int)sizeof tmp) return;
    size_t len = game_save_size();
    if (len > 65536) return;
    uint8_t *bytes = (uint8_t *)malloc(len);
    if (!bytes) return;
    if (!game_save_export(bytes, len)) { free(bytes); return; }
    FILE *f = fopen(tmp, "wb");
    if (!f) { LOGE("cannot open campaign save file"); free(bytes); return; }
    size_t count = fwrite(bytes, 1, len, f);
    int closed = fclose(f);
    if (count != len || closed != 0 || rename(tmp, path) != 0) {
        LOGE("cannot save campaign");
        remove(tmp);
    }
    free(bytes);
}

/* Open the published HTML5 multiplayer version. It uses the same author's
 * PNGs and Firebase database, but lives in a browser so no API keys or extra
 * Java bytecode need to be stored in this native-only APK. */
static void open_online_page(struct android_app *app) {
    if (!app->activity || !app->activity->vm || !app->activity->clazz) return;
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
        attached = 1;
    }
    jclass uri_class = NULL, intent_class = NULL, activity_class = NULL;
    jstring url = NULL, action = NULL;
    jobject uri = NULL, intent = NULL;
    uri_class = (*env)->FindClass(env, "android/net/Uri");
    intent_class = (*env)->FindClass(env, "android/content/Intent");
    activity_class = (*env)->FindClass(env, "android/app/Activity");
    if (!uri_class || !intent_class || !activity_class) goto done;
    jmethodID parse = (*env)->GetStaticMethodID(env, uri_class, "parse",
                                "(Ljava/lang/String;)Landroid/net/Uri;");
    jmethodID ctor = (*env)->GetMethodID(env, intent_class, "<init>",
                                "(Ljava/lang/String;Landroid/net/Uri;)V");
    jmethodID start = (*env)->GetMethodID(env, activity_class, "startActivity",
                                "(Landroid/content/Intent;)V");
    if (!parse || !ctor || !start) goto done;
    /* Pages needs a one-time repository-admin setting. Until it is enabled,
     * launch a tested, commit-pinned version through an HTML-capable proxy;
     * branch URLs can serve stale JS from cache for hours. The proxy asks
     * for a one-time confirmation before showing the game. */
    url = (*env)->NewStringUTF(env,
        "https://rawcdn.githack.com/ehekufa/PvG3/2109ba31bd7a9d96f47949f2de59bcc42a53031b/online/index.html");
    action = (*env)->NewStringUTF(env, "android.intent.action.VIEW");
    if (!url || !action) goto done;
    uri = (*env)->CallStaticObjectMethod(env, uri_class, parse, url);
    if (!uri || (*env)->ExceptionCheck(env)) goto done;
    intent = (*env)->NewObject(env, intent_class, ctor, action, uri);
    if (!intent || (*env)->ExceptionCheck(env)) goto done;
    (*env)->CallVoidMethod(env, app->activity->clazz, start, intent);
done:
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        LOGE("could not launch browser for ONLINE");
    }
    if (intent) (*env)->DeleteLocalRef(env, intent);
    if (uri) (*env)->DeleteLocalRef(env, uri);
    if (action) (*env)->DeleteLocalRef(env, action);
    if (url) (*env)->DeleteLocalRef(env, url);
    if (activity_class) (*env)->DeleteLocalRef(env, activity_class);
    if (intent_class) (*env)->DeleteLocalRef(env, intent_class);
    if (uri_class) (*env)->DeleteLocalRef(env, uri_class);
    if (attached) (*vm)->DetachCurrentThread(vm);
}

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    if (!G->ready || !G->resumed || !G->focused) return 0;
    int action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(ev, 0);
    float y = AMotionEvent_getY(ev, 0);
    int vx = (int)(x * GAME_W / G->w);
    int vy = (int)(y * GAME_H / G->h);
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        int was_garden = game_phase() == GAME_GARDEN;
        game_input_press(vx, vy);
        if (game_take_online_request()) open_online_page(app);
        if (was_garden) garden_save(app); /* also save when leaving the garden */
        campaign_save(app); /* cards, coins, selection, results and menu */
    } else if (action == AMOTION_EVENT_ACTION_UP) game_input_release(vx, vy);
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
    garden_load(app);   /* keep reading the existing pvg3-garden.v1 */
    campaign_load(app);
    if (!android_music_init(app->activity ? app->activity->assetManager : NULL))
        LOGE("music unavailable; the game continues without audio");

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double last = ts.tv_sec + ts.tv_nsec / 1e9;
    double last_save = last;

    static uint32_t fb[GAME_W * GAME_H];

    while (!app->destroyRequested) {
        int events;
        struct android_poll_source *src;
        /* Block while paused, unfocused or without a window. Waves must not
         * keep moving while the app is in the background. */
        while (ALooper_pollOnce(engine.ready && engine.resumed && engine.focused ?
                                0 : -1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested) break;

        if (!engine.ready || !engine.resumed || !engine.focused) continue;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        double t = ts.tv_sec + ts.tv_nsec / 1e9;
        float dt = (float)(t - last);
        last = t;
        if (dt < 0) dt = 0;
        if (dt > 0.05f) dt = 0.05f;

        game_tick(dt, fb);
        engine_draw(&engine, fb);
        if (t - last_save >= 1.0) {
            if (game_phase() == GAME_PLAY) campaign_save(app);
            last_save = t;
        }
    }

    campaign_save(app);
    garden_save(app);
    android_music_shutdown();
    engine_term(&engine);
}
