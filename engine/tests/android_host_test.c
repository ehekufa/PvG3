/* Тест Android-слоя движка на заглушках: окно, выбор игры из меню,
 * запуск игры из встроенных в библиотеку файлов, ввод. Работает без
 * устройства, без NDK и без APK — но это тот самый engine/android/
 * og_android.c, который попадает в библиотеку. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "android/input.h"
#include "android/native_window.h"
#include "android_native_app_glue.h"
#include "og_vfs.h"

int og_host_test_in_game(void);
int og_host_test_menu(void);
long og_host_test_frames(void);
const char *og_host_test_game(void);
int og_fake_swap_calls(void);
void og_fake_log_reset(void);
int og_fake_log_errors(void);

static int checks;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "ПРОВАЛ: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
        checks++; \
    } while (0)

static void run_host(int frames_before, float tx, float ty, int action,
                     int frames_after) {
    struct android_app app;
    memset(&app, 0, sizeof app);
    og_fake_window_set_size(1280, 720);
    og_fake_looper_configure(1280, 720, frames_before, tx, ty, action, frames_after);
    android_main(&app);
}

int main(void) {
    /* Проекты встроены в библиотеку: уходим из репозитория, чтобы игра
     * не могла прочитать файлы с диска. */
    CHECK(chdir("/") == 0);
    og_fake_log_reset();

    /* 1. Без касания хост остаётся в меню и рисует его кадрами. */
    run_host(3, 0, 0, AMOTION_EVENT_ACTION_DOWN, 2);
    CHECK(og_host_test_menu() == 1);
    CHECK(og_host_test_in_game() == 0);
    CHECK(og_host_test_frames() >= 2);
    CHECK(og_fake_swap_calls() >= 2);
    CHECK(og_vfs_count() >= 10);   /* файлы игр внутри бинарника */

    /* 2. Касание первой карточки запускает «Оборону грядки»:
     *    окно 1280x720, меню 800x600 → (400, 250) меню = (640, 300) окна. */
    og_fake_log_reset();
    run_host(2, 640.0f, 300.0f, AMOTION_EVENT_ACTION_DOWN, 6);
    CHECK(og_host_test_in_game() == 1);
    CHECK(og_host_test_menu() == 0);
    CHECK(strcmp(og_host_test_game(), "projects/oborona") == 0);
    CHECK(og_host_test_frames() >= 3);
    CHECK(og_fake_log_errors() == 0);

    /* 3. Касание второй карточки запускает «Кирпичи»: окно 1280x720,
     *    вторая полоса меню (y 360..480) попадает в окно как 432..576. */
    og_fake_log_reset();
    run_host(2, 640.0f, 520.0f, AMOTION_EVENT_ACTION_DOWN, 4);
    CHECK(og_host_test_in_game() == 1);
    CHECK(strcmp(og_host_test_game(), "projects/kirpichi") == 0);
    CHECK(og_fake_log_errors() == 0);

    /* 4. Касание мимо карточек ничего не запускает. */
    og_fake_log_reset();
    run_host(2, 60.0f, 580.0f, AMOTION_EVENT_ACTION_DOWN, 2);
    CHECK(og_host_test_in_game() == 0);
    CHECK(og_fake_log_errors() == 0);

    printf("android_host_test: все проверки пройдены (%d)\n", checks);
    return 0;
}
