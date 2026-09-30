/* PvG3's native LVGL interface (v9.3). The game simulation and Firebase REST
 * client remain in game.c / online_net.c. LVGL owns the visible navigation,
 * online rooms/search/lobby and combat HUD; the author's battlefield art is
 * still rendered beneath its transparent match layer. No Android WebView. */
#include "lvgl_ui.h"
#include "game.h"
#include "game_view.h"
#include "font.h"
#include "online_net.h"
#include "vendor/lvgl/lvgl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C(hex) lv_color_hex(0x##hex)
#define DARK C(284735)
#define INK C(304537)
#define PAPER C(FAF5E3)
#define WHITE C(FFFDF3)
#define SAGE C(D8E9BF)
#define GOLD C(F3BC6C)
#define MUTED C(667965)
#define BLUE C(C8E7E9)

enum {
    U_MENU_PLAY = 1, U_MENU_LEVELS, U_MENU_GARDEN, U_MENU_BOOK, U_MENU_ONLINE,
    U_MENU_BACK, U_INTRO, U_MAP_LAWN, U_MAP_WATER, U_REFRESH,
    U_CREATE, U_MAKER, U_SEARCH, U_SEARCH_CLOSE, U_SEARCH_ERASE, U_SEARCH_GO,
    U_ROOMS_BACK, U_LOBBY_EXIT, U_PLANTS, U_ZOMBIES, U_MATCH_BOOK,
    U_MATCH_EXIT, U_MATCH_FINISH, U_MATCH_RETURN,
    U_GARDEN_BOOK, U_GARDEN_EXIT, U_GARDEN_ERASE,
    U_BOOK_BACK, U_BOOK_PLANTS, U_BOOK_ENEMIES,
    U_INTRO_NEXT, U_INTRO_SKIP, U_OFFLINE_BOOK, U_OFFLINE_MENU,
    U_RESULT_NEXT, U_RESULT_RETRY, U_RESULT_MENU,
    U_LEVEL_BASE = 100, U_ROOM_BASE = 200, U_KEY_BASE = 300,
    U_BOOK_ENTRY_BASE = 700
};

static lv_display_t *display;
static lv_indev_t *pointer;
static lv_obj_t *screen;
static lv_font_t *fonts[5];
static const int font_sizes[5] = {19, 24, 30, 40, 54};
static lv_image_dsc_t pictures[PV_ART_COUNT];
static uint32_t *layer;
static uint8_t *drawbuf;
static int active_phase = -1, page = 0, chosen_map = 1, search_open;
static LvglUIMakerOpenCallback maker_open_callback;
static int pointer_down, captured, touch_x, touch_y, dirty;
/* A packet is dragged over the author's board. LVGL draws the packet and its
 * ghost; game.c still validates the drop, so invalid water/occupied cells and
 * unaffordable moves never spend coins or send network commands. */
static int drag_index = -1, drag_phase, drag_x, drag_y;
static lv_obj_t *drag_ghost, *drag_target;
static char drag_notice[70];
static float drag_notice_left;
static uint32_t view_sig;
static char search_code[ON_ROOM_ID_SIZE], local_notice[110];
static char visible_ids[8][ON_ROOM_ID_SIZE];

static const lv_font_t *f(int index) { return fonts[index] ? fonts[index] : LV_FONT_DEFAULT; }

void lvgl_ui_set_maker_open_callback(LvglUIMakerOpenCallback callback) {
    maker_open_callback = callback;
}

static void flush_pixels(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_ARGB8888);
    for (int row = 0; row < h; ++row)
        memcpy(layer + (area->y1 + row) * GAME_W + area->x1,
               pixels + row * stride, (size_t)w * sizeof(uint32_t));
    lv_display_flush_ready(d);
}

