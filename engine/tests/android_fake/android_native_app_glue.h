#ifndef OG_FAKE_ANDROID_NATIVE_APP_GLUE_H
#define OG_FAKE_ANDROID_NATIVE_APP_GLUE_H

#include <stdint.h>

#include <android/input.h>

struct android_window;
typedef struct android_window ANativeWindow;

enum {
    APP_CMD_INPUT_CHANGED = 1,
    APP_CMD_INIT_WINDOW = 1,
    APP_CMD_RESUME = 2,
    APP_CMD_PAUSE = 3,
    APP_CMD_TERM_WINDOW = 6,
    APP_CMD_GAINED_FOCUS = 7,
    APP_CMD_LOST_FOCUS = 8
};

struct android_app;
struct android_poll_source;

struct android_app {
    void *activity;
    void *window;
    int destroyRequested;
    void (*onAppCmd)(struct android_app *app, int32_t cmd);
    int32_t (*onInputEvent)(struct android_app *app, AInputEvent *ev);
    void (*low_level_input)(struct android_app *app, int event);
};

struct android_poll_source {
    void (*process)(struct android_app *app, struct android_poll_source *src);
};

int ALooper_pollOnce(int timeout, int *outFd, int *events,
                    void **outData);

/* Тестовый сценарий: заголовок окна → N кадров → касание → M кадров →
 * запрос на закрытие. Значения задаёт тест. */
void og_fake_looper_configure(int w, int h, int frames_before_touch,
                              float touch_x, float touch_y, int touch_action,
                              int frames_after_touch);
int og_fake_looper_started(void);

void android_main(struct android_app *app);

#endif