static void read_pointer(lv_indev_t *in, lv_indev_data_t *data) {
    (void)in;
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                     lv_color_t color, int shadow) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    if (shadow) {
        lv_obj_set_style_shadow_width(o, 12, 0);
        lv_obj_set_style_shadow_ofs_y(o, 4, 0);
        lv_obj_set_style_shadow_color(o, C(A9B296), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
    }
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, int h,
                       const char *text, int font_index, lv_color_t color,
                       lv_text_align_t align) {
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_text_font(o, f(font_index), 0);
    lv_obj_set_style_text_color(o, color, 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
    lv_label_set_text(o, text);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void pressed(lv_event_t *ev);

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h,
                        const char *name, int size, lv_color_t face,
                        lv_color_t text_color, int action) {
    lv_obj_t *o = lv_button_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, face, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 15, 0);
    lv_obj_set_style_shadow_width(o, 8, 0);
    lv_obj_set_style_shadow_ofs_y(o, 4, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(o, C(C9A172), LV_STATE_PRESSED);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (name && name[0])
        label(o, 7, (h - font_sizes[size] - 3) / 2, w - 14, font_sizes[size] + 6,
              name, size, text_color, LV_TEXT_ALIGN_CENTER);
    if (action) lv_obj_add_event_cb(o, pressed, LV_EVENT_CLICKED,
                                     (void *)(intptr_t)action);
    return o;
}

static void art(lv_obj_t *parent, int id, int cx, int cy, int scaled) {
    if (id < 0 || id >= PV_ART_COUNT || !pictures[id].data) return;
    lv_obj_t *o = lv_image_create(parent);
    lv_image_set_src(o, &pictures[id]);
    lv_image_set_scale(o, (uint32_t)(256 * scaled / pictures[id].header.w));
    lv_obj_set_pos(o, cx - pictures[id].header.w / 2,
                   cy - pictures[id].header.h / 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/* Each protected duck is its own complete user drawing, shared by packets,
 * the picture book, drag previews and battlefield sprites. */
static void duck_art(lv_obj_t *parent, int cx, int cy, int size, int variant) {
    int image_id = variant == ON_CONE ? PV_ART_DUCK_CONE :
                   variant == ON_BUCKET ? PV_ART_DUCK_BUCKET : PV_ART_DUCK;
    art(parent, image_id, cx, cy, size);
}

static void header(lv_obj_t *root, const char *title, const char *back,
                   int action) {
    /* One large heading instead of three stacked lines of tiny explanations. */
    box(root, 0, 0, GAME_W, 148, 0, DARK, 0);
    box(root, 0, 145, GAME_W, 4, 0, GOLD, 0);
    label(root, 70, 46, back ? 940 : 1120, 71, title, 4, WHITE,
          LV_TEXT_ALIGN_LEFT);
    if (back) button(root, 1065, 48, 175, 62, back, 2, C(476750),
                     WHITE, action);
}

static void menu_screen(lv_obj_t *root) {
    header(root, "Растения против гусей 3", NULL, 0);
    button(root, 730, 51, 220, 64, "Уровни", 2, C(476750), WHITE, U_MENU_LEVELS);
    button(root, 971, 51, 270, 64, "Сад Дзен", 2, C(476750), WHITE, U_MENU_GARDEN);
    /* The characters stand together on the lawn, not in three square frames. */
    box(root, 55, 176, 1170, 355, 34, C(E5EFD9), 0);
    art(root, PV_ART_BREAD, 250, 322, 228);
    art(root, PV_ART_DIMA, 640, 324, 230);
    art(root, PV_ART_KIRILL, 1029, 322, 225);
    label(root, 93, 448, 310, 55, "Хлебушек", 2, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 482, 448, 310, 55, "Дима в маске", 2, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 871, 448, 310, 55, "Кирилл", 2, INK, LV_TEXT_ALIGN_CENTER);
    button(root, 90, 563, 280, 94, "Умная книга", 2, WHITE, INK, U_MENU_BOOK);
    button(root, 410, 548, 445, 125, "Начать игру", 3, GOLD, INK, U_MENU_PLAY);
    button(root, 895, 563, 295, 94, "Играть вдвоём", 2, SAGE, INK, U_MENU_ONLINE);
}

static void levels_screen(lv_obj_t *root) {
    header(root, "Выбери уровень", "Назад", U_MENU_BACK);
    for (int n = 1; n <= 10; n++) {
        int col = (n - 1) % 5, row = (n - 1) / 5;
        int x = 84 + col * 225, y = 194 + row * 188;
        lv_color_t tint = n == 5 ? BLUE : n <= game_completed_level() ?
                          SAGE : C(EEE4C9);
        lv_obj_t *o = button(root, x, y, 206, 153, "", 2, tint, INK,
                             U_LEVEL_BASE + n);
        char num[30];
        snprintf(num, sizeof num, "%02d", n);
        label(o, 17, 13, 170, 62, num, 4, INK, LV_TEXT_ALIGN_LEFT);
        if (n == 5 || n == 10)
            label(o, 18, 91, 180, 48, n == 5 ? "Вода" : "Финал",
                  2, INK, LV_TEXT_ALIGN_LEFT);
    }
    button(root, 414, 590, 452, 67, "Уровень 0 · история", 2,
           GOLD, INK, U_INTRO);
}

/* Offline navigation and HUD share the same real LVGL widgets as online. The
 * plants, map, coins and opponents beneath them are still the original game. */
static void garden_screen(lv_obj_t *root) {
    box(root, 0, 0, 1280, 121, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    label(root, 18, 29, 236, 53, "Сад Дзен", 3, WHITE, LV_TEXT_ALIGN_LEFT);
    for (int i = 0; i < 5; i++) {
        GameBookEntry entry;
        if (!game_book_entry(0, i, &entry)) continue;
        int x = 260 + i * 130;
        lv_obj_t *slot = button(root, x, 15, 120, 100, "", 0,
                                DARK, WHITE, 0);
        lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(slot, 0, 0);
        if (drag_phase == GAME_GARDEN && drag_index == i)
            box(slot, 17, 6, 86, 86, 43, GOLD, 0);
        art(slot, entry.art_id, 60, 49, 85);
    }
    button(root, 918, 22, 145, 44, "Книга", 2, C(476750), WHITE, U_GARDEN_BOOK);
    button(root, 1079, 22, 179, 44, "В меню", 2, C(476750), WHITE, U_GARDEN_EXIT);
    button(root, 918, 77, 340, 39, "Убрать", 2, SAGE, INK, U_GARDEN_ERASE);
    label(root, 24, 83, 225, 37, "Тяни в сад", 2, GOLD, LV_TEXT_ALIGN_LEFT);
}

static void book_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    header(root, "Умная книга", "Назад", U_BOOK_BACK);
    box(root, 63, 179, 513, 505, 22, PAPER, 1);
    box(root, 596, 179, 633, 505, 22, WHITE, 1);
    button(root, 84, 194, 220, 54, "Растения", 2,
           state.book_enemy_tab ? WHITE : SAGE, INK, U_BOOK_PLANTS);
    button(root, 314, 194, 226, 54, "Противники", 2,
           state.book_enemy_tab ? SAGE : WHITE, INK, U_BOOK_ENEMIES);
    int count = state.book_enemy_tab ? 4 : 5;
    for (int i = 0; i < count; ++i) {
        GameBookEntry entry;
        if (!game_book_entry(state.book_enemy_tab, i, &entry)) continue;
        int y = 259 + i * 79;
        lv_obj_t *card = button(root, 83, y, 472, 68, "", 0,
                                state.book_selection == i ? GOLD : WHITE,
                                INK, U_BOOK_ENTRY_BASE + i);
        if (state.book_enemy_tab && entry.art_id == PV_ART_DUCK)
            duck_art(card, 42, 34, 50, entry.enemy_variant);
        else art(card, entry.art_id, 42, 34, 54);
        label(card, 81, 16, 371, 44, entry.short_name, 2,
              INK, LV_TEXT_ALIGN_LEFT);
    }
    GameBookEntry entry;
    if (!game_book_entry(state.book_enemy_tab, state.book_selection, &entry)) return;
    if (state.book_enemy_tab && entry.art_id == PV_ART_DUCK)
        duck_art(root, 913, 303, 180, entry.enemy_variant);
    else art(root, entry.art_id, 913, 295, entry.art_id == PV_ART_ROBOT ? 210 : 195);
    label(root, 617, 394, 592, 64, entry.name, 3, INK,
          LV_TEXT_ALIGN_CENTER);
    /* A picture book, not a tiny stats table: prices, timers and HP live in
     * the combat rules, while these two large lines explain the characters. */
    box(root, 616, 477, 594, 186, 23, C(F7E7CD), 0);
    label(root, 642, 494, 542, 77, entry.description, 2, INK,
          LV_TEXT_ALIGN_CENTER);
    label(root, 642, 575, 542, 78, entry.detail, 2, INK,
          LV_TEXT_ALIGN_CENTER);
}

static void intro_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    lv_obj_t *advance = button(root, 0, 121, 1280, 599, "", 0,
                               DARK, WHITE, U_INTRO_NEXT);
    lv_obj_set_style_bg_opa(advance, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(advance, 0, 0);
    box(root, 0, 0, 1280, 121, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    label(root, 69, 40, 872, 57, "Уровень 0 · история Кирилла", 3,
          WHITE, LV_TEXT_ALIGN_LEFT);
    button(root, 1018, 43, 238, 66, "Пропустить", 2,
           C(476750), WHITE, U_INTRO_SKIP);
    box(root, 93, 533, 1094, 149, 22, PAPER, 1);
    const char *speaker = state.intro_step == 0 ? "ХЛЕБУШЕК" :
                          state.intro_step == 1 ? "ДИМА" : "КИРИЛЛ";
    const char *line = state.intro_step == 0 ? "Хлебушек плачет..." :
                       state.intro_step == 1 ? "Не плачь, мы нового сделаем." :
                       "Может, кто-то помогать мне будет?";
    label(root, 136, 547, 918, 40, speaker, 2, MUTED, LV_TEXT_ALIGN_LEFT);
    label(root, 136, 590, 982, 61, line, 3, INK, LV_TEXT_ALIGN_LEFT);

}

/* Big, unframed pictures rather than price/time cards. Keep the picture
 * visible throughout cooldown; only its caption changes to "Подождите". */
static void packet(lv_obj_t *root, int y, int image_id, int duck_variant,
                   int waiting, int dragging) {
    lv_obj_t *slot = button(root, 12, y, 223, 100, "", 0, DARK, WHITE, 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(slot, 0, 0);
    if (dragging) box(slot, 10, 5, 94, 94, 47, GOLD, 0);
    if (duck_variant >= 0) duck_art(slot, 57, 51, 94, duck_variant);
    else art(slot, image_id, 57, 51, 94);
    if (waiting)
        label(slot, 95, 31, 128, 48, "Подождите", 1, GOLD,
              LV_TEXT_ALIGN_LEFT);
}

static void play_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    box(root, 0, 0, 1280, 121, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    lv_obj_t *rail = box(root, 0, 121, 246, 599, 0, C(24382E), 0);
    lv_obj_set_style_bg_opa(rail, 115, 0); /* author's wood stays visible */
    art(root, PV_ART_COIN, 40, 82, 40);
    char coins[25];snprintf(coins, sizeof coins, "%d", state.coins);
    label(root, 72, 53, 157, 58, coins, 3, WHITE, LV_TEXT_ALIGN_LEFT);
    char title[100];
    if (state.boss_health_percent > 0)
        snprintf(title, sizeof title, "Уровень %d  ·  Робот: %d%%",
                 state.level, state.boss_health_percent);
    else snprintf(title, sizeof title, "Уровень %d  ·  Осталось: %d",
                  state.level, state.wave_remaining);
    label(root, 270, 32, 639, 45, title, 2, WHITE, LV_TEXT_ALIGN_LEFT);
    label(root, 271, 75, 652, 42,
          drag_notice_left > 0 ? drag_notice : state.level == 5 ?
          "Тяни кувшинку на воду" : "Тяни растение на клетку",
          2, drag_notice_left > 0 ? GOLD : C(C4D9BB), LV_TEXT_ALIGN_LEFT);
    button(root, 929, 43, 150, 64, "Книга", 2, C(476750), WHITE, U_OFFLINE_BOOK);
    button(root, 1094, 43, 165, 64, "В меню", 2, C(476750), WHITE, U_OFFLINE_MENU);
    for (int i = 0; i < 5; ++i) {
        if (i == 4 && state.level != 5) continue; /* no unused lily on dry levels */
        GameBookEntry entry;
        if (!game_book_entry(0, i, &entry)) continue;
        packet(root, 137 + i * 107, entry.art_id, -1,
               state.cooldown[i] > 0, drag_phase == GAME_PLAY && drag_index == i);
    }
}

static void result_screen(lv_obj_t *root, int phase) {
    lv_obj_t *veil = box(root, 0, 0, 1280, 720, 0, C(14251E), 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_80, 0);
    box(root, 307, 157, 666, 487, 27, PAPER, 1);
    const char *title = phase == GAME_LEVEL_CLEAR ? "Уровень пройден!" :
                        phase == GAME_WIN ? "Робот остановлен!" :
                        "Защита прорвана";
    label(root, 347, 222, 586, 81, title, 4, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 355, 317, 570, 91,
          phase == GAME_LEVEL_CLEAR ? "Волна побеждена. Готов к следующей?" :
          phase == GAME_WIN ? "Кирилл и гуси спасены!" :
          "Попробуй снова — сохранение не пропало.",
          2, MUTED, LV_TEXT_ALIGN_CENTER);
    if (phase == GAME_LEVEL_CLEAR) {
        button(root, 392, 461, 496, 98, "Следующий уровень", 2,
               GOLD, INK, U_RESULT_NEXT);
        button(root, 480, 581, 320, 63, "В меню", 1,
               WHITE, INK, U_RESULT_MENU);
    } else if (phase == GAME_WIN)
        button(root, 425, 464, 430, 100, "В меню", 2, GOLD, INK, U_RESULT_MENU);
    else {
        button(root, 335, 460, 310, 104, "Повторить", 2,
               GOLD, INK, U_RESULT_RETRY);
        button(root, 660, 460, 315, 104, "В меню", 2,
               WHITE, INK, U_RESULT_MENU);
    }
}

/* Count/status is recomputed on each Firebase snapshot, never hard-coded. */
static void rooms_screen(lv_obj_t *root, const OnNetView *v) {
    header(root, "Играем вместе", "В меню", U_ROOMS_BACK);
    box(root, 43, 162, 1195, 89, 19, PAPER, 1);
    lv_obj_t *lawn = button(root, 64, 191, 207, 54, "Газон", 2,
                             chosen_map == 1 ? SAGE : WHITE, INK, U_MAP_LAWN);
    lv_obj_t *water = button(root, 282, 191, 210, 54, "Вода", 2,
                              chosen_map == 5 ? BLUE : WHITE, INK, U_MAP_WATER);
    lv_obj_set_style_border_width(chosen_map == 1 ? lawn : water, 2, 0);
    lv_obj_set_style_border_color(chosen_map == 1 ? lawn : water,
                                  C(779C73), 0);
    button(root, 650, 179, 155, 62, "Поиск", 2, WHITE, INK, U_SEARCH);
    button(root, 813, 179, 187, 62, "Мастерская", 2, SAGE, INK, U_MAKER);
    button(root, 1008, 179, 208, 62, "+ Создать", 2, GOLD, INK, U_CREATE);
    box(root, 43, 271, 1195, 421, 23, PAPER, 1);
    label(root, 70, 286, 660, 52, "Свободные комнаты", 3, INK,
          LV_TEXT_ALIGN_LEFT);
    button(root, 1027, 284, 193, 48, "Обновить", 2, WHITE, INK, U_REFRESH);
    box(root, 69, 341, 1142, 2, 0, C(D9DDC5), 0);
    if (v->room_count == 0) {
        art(root, PV_ART_PEA, 544, 417, 118);
        art(root, PV_ART_DUCK, 740, 418, 116);
        const char *title = v->notice[0] ? "Нет связи" :
                            !v->connected ? "Ищем комнаты..." :
                            "Здесь пока тихо";
        label(root, 285, 497, 710, 52, title, 3, INK, LV_TEXT_ALIGN_CENTER);
        if (v->notice[0])
            label(root, 253, 548, 774, 38, v->notice, 1,
                  C(A1513B), LV_TEXT_ALIGN_CENTER);
        button(root, 479, 587, 322, 66,
               v->notice[0] ? "Повторить поиск" : "+ Создать комнату", 2,
               GOLD, INK, v->notice[0] ? U_REFRESH : U_CREATE);
    } else {
        if (page * 8 >= v->room_count) page = 0;
        memset(visible_ids, 0, sizeof visible_ids);
        int two_columns = v->room_count > 4;
        for (int i = 0; i < 8 && page * 8 + i < v->room_count; i++) {
            int idx = page * 8 + i;
            int x = 70 + (two_columns ? (i % 2) * 582 : 0);
            int y = two_columns ? 349 + (i / 2) * 77 : 349 + i * 81;
            int width = two_columns ? 550 : 1138;
            snprintf(visible_ids[i], sizeof visible_ids[i], "%s", v->rooms[idx].id);
            lv_obj_t *card = button(root, x, y, width, 71, "", 1, WHITE, INK,
                                    U_ROOM_BASE + i);
            label(card, 29, 14, 217, 48, v->rooms[idx].id, 2, INK,
                  LV_TEXT_ALIGN_LEFT);
            label(card, width - 300, 19, 119, 42,
                  v->rooms[idx].map == 5 ? "Вода" : "Газон",
                  1, MUTED, LV_TEXT_ALIGN_CENTER);
            label(card, width - 174, 19, 158, 42, "Войти  >", 1,
                  C(477E4D), LV_TEXT_ALIGN_CENTER);
        }
        if (v->room_count > 8) {
            button(root, 77, 660, 168, 43, "Назад", 1, WHITE, INK, U_ROOM_BASE + 8);
            button(root, 1035, 660, 168, 43, "Дальше", 1, WHITE, INK,
                   U_ROOM_BASE + 9);
        }
    }
    if (!search_open) return;
    lv_obj_t *veil = box(root, 0, 0, 1280, 720, 0, C(14251E), 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_80, 0);
    box(root, 216, 72, 848, 611, 26, PAPER, 1);
    label(root, 259, 111, 590, 58, "Найти комнату по коду", 3, INK,
          LV_TEXT_ALIGN_LEFT);

    button(root, 895, 111, 126, 51, "Назад", 1, WHITE, INK, U_SEARCH_CLOSE);
    size_t len = strlen(search_code);
    for (int i = 0; i < 6; ++i) {
        int x = 287 + i * 88;
        box(root, x, 207, 75, 64, 12, WHITE, 0);
        char letter[2] = {i < (int)len ? search_code[i] : ' ', 0};
        label(root, x, 216, 75, 51, letter, 3, INK, LV_TEXT_ALIGN_CENTER);
        if (i >= (int)len) box(root, x + 27, 260, 22, 2, 0, C(B9C6AF), 0);
    }
    button(root, 844, 207, 148, 64, "Стереть", 1, WHITE, INK, U_SEARCH_ERASE);
    const char *keys = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 32; ++i) {
        char ch[2] = {keys[i], 0};
        int x = 286 + (i % 9) * 78, y = 286 + (i / 9) * 67;
        if (i >= 27) x += 156; /* centre the last, shorter row */
        button(root, x, y, 68, 55, ch, 2, WHITE, INK, U_KEY_BASE + i);
    }
    if (local_notice[0])
        label(root, 331, 558, 620, 33, local_notice, 1,
              C(A1513B), LV_TEXT_ALIGN_CENTER);
    button(root, 395, 599, 490, 64, "Войти в комнату", 2,
           len == 6 ? GOLD : WHITE, INK, U_SEARCH_GO);
}

static void lobby_screen(lv_obj_t *root, const OnNetView *v) {
    char title[66];
    snprintf(title, sizeof title, "Комната %s", v->room_id);
    header(root, title, "Выйти", U_LOBBY_EXIT);
    label(root, 127, 184, 430, 51,
          v->map == 5 ? "Вода" : "Газон", 2, INK,
          LV_TEXT_ALIGN_LEFT);
    label(root, 815, 184, 350, 47, v->guest_id[0] ?
          "Друг вошёл" : "Ждём друга", 2, MUTED,
          LV_TEXT_ALIGN_RIGHT);
    int mine = v->slot == ON_SLOT_HOST ? v->host_role : v->guest_role;
    int other = v->slot == ON_SLOT_HOST ? v->guest_role : v->host_role;
    for (int side = ON_ROLE_PLANTS; side <= ON_ROLE_ZOMBIES; ++side) {
        int x = side == ON_ROLE_PLANTS ? 90 : 663;
        int chosen = mine == side, taken = other == side;
        lv_obj_t *card = button(root, x, 268, 527, 317, "", 1,
                                WHITE, INK,
                                side == ON_ROLE_PLANTS ? U_PLANTS : U_ZOMBIES);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        art(card, side == ON_ROLE_PLANTS ? PV_ART_PEA : PV_ART_DUCK,
            264, 121, 210);
        label(card, 32, 224, 463, 61,
              side == ON_ROLE_PLANTS ? "Растения" : "Зомби", 3, INK,
              LV_TEXT_ALIGN_CENTER);
        box(card, 129, 285, 269, 6, 3, chosen ? GOLD : SAGE, 0);
        if (taken)
            label(card, 155, 288, 220, 29, "Сторона занята", 1,
                  MUTED, LV_TEXT_ALIGN_CENTER);
    }
    box(root, 90, 609, 1100, 78, 17, DARK, 0);
    const char *status = v->notice[0] ? v->notice : !v->guest_id[0] ?
                         "Ждём друга" : v->busy ? "Сохраняем выбор..." :
                         !mine ? "Выбери сторону" : !other ?
                         "Ждём выбор друга" : "Начинаем бой...";
    label(root, 138, 623, 1004, 49, status, 2,
          v->notice[0] ? GOLD : WHITE, LV_TEXT_ALIGN_CENTER);
}

static void match_screen(lv_obj_t *root, const OnNetView *v) {
    OnMatch state;
    int role = 0;
    char hint[110];float hint_left = 0;
    game_online_ui_snapshot(&state, &role, NULL, hint, sizeof hint, &hint_left);
    int plants = role == ON_ROLE_PLANTS;
    box(root, 0, 0, 1280, 120, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    lv_obj_t *rail = box(root, 0, 121, 246, 599, 0, C(24382E), 0);
    lv_obj_set_style_bg_opa(rail, 115, 0);
    art(root, PV_ART_COIN, 40, 82, 40);
    char cash[24];
    snprintf(cash, sizeof cash, "%d", plants ? state.plant_cash : state.zombie_cash);
    label(root, 72, 53, 157, 58, cash, 3, WHITE, LV_TEXT_ALIGN_LEFT);
    char title[86];
    snprintf(title, sizeof title, "Комната %s  ·  осталось: %d",
             v->room_id, state.left + state.duck_count);
    label(root, 272, 32, 634, 45, title, 2, WHITE, LV_TEXT_ALIGN_LEFT);
    const char *message = drag_notice_left > 0 ? drag_notice :
                          hint_left > 0 ? hint : v->notice[0] ? v->notice :
                          v->pending ? "Ждём ход..." : !v->guest_id[0] ?
                          "Друг вышел" : plants && state.map == 5 ?
                          "Тяни кувшинку на воду" : plants ?
                          "Тяни растение на клетку" : "Тяни утку на ряд";
    label(root, 272, 75, 655, 42, message, 2,
          drag_notice_left > 0 || hint_left > 0 || v->notice[0] ?
          GOLD : C(C4D9BB), LV_TEXT_ALIGN_LEFT);
    button(root, 930, 43, 150, 66, "Книга", 2, C(476750), WHITE, U_MATCH_BOOK);
    button(root, 1094, 43, 165, 66, "Выйти", 2, C(476750), WHITE, U_MATCH_EXIT);
    static const int arts[5] = {PV_ART_PEA, PV_ART_WALNUT,
                                PV_ART_SUNFLOWER, PV_ART_JUMPER, PV_ART_LILY};
    int n = plants ? (state.map == 5 ? 5 : 4) : 3;
    for (int i = 0; i < n; ++i) {
        int top = plants ? 125 + i * 104 : 151 + i * 113;
        int waiting = plants ? state.plant_cooldown[i] > 0 :
                               state.duck_cooldown[i] > 0;
        packet(root, top, plants ? arts[i] : PV_ART_DUCK,
               plants ? -1 : on_duck_type[i],
               waiting, drag_phase == GAME_ONLINE_MATCH && drag_index == i);
    }
    if (!plants)
        button(root, 15, 615, 217, 80, "Закончить", 2, GOLD, INK, U_MATCH_FINISH);
    if (state.winner) {
        lv_obj_t *shade = box(root, 0, 0, 1280, 720, 0, C(14251E), 0);
        lv_obj_set_style_bg_opa(shade, LV_OPA_80, 0);
        box(root, 309, 177, 662, 402, 26, PAPER, 1);
        art(root, state.winner == ON_WIN_PLANTS ? PV_ART_PEA : PV_ART_DUCK,
            638, 295, 126);
        label(root, 365, 369, 550, 70,
              state.winner == role ? "Победа!" : "Победил соперник",
              4, INK, LV_TEXT_ALIGN_CENTER);
        label(root, 367, 429, 546, 46,
              state.winner == ON_WIN_PLANTS ? "Растения спасли сад" :
              "Утки захватили сад", 2, MUTED, LV_TEXT_ALIGN_CENTER);
        button(root, 410, 493, 460, 65, "Вернуться к комнатам", 2,
               GOLD, INK, U_MATCH_RETURN);
    }
}

/* FNV-1a of visible, stable state. No rebuild during moving pieces in a match. */
static uint32_t mix(uint32_t h, const void *buf, size_t size) {
    const unsigned char *s = buf;
    while (size--) h = (h ^ *s++) * 16777619u;
    return h;
}
static uint32_t state_signature(int phase, const OnNetView *v) {
    uint32_t h = 2166136261u;
    h = mix(h, &phase, sizeof phase);
    if (phase == GAME_ONLINE_ROOMS) {
        h = mix(h, &v->room_count, sizeof v->room_count);
        h = mix(h, &v->rooms, (size_t)v->room_count * sizeof v->rooms[0]);
        h = mix(h, &v->connected, sizeof v->connected);
        h = mix(h, &v->busy, sizeof v->busy);
        h = mix(h, v->notice, strlen(v->notice));
    } else if (phase == GAME_ONLINE_LOBBY) {
        h = mix(h, v->room_id, strlen(v->room_id));
        h = mix(h, v->guest_id, strlen(v->guest_id));
        h = mix(h, &v->map, sizeof v->map);
        h = mix(h, &v->host_role, sizeof v->host_role);
        h = mix(h, &v->guest_role, sizeof v->guest_role);
        h = mix(h, &v->busy, sizeof v->busy);
        h = mix(h, v->notice, strlen(v->notice));
    } else if (phase == GAME_ONLINE_MATCH) {
        OnMatch s;
        int role, selected;float secs;
        char hint[110];
        game_online_ui_snapshot(&s, &role, &selected, hint, sizeof hint, &secs);
        h = mix(h, &role, sizeof role);
        h = mix(h, &selected, sizeof selected);
        h = mix(h, &s.plant_cash, sizeof s.plant_cash);
        h = mix(h, &s.zombie_cash, sizeof s.zombie_cash);
        h = mix(h, &s.map, sizeof s.map);
        h = mix(h, &s.left, sizeof s.left);
        h = mix(h, &s.duck_count, sizeof s.duck_count);
        h = mix(h, &s.winner, sizeof s.winner);
        for (int i = 0; i < 5; ++i) {
            int delay = (int)ceilf(s.plant_cooldown[i]);
            h = mix(h, &delay, sizeof delay);
        }
        for (int i = 0; i < 3; ++i) {
            int delay = (int)ceilf(s.duck_cooldown[i]);
            h = mix(h, &delay, sizeof delay);
        }
        h = mix(h, &v->pending, sizeof v->pending);
        h = mix(h, v->guest_id, strlen(v->guest_id));
        h = mix(h, v->notice, strlen(v->notice));
        if (secs > 0) h = mix(h, hint, strlen(hint));
    } else if (phase == GAME_GARDEN || phase == GAME_BOOK ||
               phase == GAME_INTRO || phase == GAME_PLAY) {
        GameOfflineUIState state;
        game_offline_ui_snapshot(&state);
        if (phase == GAME_GARDEN)
            h = mix(h, &state.garden_selection, sizeof state.garden_selection);
        else if (phase == GAME_BOOK) {
            h = mix(h, &state.book_enemy_tab, sizeof state.book_enemy_tab);
            h = mix(h, &state.book_selection, sizeof state.book_selection);
        } else if (phase == GAME_INTRO)
            h = mix(h, &state.intro_step, sizeof state.intro_step);
        else {
            h = mix(h, &state.level, sizeof state.level);
            h = mix(h, &state.coins, sizeof state.coins);
            h = mix(h, &state.selection, sizeof state.selection);
            h = mix(h, &state.wave_remaining, sizeof state.wave_remaining);
            h = mix(h, &state.boss_health_percent, sizeof state.boss_health_percent);
            for (int i = 0; i < 5; ++i) {
                int delay = (int)ceilf(state.cooldown[i]);
                h = mix(h, &delay, sizeof delay);
            }
        }
    } else if (phase == GAME_MENU || phase == GAME_SELECT) {
        int completed = game_completed_level(), resume = game_resume_level();
        h = mix(h, &completed, sizeof completed);
        h = mix(h, &resume, sizeof resume);
    }
    return h;
}

/* Android's MOVE events update a real LVGL image under the finger. Planting
 * happens only on release over the 9x5 board, never on tapping a packet. */
static int over_board(int x, int y) {
    return x >= ON_BOARD_X && x < ON_BOARD_X + ON_COLS * ON_CELL_W &&
           y >= ON_BOARD_Y && y < ON_BOARD_Y + ON_ROWS * ON_CELL_H;
}

static int packet_at(int phase, int x, int y) {
    if (phase == GAME_GARDEN && y >= 15 && y < 115) {
        for (int i = 0; i < 5; ++i)
            if (x >= 260 + i * 130 && x < 380 + i * 130) return i;
    }
    if (phase == GAME_PLAY && x >= 12 && x < 235) {
        for (int i = 0; i < 5; ++i) {
            int top = 137 + i * 107;
            if (y >= top && y < top + 100)
                return i == 4 && game_level() != 5 ? -1 : i;
        }
    }
    if (phase == GAME_ONLINE_MATCH && x >= 12 && x < 235) {
        OnMatch state;int role;
        game_online_ui_snapshot(&state, &role, NULL, NULL, 0, NULL);
        if (state.winner) return -1;
        int plants = role == ON_ROLE_PLANTS;
        for (int i = 0; i < (plants ? (state.map == 5 ? 5 : 4) : 3); ++i) {
            int top = plants ? 125 + i * 104 : 151 + i * 113;
            if (y >= top && y < top + 100) return i;
        }
    }
    return -1;
}

static void drag_warning(const char *message) {
    snprintf(drag_notice, sizeof drag_notice, "%s", message);
    drag_notice_left = 1.8f;
    dirty = 1;
}

static int packet_ready(int phase, int index) {
    if (phase == GAME_GARDEN) return 1;
    if (phase == GAME_PLAY) {
        GameOfflineUIState s;GameBookEntry entry;
        game_offline_ui_snapshot(&s);
        if (!game_book_entry(0, index, &entry)) return 0;
        if (s.cooldown[index] > 0) {drag_warning("Подождите");return 0;}
        if (s.coins < entry.cost) {drag_warning("Мало монет");return 0;}
        return 1;
    }
    if (phase == GAME_ONLINE_MATCH) {
        OnMatch s;int role;
        game_online_ui_snapshot(&s, &role, NULL, NULL, 0, NULL);
        int plants = role == ON_ROLE_PLANTS;
        if ((plants ? s.plant_cooldown[index] : s.duck_cooldown[index]) > 0) {
            drag_warning("Подождите");return 0;
        }
        if (!plants && s.left <= 0) {drag_warning("Утки закончились");return 0;}
        if ((plants ? s.plant_cash : s.zombie_cash) <
            (plants ? on_plant_cost[index] : on_duck_cost[index])) {
            drag_warning("Мало монет");return 0;
        }
        return 1;
    }
    return 0;
}

static void drag_position(int x, int y) {
    drag_x = x;drag_y = y;
    if (!drag_ghost) return;
    int gx = x - 55, gy = y - 94;
    if (over_board(x, y)) {
        int col = (x - ON_BOARD_X) / ON_CELL_W;
        int row = (y - ON_BOARD_Y) / ON_CELL_H;
        int zombie = 0;
        if (drag_phase == GAME_ONLINE_MATCH) {
            int role;
            game_online_ui_snapshot(NULL, &role, NULL, NULL, 0, NULL);
            zombie = role == ON_ROLE_ZOMBIES;
        }
        if (drag_target) {
            lv_obj_set_size(drag_target, zombie ? GAME_W - ON_BOARD_X : ON_CELL_W,
                            ON_CELL_H);
            lv_obj_set_pos(drag_target,
                           zombie ? ON_BOARD_X : ON_BOARD_X + col * ON_CELL_W,
                           ON_BOARD_Y + row * ON_CELL_H);
            lv_obj_remove_flag(drag_target, LV_OBJ_FLAG_HIDDEN);
        }
        gx = (zombie ? GAME_W - 90 : ON_BOARD_X + col * ON_CELL_W + ON_CELL_W / 2) - 55;
        gy = ON_BOARD_Y + row * ON_CELL_H + ON_CELL_H / 2 - 55;
    } else if (drag_target) lv_obj_add_flag(drag_target, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(drag_ghost, gx, gy);
}

static void drag_overlay(lv_obj_t *root, int phase) {
    drag_ghost = drag_target = NULL;
    if (drag_index < 0 || drag_phase != phase) return;
    /* A round, temporary drop halo rather than permanent checkerboard tiles. */
    drag_target = box(root, 0, 0, ON_CELL_W, ON_CELL_H, ON_CELL_W / 2, GOLD, 0);
    lv_obj_set_style_bg_opa(drag_target, 45, 0);
    lv_obj_set_style_border_width(drag_target, 4, 0);
    lv_obj_set_style_border_color(drag_target, GOLD, 0);
    drag_ghost = lv_obj_create(root);
    lv_obj_remove_style_all(drag_ghost);
    lv_obj_set_size(drag_ghost, 110, 110);
    lv_obj_set_style_bg_opa(drag_ghost, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(drag_ghost, LV_OPA_80, 0);
    lv_obj_remove_flag(drag_ghost, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (phase == GAME_ONLINE_MATCH) {
        int role;
        game_online_ui_snapshot(NULL, &role, NULL, NULL, 0, NULL);
        if (role == ON_ROLE_ZOMBIES) {
            duck_art(drag_ghost, 55, 55, 96, on_duck_type[drag_index]);
            drag_position(drag_x, drag_y);
            return;
        }
    }
    GameBookEntry entry;
    if (game_book_entry(0, drag_index, &entry))
        art(drag_ghost, entry.art_id, 55, 55, 95);
    drag_position(drag_x, drag_y);
}

static void drop_packet(int phase, int index, int x, int y) {
    if (!over_board(x, y) || phase != game_phase()) return;
    if (phase == GAME_GARDEN)
        game_input_press(320 + index * 130, 60);
    else if (phase == GAME_PLAY)
        game_input_press(125, 185 + index * 107);
    else if (phase == GAME_ONLINE_MATCH) {
        int role;
        game_online_ui_snapshot(NULL, &role, NULL, NULL, 0, NULL);
        game_input_press(120, role == ON_ROLE_ZOMBIES ?
                         201 + index * 113 : 175 + index * 104);
    }
    game_input_press(x, y);
    game_input_release(x, y);
}

static void pressed(lv_event_t *ev) {
    int code = (int)(intptr_t)lv_event_get_user_data(ev);
    if (code >= U_KEY_BASE && code < U_KEY_BASE + 32) {
        const char *keys = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        size_t n = strlen(search_code);
        if (n < 6) { search_code[n] = keys[code - U_KEY_BASE]; search_code[n + 1] = 0; }
        local_notice[0] = 0;dirty = 1;return;
    }
    if (code >= U_ROOM_BASE && code < U_ROOM_BASE + 8) {
        int n = code - U_ROOM_BASE;
        if (visible_ids[n][0]) on_net_join(visible_ids[n]);
        return;
    }
    if (code == U_ROOM_BASE + 8) {if (page > 0) page--;dirty = 1;return;}
    if (code == U_ROOM_BASE + 9) {page++;dirty = 1;return;}
    if (code >= U_BOOK_ENTRY_BASE && code < U_BOOK_ENTRY_BASE + 5) {
        game_input_press(300, 181 + (code - U_BOOK_ENTRY_BASE) * 99);
        dirty = 1;return;
    }
    if (code > U_LEVEL_BASE && code <= U_LEVEL_BASE + 10) {
        int n = code - U_LEVEL_BASE;
        game_input_press(190 + ((n - 1) % 5) * 220,
                         260 + ((n - 1) / 5) * 190);return;
    }
    switch (code) {
    case U_MENU_PLAY: game_input_press(640, 600);break;
    case U_MENU_LEVELS: game_input_press(635, 85);break;
    case U_MENU_GARDEN: game_input_press(1040, 83);break;
    case U_MENU_BOOK: game_input_press(205, 615);break;
    case U_MENU_ONLINE: game_input_press(1058, 615);page = 0;break;
    case U_MENU_BACK: game_input_press(1150, 55);break;
    case U_INTRO: game_input_press(640, 605);break;
    case U_MAP_LAWN: chosen_map = 1;dirty = 1;break;
    case U_MAP_WATER: chosen_map = 5;dirty = 1;break;
    case U_REFRESH: on_net_refresh();break;
    case U_CREATE: on_net_create(chosen_map);break;
    case U_MAKER: if (maker_open_callback) maker_open_callback();break;
    case U_SEARCH: search_open = 1;search_code[0] = local_notice[0] = 0;
                   dirty = 1;break;
    case U_SEARCH_CLOSE: search_open = 0;search_code[0] = 0;dirty = 1;break;
    case U_SEARCH_ERASE: {
        size_t n = strlen(search_code);
        if (n) search_code[n - 1] = 0;
        local_notice[0] = 0;dirty = 1;break;
    }
    case U_SEARCH_GO:
        if (strlen(search_code) == 6) {
            on_net_join(search_code);search_open = 0;
            search_code[0] = 0;dirty = 1;
        } else {
            snprintf(local_notice, sizeof local_notice,
                     "Введи все шесть символов кода.");dirty = 1;
        }
        break;
    case U_ROOMS_BACK: game_input_press(1140, 50);break;
    case U_LOBBY_EXIT: game_input_press(1150, 50);break;
    case U_PLANTS: on_net_choose(ON_ROLE_PLANTS);break;
    case U_ZOMBIES: on_net_choose(ON_ROLE_ZOMBIES);break;
    case U_MATCH_BOOK: game_input_press(985, 54);break;
    case U_MATCH_EXIT: game_input_press(1150, 54);break;
    case U_MATCH_FINISH: game_input_press(100, 640);break;
    case U_MATCH_RETURN: game_input_press(640, 510);break;
    case U_GARDEN_BOOK: game_input_press(988, 43);break;
    case U_GARDEN_EXIT: game_input_press(1160, 43);break;
    case U_GARDEN_ERASE: game_input_press(1100, 95);dirty = 1;break;
    case U_BOOK_BACK: game_input_press(1150, 55);break;
    case U_BOOK_PLANTS: game_input_press(250, 140);dirty = 1;break;
    case U_BOOK_ENEMIES: game_input_press(447, 140);dirty = 1;break;
    case U_INTRO_NEXT: game_input_press(640, 600);dirty = 1;break;
    case U_INTRO_SKIP: game_input_press(1130, 54);break;
    case U_OFFLINE_BOOK: game_input_press(988, 58);break;
    case U_OFFLINE_MENU: game_input_press(1160, 58);break;
    case U_RESULT_NEXT: game_input_press(640, 505);break;
    case U_RESULT_RETRY: game_input_press(490, 510);break;
    case U_RESULT_MENU:
        game_input_press(active_phase == GAME_LEVEL_CLEAR ? 640 :
                         active_phase == GAME_LOSE ? 810 : 640,
                         active_phase == GAME_LEVEL_CLEAR ? 610 : 510);
        break;
    default: break;
    }
}

static void rebuild(int phase, const OnNetView *net) {
    lv_obj_t *old = screen;
    screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, GAME_W, GAME_H);
    lv_obj_set_style_bg_color(screen, C(E8EAD8), 0);
    lv_obj_set_style_bg_opa(screen,
                            lvgl_ui_fullscreen(phase) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    if (phase == GAME_MENU) menu_screen(screen);
    else if (phase == GAME_SELECT) levels_screen(screen);
    else if (phase == GAME_GARDEN) garden_screen(screen);
    else if (phase == GAME_BOOK) book_screen(screen);
    else if (phase == GAME_INTRO) intro_screen(screen);
    else if (phase == GAME_PLAY) play_screen(screen);
    else if (phase == GAME_LEVEL_CLEAR || phase == GAME_WIN || phase == GAME_LOSE)
        result_screen(screen, phase);
    else if (phase == GAME_ONLINE_ROOMS) rooms_screen(screen, net);
    else if (phase == GAME_ONLINE_LOBBY) lobby_screen(screen, net);
    else if (phase == GAME_ONLINE_MATCH) match_screen(screen, net);
    drag_overlay(screen, phase);
    memset(layer, 0, (size_t)GAME_W * GAME_H * 4);
    lv_screen_load(screen);
    if (old) lv_obj_del(old);
    active_phase = phase;
    dirty = 0;
}

int lvgl_ui_init(void) {
    if (display) return 1;
    layer = calloc((size_t)GAME_W * GAME_H, sizeof *layer);
    drawbuf = malloc((size_t)GAME_W * 64 * 4);
    if (!layer || !drawbuf) { free(layer);free(drawbuf);layer = NULL;drawbuf = NULL;return 0; }
    lv_init();
    display = lv_display_create(GAME_W, GAME_H);
    if (!display) {lv_deinit();free(layer);free(drawbuf);layer = NULL;drawbuf = NULL;return 0;}
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, drawbuf, NULL, GAME_W * 64 * 4,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_pixels);
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_read_cb(pointer, read_pointer);
    /* Consume each Android DOWN/UP immediately. A very fast tap can have
     * both events in one looper iteration, before the next draw frame. */
    lv_indev_set_mode(pointer, LV_INDEV_MODE_EVENT);
    size_t ttf_len = 0;
    const unsigned char *ttf = font_ttf_data(&ttf_len);
    for (int i = 0; i < 5; i++)
        fonts[i] = lv_tiny_ttf_create_data(ttf, ttf_len, font_sizes[i]);
    if (!fonts[0] || !fonts[1] || !fonts[2] || !fonts[3] || !fonts[4]) {
        lvgl_ui_shutdown();return 0;
    }
    const int art_ids[] = {PV_ART_BREAD, PV_ART_DIMA, PV_ART_KIRILL,
                           PV_ART_DUCK, PV_ART_DUCK_CONE, PV_ART_DUCK_BUCKET,
                           PV_ART_ROBOT, PV_ART_PEA, PV_ART_WALNUT, PV_ART_SUNFLOWER,
                           PV_ART_JUMPER, PV_ART_LILY, PV_ART_COIN};
    for (size_t j = 0; j < sizeof art_ids / sizeof art_ids[0]; j++) {
        int id = art_ids[j], w = 0, h = 0;
        const uint32_t *original = game_art_rgba(id, &w, &h);
        if (!original || w <= 0 || h <= 0) continue;
        uint32_t *pixels = malloc((size_t)w * h * sizeof *pixels);
        if (!pixels) continue;
        for (int p = 0; p < w * h; p++) {
            uint32_t src = original[p]; /* game memory: R,G,B,A */
            pixels[p] = (src & 0xff00ff00u) |
                        ((src & 0x000000ffu) << 16) |
                        ((src & 0x00ff0000u) >> 16); /* LVGL: B,G,R,A */
        }
        pictures[id].header.magic = LV_IMAGE_HEADER_MAGIC;
        pictures[id].header.cf = LV_COLOR_FORMAT_ARGB8888;
        pictures[id].header.w = (uint16_t)w;
        pictures[id].header.h = (uint16_t)h;
        pictures[id].header.stride = (uint16_t)(w * 4);
        pictures[id].data_size = (uint32_t)(w * h * 4);
        pictures[id].data = (const uint8_t *)pixels;
    }
    page = 0;chosen_map = 1;active_phase = -1;drag_index = -1;
    drag_ghost = drag_target = NULL;
    drag_notice_left = 0;drag_notice[0] = 0;
    pointer_down = captured = touch_x = touch_y = search_open = dirty = 0;
    search_code[0] = local_notice[0] = 0;
    return 1;
}

void lvgl_ui_shutdown(void) {
    if (display) {
        lv_display_delete(display);
        display = NULL;screen = NULL;pointer = NULL;
        for (int i = 0; i < 5; ++i) {
            if (fonts[i]) lv_tiny_ttf_destroy(fonts[i]);
            fonts[i] = NULL;
        }
        lv_deinit();
    }
    for (int i = 0; i < PV_ART_COUNT; i++) {
        free((void *)pictures[i].data);
        memset(&pictures[i], 0, sizeof pictures[i]);
    }
    free(drawbuf);drawbuf = NULL;
    free(layer);layer = NULL;
    active_phase = -1;drag_index = -1;
    drag_ghost = drag_target = NULL;
}

int lvgl_ui_fullscreen(int phase) {
    return phase == GAME_MENU || phase == GAME_SELECT ||
           phase == GAME_BOOK || phase == GAME_ONLINE_ROOMS ||
           phase == GAME_ONLINE_LOBBY;
}

int lvgl_ui_pointer(int x, int y, int down) {
    if (!display) return 0;
    if (x < 0) x = 0;
    if (x >= GAME_W) x = GAME_W - 1;
    if (y < 0) y = 0;
    if (y >= GAME_H) y = GAME_H - 1;
    touch_x = x;touch_y = y;
    if (down) {
        int phase = game_phase();
        int index = packet_at(phase, x, y);
        if (index >= 0) {
            /* The LVGL image is draggable, not a two-tap selection button. */
            captured = 1;
            pointer_down = 0;
            if (packet_ready(phase, index)) {
                drag_phase = phase;
                drag_index = index;
                drag_x = x;drag_y = y;
                dirty = 1;
            }
            return 1;
        }
        captured = lvgl_ui_fullscreen(phase) || phase == GAME_INTRO ||
                   phase == GAME_LEVEL_CLEAR || phase == GAME_WIN ||
                   phase == GAME_LOSE;
        if (phase == GAME_PLAY || phase == GAME_GARDEN)
            captured = y <= 120 || x < 246 || y >= 680;
        if (phase == GAME_ONLINE_MATCH) {
            OnMatch match;
            game_online_ui_snapshot(&match, NULL, NULL, NULL, 0, NULL);
            captured = match.winner || y <= 120 || x < 246 || y >= 680;
        }
        pointer_down = captured;
        if (captured) lv_indev_read(pointer);
        return captured;
    }
    int handled = captured;
    pointer_down = 0;
    if (drag_index >= 0) {
        int index = drag_index, phase = drag_phase;
        drag_index = -1;
        dirty = 1;
        drop_packet(phase, index, x, y);
        handled = 1;
    } else if (handled) lv_indev_read(pointer);
    captured = 0;
    return handled;
}

int lvgl_ui_move(int x, int y) {
    if (!display) return 0;
    if (x < 0) x = 0;
    if (x >= GAME_W) x = GAME_W - 1;
    if (y < 0) y = 0;
    if (y >= GAME_H) y = GAME_H - 1;
    touch_x = x;touch_y = y;
    if (drag_index >= 0) drag_position(x, y);
    else if (captured && pointer_down) lv_indev_read(pointer);
    return captured;
}

int lvgl_ui_cancel(void) {
    int handled = captured;
    if (pointer_down && pointer) lv_indev_wait_release(pointer);
    pointer_down = captured = 0;
    touch_x = touch_y = 0;
    if (handled && pointer) lv_indev_read(pointer);
    if (drag_index >= 0) {drag_index = -1;dirty = 1;}
    return handled;
}

void lvgl_ui_frame(float dt, uint32_t *game_rgba) {
    if (!display || !game_rgba) return;
    if (drag_notice_left > 0) {
        drag_notice_left -= dt;
        if (drag_notice_left <= 0) dirty = 1;
    }
    int ms = (int)(dt * 1000 + 0.5f);
    if (ms < 1) ms = 1;
    if (ms > 50) ms = 50;
    lv_tick_inc((uint32_t)ms);
    /* Process releases BEFORE deleting/rebuilding widget trees. */
    lv_timer_handler();
    int phase = game_phase();
    if (phase != active_phase) {
        drag_notice_left = 0;
        drag_notice[0] = 0;
    }
    OnNetView v = {0};
    if (phase == GAME_ONLINE_ROOMS || phase == GAME_ONLINE_LOBBY ||
        phase == GAME_ONLINE_MATCH) on_net_view(&v);
    if (drag_index >= 0 &&
        (phase != drag_phase || (phase == GAME_ONLINE_MATCH && v.state.winner))) {
        drag_index = -1;
        dirty = 1;
    }
    uint32_t sig = state_signature(phase, &v);
    /* Never delete a pressed LVGL button during a network refresh or a toast
     * timeout: the release must reach the same widget to generate CLICKED. */
    if (phase != active_phase || (!pointer_down && (sig != view_sig || dirty))) {
        rebuild(phase, &v);
        view_sig = state_signature(phase, &v);
        lv_refr_now(display); /* show network transitions on the same frame */
    }
    if (lvgl_ui_fullscreen(phase)) {
        for (int p = 0; p < GAME_W * GAME_H; ++p) {
            uint32_t c = layer[p];
            /* LVGL ARGB8888 -> the GL texture's byte order RGBA. */
            game_rgba[p] = (c & 0xff00ff00u) |
                           ((c & 0x00ff0000u) >> 16) |
                           ((c & 0x000000ffu) << 16);
        }
    } else {
        for (int p = 0; p < GAME_W * GAME_H; ++p) {
            uint32_t c = layer[p];
            unsigned a = c >> 24;
            if (!a) continue;
            uint32_t rgb = (c & 0xff00ff00u) |
                           ((c & 0x00ff0000u) >> 16) |
                           ((c & 0x000000ffu) << 16);
            if (a == 255) game_rgba[p] = rgb;
            else {
                unsigned inv = 255 - a, base = game_rgba[p];
                unsigned r = ((rgb & 255u) * a + (base & 255u) * inv) / 255;
                unsigned g = (((rgb >> 8) & 255u) * a + ((base >> 8) & 255u) * inv) / 255;
                unsigned b = (((rgb >> 16) & 255u) * a + ((base >> 16) & 255u) * inv) / 255;
                game_rgba[p] = 0xff000000u | (b << 16) | (g << 8) | r;
            }
        }
    }
}
