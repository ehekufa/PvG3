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
    U_CREATE, U_SEARCH, U_SEARCH_CLOSE, U_SEARCH_ERASE, U_SEARCH_GO,
    U_ROOMS_BACK, U_LOBBY_EXIT, U_PLANTS, U_ZOMBIES, U_MATCH_BOOK,
    U_MATCH_EXIT, U_MATCH_FINISH, U_MATCH_RETURN,
    U_GARDEN_BOOK, U_GARDEN_EXIT, U_GARDEN_ERASE,
    U_BOOK_BACK, U_BOOK_PLANTS, U_BOOK_ENEMIES,
    U_INTRO_NEXT, U_INTRO_SKIP, U_OFFLINE_BOOK, U_OFFLINE_MENU,
    U_RESULT_NEXT, U_RESULT_RETRY, U_RESULT_MENU,
    U_LEVEL_BASE = 100, U_ROOM_BASE = 200, U_KEY_BASE = 300, U_SEED_BASE = 400,
    U_OFFLINE_SEED_BASE = 500, U_GARDEN_SEED_BASE = 600,
    U_BOOK_ENTRY_BASE = 700
};

static lv_display_t *display;
static lv_indev_t *pointer;
static lv_obj_t *screen;
static lv_font_t *fonts[5], *packet_font;
static const int font_sizes[5] = {19, 24, 30, 40, 54};
static lv_image_dsc_t pictures[PV_ART_COUNT];
static uint32_t *layer;
static uint8_t *drawbuf;
static int active_phase = -1, page = 0, chosen_map = 1, search_open;
static int pointer_down, captured, touch_x, touch_y, dirty;
static uint32_t view_sig;
static char search_code[ON_ROOM_ID_SIZE], local_notice[110];
static char visible_ids[8][ON_ROOM_ID_SIZE];

static const lv_font_t *f(int index) { return fonts[index] ? fonts[index] : LV_FONT_DEFAULT; }

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

/* Same original duck with simple cone/helmet geometry on top, as in the
 * legacy renderer. These are protective items, never separate characters. */
static void duck_art(lv_obj_t *parent, int cx, int cy, int size, int variant) {
    art(parent, PV_ART_DUCK, cx, cy, size);
    if (variant != ON_CONE && variant != ON_HELMET) return;
    int brim = cy - size / 2 + size * 34 / 100;
    int head = cx + size * 4 / 100;
    if (variant == ON_CONE) {
        int tip = brim - size * 52 / 100;
        for (int i = 0; i < 10; ++i) {
            int y = tip + i * (brim - tip) / 10;
            int half = 2 + i * size * 25 / 1000;
            box(parent, head - half, y, 2 * half, (brim - tip) / 10 + 1,
                0, C(EB782C), 0);
        }
        box(parent, head - size * 29 / 100, brim - size / 17,
            size * 58 / 100, size / 13, 1, C(A84929), 0);
    } else {
        box(parent, head - size * 30 / 100, brim - size * 27 / 100,
            size * 60 / 100, size * 27 / 100, size / 7, C(647887), 0);
        box(parent, head - size * 24 / 100, brim - size * 22 / 100,
            size * 24 / 100, size / 20 + 2, size / 22, C(A6B8B8), 0);
        box(parent, head - size * 33 / 100, brim - size / 23,
            size * 66 / 100, size / 13, 2, C(354F5D), 0);
    }
}

static void header(lv_obj_t *root, const char *kicker, const char *title,
                   const char *subtitle, const char *back, int action) {
    box(root, 0, 0, GAME_W, 148, 0, DARK, 0);
    box(root, 0, 145, GAME_W, 4, 0, GOLD, 0);
    label(root, 73, 31, 785, 27, kicker, 1, GOLD, LV_TEXT_ALIGN_LEFT);
    label(root, 70, 61, 900, 64, title, 4, WHITE, LV_TEXT_ALIGN_LEFT);
    if (subtitle) label(root, 73, 117, 890, 30, subtitle, 1,
                        C(C5D8B7), LV_TEXT_ALIGN_LEFT);
    if (back) button(root, 1065, 48, 175, 62, back, 2, C(476750),
                     WHITE, action);
}

static void menu_screen(lv_obj_t *root) {
    header(root, "МИР КИРИЛЛА  /  РАСТЕНИЯ И ГУСИ", "Растения против гусей 3",
           "Приключение Кирилла: сад, утки и история для друзей", NULL, 0);
    button(root, 730, 51, 220, 64, "Уровни", 2, C(476750), WHITE, U_MENU_LEVELS);
    button(root, 971, 51, 270, 64, "Сад Дзен", 2, C(476750), WHITE, U_MENU_GARDEN);
    box(root, 55, 179, 1170, 350, 25, PAPER, 1);
    box(root, 77, 202, 347, 301, 22, C(E0EDCC), 0);
    box(root, 466, 202, 347, 301, 22, C(E4EAD2), 0);
    box(root, 855, 202, 347, 301, 22, C(EEE1C4), 0);
    art(root, PV_ART_BREAD, 250, 324, 206);
    art(root, PV_ART_DIMA, 640, 326, 210);
    art(root, PV_ART_KIRILL, 1029, 324, 205);
    label(root, 93, 439, 310, 55, "Хлебушек", 2, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 482, 439, 310, 55, "Дима в маске", 2, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 871, 439, 310, 55, "Кирилл", 2, INK, LV_TEXT_ALIGN_CENTER);
    button(root, 90, 563, 280, 94, "Умная книга", 2, WHITE, INK, U_MENU_BOOK);
    button(root, 410, 548, 445, 125, "Начать игру", 3, GOLD, INK, U_MENU_PLAY);
    button(root, 895, 563, 295, 94, "Играть вдвоём", 2, SAGE, INK, U_MENU_ONLINE);
    char detail[160];
    snprintf(detail, sizeof detail, "Автосохранение   ·   пройдено %d / 10   ·   продолжить с уровня %d",
             game_completed_level(), game_resume_level());
    label(root, 230, 683, 820, 27, detail, 0, MUTED, LV_TEXT_ALIGN_CENTER);
}

static void levels_screen(lv_obj_t *root) {
    header(root, "ИСТОРИЯ КИРИЛЛА", "Выбери уровень",
           "Уровень 0 — кат-сцена; бой кончается, когда вся волна побеждена",
           "Назад", U_MENU_BACK);
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
        label(o, 18, 93, 177, 39, n == 5 ? "Водная карта" :
              n == 10 ? "Робот Королевы" : "Обычный газон", 1,
              MUTED, LV_TEXT_ALIGN_LEFT);
    }
    button(root, 414, 590, 452, 67, "Уровень 0 · повторить историю", 2,
           GOLD, INK, U_INTRO);
    label(root, 295, 672, 690, 28,
          "Кат-сцену можно пересматривать: сохранение останется на месте.",
          0, MUTED, LV_TEXT_ALIGN_CENTER);
}

/* Offline navigation and HUD share the same real LVGL widgets as online. The
 * plants, map, coins and opponents beneath them are still the original game. */
static void garden_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    box(root, 0, 0, 1280, 121, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    label(root, 22, 35, 225, 42, "Сад Дзен", 2, WHITE, LV_TEXT_ALIGN_LEFT);
    label(root, 23, 78, 228, 30, "Создания Кирилла", 0, GOLD, LV_TEXT_ALIGN_LEFT);
    for (int i = 0; i < 5; i++) {
        GameBookEntry entry;
        if (!game_book_entry(0, i, &entry)) continue;
        int x = 260 + i * 130;
        lv_obj_t *card = button(root, x, 16, 120, 99, "", 0,
                                state.garden_selection == i ? GOLD : PAPER,
                                INK, U_GARDEN_SEED_BASE + i);
        art(card, entry.art_id, 60, 38, 52);
        lv_obj_t *caption = label(card, 3, 74, 114, 27,
                                  entry.short_name, 0, INK,
                                  LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_font(caption, packet_font, 0);
    }
    button(root, 918, 22, 145, 44, "Книга", 1, C(476750), WHITE, U_GARDEN_BOOK);
    button(root, 1079, 22, 179, 44, "В меню", 1, C(476750), WHITE, U_GARDEN_EXIT);
    button(root, 918, 77, 340, 39, state.garden_selection == 5 ?
           "Убираем растение" : "Убрать растение", 1,
           state.garden_selection == 5 ? GOLD : SAGE, INK, U_GARDEN_ERASE);
    box(root, 250, 681, 1030, 39, 0, DARK, 0);
    label(root, 275, 688, 980, 29,
          "Выбери растение и клетку. В саду все растения бесплатны.",
          0, WHITE, LV_TEXT_ALIGN_CENTER);
}

static void book_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    header(root, "ЗАПИСИ КИРИЛЛА  /  ЖИВАЯ КНИГА", "Умная книга",
           "Растения и противники с рисунков Кирилла", "Назад", U_BOOK_BACK);
    box(root, 63, 179, 513, 505, 22, PAPER, 1);
    box(root, 596, 179, 633, 505, 22, WHITE, 1);
    button(root, 84, 194, 220, 54, "Растения", 1,
           state.book_enemy_tab ? WHITE : SAGE, INK, U_BOOK_PLANTS);
    button(root, 314, 194, 226, 54, "Противники", 1,
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
        label(card, 81, 7, 371, 40, entry.short_name, 1,
              INK, LV_TEXT_ALIGN_LEFT);
        char info[90];
        if (state.book_enemy_tab) snprintf(info, sizeof info, "Здоровье: %d", entry.hp);
        else snprintf(info, sizeof info, "Цена: %d монет", entry.cost);
        label(card, 82, 40, 325, 26, info, 0, MUTED, LV_TEXT_ALIGN_LEFT);
    }
    if (state.book_enemy_tab)
        label(root, 93, 596, 452, 71,
              "Конус и шлем защищают ту же самую утку с рисунка автора.",
              0, MUTED, LV_TEXT_ALIGN_CENTER);
    GameBookEntry entry;
    if (!game_book_entry(state.book_enemy_tab, state.book_selection, &entry)) return;
    box(root, 618, 198, 589, 193, 20,
        state.book_enemy_tab ? C(F3E7D0) : C(E7F0DB), 0);
    if (state.book_enemy_tab && entry.art_id == PV_ART_DUCK)
        duck_art(root, 913, 303, 155, entry.enemy_variant);
    else art(root, entry.art_id, 913, 295, entry.art_id == PV_ART_ROBOT ? 190 : 175);
    label(root, 617, 398, 592, 53, entry.name, 3, INK,
          LV_TEXT_ALIGN_CENTER);
    char stats[135];
    if (state.book_enemy_tab) {
        if (entry.art_id == PV_ART_ROBOT)
            snprintf(stats, sizeof stats,
                     "Здоровье: %d\nПриходит в финале уровня 10", entry.hp);
        else snprintf(stats, sizeof stats,
                      "Здоровье: %d\nОтправить онлайн: %d монет",
                      entry.hp, entry.cost);
    } else snprintf(stats, sizeof stats,
                    "Цена: %d монет  ·  здоровье: %d\nПерезарядка: %.1f с",
                    entry.cost, entry.hp, (double)entry.recharge);
    label(root, 635, 449, 554, 68, stats, 1, MUTED, LV_TEXT_ALIGN_CENTER);
    box(root, 616, 528, 594, 135, 17, C(F7E7CD), 0);
    label(root, 640, 542, 550, 52, entry.description, 1, INK,
          LV_TEXT_ALIGN_CENTER);
    label(root, 640, 592, 550, 45, entry.detail, 0, MUTED,
          LV_TEXT_ALIGN_CENTER);
    label(root, 676, 665, 476, 34, "Рисунок и растения: Кирилл",
          0, MUTED, LV_TEXT_ALIGN_CENTER);
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
    button(root, 1018, 43, 238, 66, "Пропустить", 1,
           C(476750), WHITE, U_INTRO_SKIP);
    box(root, 93, 533, 1094, 149, 22, PAPER, 1);
    const char *speaker = state.intro_step == 0 ? "ХЛЕБУШЕК" :
                          state.intro_step == 1 ? "ДИМА" : "КИРИЛЛ";
    const char *line = state.intro_step == 0 ? "Хлебушек плачет..." :
                       state.intro_step == 1 ? "Не плачь, мы нового сделаем." :
                       "Может, кто-то помогать мне будет?";
    label(root, 136, 550, 918, 33, speaker, 1, MUTED, LV_TEXT_ALIGN_LEFT);
    label(root, 136, 590, 982, 61, line, 3, INK, LV_TEXT_ALIGN_LEFT);
    label(root, 973, 685, 243, 30, "Коснись: дальше  >", 0,
          WHITE, LV_TEXT_ALIGN_RIGHT);
}

static void play_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    box(root, 0, 0, 1280, 121, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    box(root, 0, 121, 246, 599, 0, C(334B3B), 0);
    label(root, 22, 40, 210, 30, "МОНЕТЫ", 1, GOLD, LV_TEXT_ALIGN_LEFT);
    box(root, 22, 76, 34, 34, 17, GOLD, 0);
    label(root, 24, 78, 30, 30, "М", 0, INK, LV_TEXT_ALIGN_CENTER);
    char coins[25];snprintf(coins, sizeof coins, "%d", state.coins);
    label(root, 67, 73, 159, 48, coins, 3, WHITE, LV_TEXT_ALIGN_LEFT);
    char title[100];
    if (state.boss_health_percent > 0)
        snprintf(title, sizeof title, "Уровень %d  ·  Робот Королевы: %d%%",
                 state.level, state.boss_health_percent);
    else snprintf(title, sizeof title, "Уровень %d  ·  Осталось врагов: %d из %d",
                  state.level, state.wave_remaining, state.wave_total);
    label(root, 270, 40, 639, 45, title, 2, WHITE, LV_TEXT_ALIGN_LEFT);
    label(root, 272, 82, 630, 28,
          state.level == 5 ? "Вода: сначала кувшинка, потом растение" :
          "Выбери растение, затем свободную клетку. Собирай монеты!",
          0, C(C4D9BB), LV_TEXT_ALIGN_LEFT);
    button(root, 929, 43, 150, 64, "Книга", 2, C(476750), WHITE, U_OFFLINE_BOOK);
    button(root, 1094, 43, 165, 64, "В меню", 1, C(476750), WHITE, U_OFFLINE_MENU);
    for (int i = 0; i < 5; ++i) {
        GameBookEntry entry;
        if (!game_book_entry(0, i, &entry)) continue;
        int y = 137 + i * 107;
        lv_obj_t *card = button(root, 20, y, 210, 100, "", 0,
                                state.selection == i ? GOLD : PAPER,
                                INK, U_OFFLINE_SEED_BASE + i);
        art(card, entry.art_id, 50, 50, 65);
        label(card, 80, 16, 124, 42, entry.short_name,
              0, INK, LV_TEXT_ALIGN_LEFT);
        char price[44];
        if (state.cooldown[i] > .05f)
            snprintf(price, sizeof price, "Ждать %.0f с", (double)ceilf(state.cooldown[i]));
        else snprintf(price, sizeof price, "%d монет", entry.cost);
        label(card, 80, 67, 126, 30, price, 0,
              state.coins < entry.cost || state.cooldown[i] > 0 ?
              C(A37260) : C(59804F), LV_TEXT_ALIGN_LEFT);
        if (state.coins < entry.cost || state.cooldown[i] > 0 ||
            (i == 4 && state.level != 5))
            lv_obj_set_style_bg_color(card, C(EEE7D5), 0);
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
    label(root, 372, 322, 536, 73,
          phase == GAME_LEVEL_CLEAR ? "Волна побеждена. Готов к следующей?" :
          phase == GAME_WIN ? "Кирилл и гуси спасены!" :
          "Попробуй снова — сохранение не пропало.",
          1, MUTED, LV_TEXT_ALIGN_CENTER);
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
    header(root, "МИР КИРИЛЛА  /  ИГРА ВДВОЁМ", "Играем вместе",
           "Создай комнату или зайди к другу", "В меню", U_ROOMS_BACK);
    box(root, 43, 162, 1195, 89, 19, PAPER, 1);
    label(root, 68, 167, 410, 23, "ПОЛЕ ДЛЯ НОВОЙ КОМНАТЫ", 0, MUTED,
          LV_TEXT_ALIGN_LEFT);
    lv_obj_t *lawn = button(root, 64, 191, 207, 54, "Газон", 2,
                             chosen_map == 1 ? SAGE : WHITE, INK, U_MAP_LAWN);
    lv_obj_t *water = button(root, 282, 191, 210, 54, "Вода", 2,
                              chosen_map == 5 ? BLUE : WHITE, INK, U_MAP_WATER);
    lv_obj_set_style_border_width(chosen_map == 1 ? lawn : water, 2, 0);
    lv_obj_set_style_border_color(chosen_map == 1 ? lawn : water,
                                  C(779C73), 0);
    button(root, 720, 179, 208, 62, "□  Поиск", 2, WHITE, INK, U_SEARCH);
    button(root, 941, 179, 275, 62, "+  Создать", 2, GOLD, INK, U_CREATE);
    box(root, 43, 271, 1195, 421, 23, PAPER, 1);
    label(root, 70, 286, 550, 52, "Свободные комнаты", 3, INK,
          LV_TEXT_ALIGN_LEFT);
    char count[60];
    if (v->notice[0]) snprintf(count, sizeof count, "Проблема со связью");
    else if (!v->connected) snprintf(count, sizeof count, "Ищем комнаты...");
    else snprintf(count, sizeof count, "Свободно: %d", v->room_count);
    label(root, 568, 295, 310, 34, count, 1,
          v->notice[0] ? C(A1513B) : MUTED, LV_TEXT_ALIGN_LEFT);
    button(root, 1027, 284, 193, 48, "Обновить", 1, WHITE, INK, U_REFRESH);
    box(root, 69, 341, 1142, 2, 0, C(D9DDC5), 0);
    if (v->room_count == 0) {
        box(root, 452, 360, 376, 125, 24, C(E6EFDB), 0);
        art(root, PV_ART_PEA, 544, 417, 118);
        art(root, PV_ART_DUCK, 740, 418, 116);
        const char *title = v->notice[0] ? "Проверим подключение" :
                            !v->connected ? "Ищем комнаты..." :
                            "Здесь пока тихо";
        label(root, 285, 497, 710, 52, title, 3, INK, LV_TEXT_ALIGN_CENTER);
        label(root, 253, 544, 774, 37,
              v->notice[0] ? v->notice : !v->connected ?
              "Это займёт пару секунд." :
              "Создай комнату — и пригласи друга по коду.", 1, MUTED,
              LV_TEXT_ALIGN_CENTER);
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
            box(card, 11, 10, 60, 50, 11,
                v->rooms[idx].map == 5 ? BLUE : SAGE, 0);
            label(card, 89, 4, 340, 40, v->rooms[idx].id, 2, INK,
                  LV_TEXT_ALIGN_LEFT);
            label(card, 91, 39, 340, 28,
                  v->rooms[idx].map == 5 ? "Водный уровень" : "Обычный газон",
                  0, MUTED, LV_TEXT_ALIGN_LEFT);
            label(card, width - 194, 20, 173, 35, "Войти  >", 1,
                  C(477E4D), LV_TEXT_ALIGN_CENTER);
        }
        if (v->room_count > 8) {
            button(root, 77, 660, 168, 43, "Назад", 1, WHITE, INK, U_ROOM_BASE + 8);
            button(root, 1035, 660, 168, 43, "Дальше", 1, WHITE, INK,
                   U_ROOM_BASE + 9);
        } else if (v->room_count <= 2) {
            int y = v->room_count == 1 ? 465 : 542;
            label(root, 260, y, 760, 45,
                  "Комната найдена! Коснись карточки, чтобы войти.",
                  2, INK, LV_TEXT_ALIGN_CENTER);
            label(root, 305, y + 42, 670, 34,
                  "Не видишь комнату друга? Найди её по коду сверху.",
                  1, MUTED, LV_TEXT_ALIGN_CENTER);
            button(root, 475, y + 88, 330, 56,
                   "Поиск по коду", 1, SAGE, INK, U_SEARCH);
        } else label(root, 255, 665, 770, 29,
                     "Коснись комнаты, чтобы войти. Занятые скрыты.",
                     0, MUTED, LV_TEXT_ALIGN_CENTER);
    }
    if (!search_open) return;
    lv_obj_t *veil = box(root, 0, 0, 1280, 720, 0, C(14251E), 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_80, 0);
    box(root, 216, 72, 848, 611, 26, PAPER, 1);
    label(root, 259, 111, 590, 58, "Найти комнату по коду", 3, INK,
          LV_TEXT_ALIGN_LEFT);
    label(root, 261, 161, 682, 34,
          "Попроси у друга шесть букв или цифр из заголовка.",
          1, MUTED, LV_TEXT_ALIGN_LEFT);
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
    header(root, "МИР КИРИЛЛА  /  ИГРА ВДВОЁМ", title,
           "Назови другу этот код, чтобы он смог зайти", "Выйти", U_LOBBY_EXIT);
    box(root, 89, 166, 1102, 83, 19, WHITE, 1);
    box(root, 108, 181, 73, 53, 12, v->map == 5 ? BLUE : SAGE, 0);
    label(root, 203, 186, 403, 51,
          v->map == 5 ? "Водный уровень" : "Обычный газон", 2, INK,
          LV_TEXT_ALIGN_LEFT);
    box(root, 876, 185, 285, 45, 14,
        v->guest_id[0] ? SAGE : C(F2E4C9), 0);
    label(root, 892, 191, 257, 35, v->guest_id[0] ?
          "Друг уже в комнате" : "Ждём второго игрока", 1, INK,
          LV_TEXT_ALIGN_CENTER);
    int mine = v->slot == ON_SLOT_HOST ? v->host_role : v->guest_role;
    int other = v->slot == ON_SLOT_HOST ? v->guest_role : v->host_role;
    for (int side = ON_ROLE_PLANTS; side <= ON_ROLE_ZOMBIES; ++side) {
        int x = side == ON_ROLE_PLANTS ? 90 : 663;
        int chosen = mine == side, taken = other == side;
        lv_obj_t *card = button(root, x, 268, 527, 317, "", 1,
                                chosen ? GOLD : WHITE, INK,
                                side == ON_ROLE_PLANTS ? U_PLANTS : U_ZOMBIES);
        box(card, 6, 6, 515, 305, 20,
            side == ON_ROLE_PLANTS ? C(EAF3DC) : C(F9EBD4), 0);
        box(card, 24, 23, 189, 211, 19,
            side == ON_ROLE_PLANTS ? C(D4E9BE) : C(EFDABB), 0);
        art(card, side == ON_ROLE_PLANTS ? PV_ART_PEA : PV_ART_DUCK,
            119, 128, 165);
        label(card, 231, 44, 283, 48,
              side == ON_ROLE_PLANTS ? "За растения" : "За зомби", 3, INK,
              LV_TEXT_ALIGN_LEFT);
        label(card, 232, 108, 264, 75,
              side == ON_ROLE_PLANTS ? "Посади защиту.\nСобирай монеты." :
              "Выпусти уток.\nВыбери нужный ряд.", 1, MUTED,
              LV_TEXT_ALIGN_LEFT);
        box(card, 26, 247, 475, 53, 13,
            chosen ? DARK : taken ? C(E1E2D5) : WHITE, 0);
        label(card, 40, 256, 449, 37,
              chosen ? "Ты играешь здесь" : taken ? "Здесь играет друг" :
              "Нажми, чтобы выбрать", 1, chosen ? WHITE : INK,
              LV_TEXT_ALIGN_CENTER);
    }
    box(root, 90, 609, 1100, 78, 17, DARK, 0);
    const char *status = !v->guest_id[0] ? "Ждём друга" :
                         v->busy ? "Запоминаем твой выбор..." :
                         !mine && other == ON_ROLE_PLANTS ?
                         "Друг защищает сад. Выбери зомби справа!" :
                         !mine && other == ON_ROLE_ZOMBIES ?
                         "Утки уже у друга. Выбери растения слева!" :
                         !mine ? "Выбери свою сторону" : !other ?
                         "Ты готов! Теперь очередь друга" :
                         "Оба готовы! Начинаем бой...";
    label(root, 138, 622, 1004, 40, status, 2, WHITE, LV_TEXT_ALIGN_CENTER);
    label(root, 128, 659, 1024, 26,
          v->notice[0] ? v->notice : !v->guest_id[0] ?
          "Скажи другу код сверху. Сторону можно выбрать заранее." :
          "Игра начнётся сама, когда вы выберете разные стороны.",
          0, v->notice[0] ? GOLD : C(C6D9BC), LV_TEXT_ALIGN_CENTER);
}

static void match_screen(lv_obj_t *root, const OnNetView *v) {
    OnMatch state;
    int role = 0, selected = -1;
    char hint[110];float hint_left = 0;
    game_online_ui_snapshot(&state, &role, &selected, hint, sizeof hint, &hint_left);
    int plants = role == ON_ROLE_PLANTS;
    box(root, 0, 0, 1280, 120, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, GOLD, 0);
    box(root, 0, 121, 246, 599, 0, C(334B3B), 0);
    label(root, 22, 40, 215, 34, plants ? "ЗА РАСТЕНИЯ" : "ЗА ЗОМБИ",
          1, GOLD, LV_TEXT_ALIGN_LEFT);
    box(root, 22, 75, 33, 33, 17, GOLD, 0);
    label(root, 24, 78, 29, 30, "М", 0, INK, LV_TEXT_ALIGN_CENTER);
    char cash[24];
    snprintf(cash, sizeof cash, "%d", plants ? state.plant_cash : state.zombie_cash);
    label(root, 66, 73, 159, 48, cash, 3, WHITE, LV_TEXT_ALIGN_LEFT);
    char title[86];
    snprintf(title, sizeof title, "Комната %s  ·  осталось уток: %d",
             v->room_id, state.left + state.duck_count);
    label(root, 272, 37, 634, 40, title, 2, WHITE, LV_TEXT_ALIGN_LEFT);
    label(root, 274, 78, 630, 31,
          state.map == 5 ? "Вода: сначала кувшинка, потом растение" :
          "Выбери карточку, затем клетку или ряд на поле", 1,
          C(C4D9BB), LV_TEXT_ALIGN_LEFT);
    button(root, 930, 43, 150, 66, "Книга", 2, C(476750), WHITE, U_MATCH_BOOK);
    button(root, 1094, 43, 165, 66, "Выйти", 2, C(476750), WHITE, U_MATCH_EXIT);
    static const int arts[5] = {PV_ART_PEA, PV_ART_WALNUT,
                                PV_ART_SUNFLOWER, PV_ART_JUMPER, PV_ART_LILY};
    static const char *names[5] = {"Горохострел", "Орех", "Подсолнух",
                                    "Джампер", "Кувшинка"};
    static const char *ducks[3] = {"Утка-зомби", "Утка с конусом", "Утка в шлеме"};
    int n = plants ? 5 : 3;
    for (int i = 0; i < n; ++i) {
        int top = plants ? 125 + i * 104 : 151 + i * 113;
        int price = plants ? on_plant_cost[i] : on_duck_cost[i];
        float wait = plants ? state.plant_cooldown[i] : state.duck_cooldown[i];
        int affordable = (plants ? state.plant_cash : state.zombie_cash) >= price &&
                         wait <= 0 && (plants ? (i != ON_LILY || state.map == 5) :
                                       state.left > 0);
        lv_obj_t *card = button(root, 12, top, 223, 98, "", 0,
                                selected == i ? GOLD : PAPER, INK,
                                U_SEED_BASE + i);
        if (plants) art(card, arts[i], 53, 49, 69);
        else duck_art(card, 53, 49, 69, on_duck_type[i]);
        label(card, 86, 14, 134, 41, plants ? names[i] : ducks[i],
              0, INK, LV_TEXT_ALIGN_LEFT);
        char cost[48];
        if (wait > 0.05f) snprintf(cost, sizeof cost, "Ждать %.0f с", ceilf(wait));
        else snprintf(cost, sizeof cost, "%d монет", price);
        label(card, 86, 64, 134, 31, cost, 0,
              affordable ? C(59804F) : C(A37260), LV_TEXT_ALIGN_LEFT);
        if (!affordable) lv_obj_set_style_bg_color(card, C(EEE6D4), 0);
    }
    if (!plants) {
        label(root, 15, 527, 220, 59, "Выбери утку,\nзатем ряд на поле", 0,
              C(D9E8CE), LV_TEXT_ALIGN_CENTER);
        button(root, 15, 615, 217, 80, "Закончить", 1, GOLD, INK, U_MATCH_FINISH);
    }
    box(root, 248, 681, 1032, 39, 0, DARK, 0);
    label(root, 285, 686, 955, 32, hint_left > 0 ? hint :
          v->notice[0] ? v->notice : v->pending ?
          "Ждём подтверждения прошлого хода..." : !v->guest_id[0] ?
          "Друг вышел. Бой поставлен на паузу." : state.map == 5 ?
          "На воде сначала ставь кувшинку." :
          "Карточка + клетка. Коснись монеты, чтобы собрать.",
          0, hint_left > 0 || v->notice[0] ? GOLD : WHITE,
          LV_TEXT_ALIGN_CENTER);
    if (state.winner) {
        lv_obj_t *shade = box(root, 0, 0, 1280, 720, 0, C(14251E), 0);
        lv_obj_set_style_bg_opa(shade, LV_OPA_80, 0);
        box(root, 309, 177, 662, 402, 26, PAPER, 1);
        art(root, state.winner == ON_WIN_PLANTS ? PV_ART_PEA : PV_ART_DUCK,
            638, 295, 126);
        label(root, 365, 369, 550, 70,
              state.winner == role ? "Победа!" : "Победил соперник",
              4, INK, LV_TEXT_ALIGN_CENTER);
        label(root, 367, 434, 546, 35,
              state.winner == ON_WIN_PLANTS ? "Растения спасли сад" :
              "Утки захватили сад", 1, MUTED, LV_TEXT_ALIGN_CENTER);
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
    if (code >= U_GARDEN_SEED_BASE && code < U_GARDEN_SEED_BASE + 5) {
        game_input_press(320 + (code - U_GARDEN_SEED_BASE) * 130, 60);
        dirty = 1;return;
    }
    if (code >= U_BOOK_ENTRY_BASE && code < U_BOOK_ENTRY_BASE + 5) {
        game_input_press(300, 181 + (code - U_BOOK_ENTRY_BASE) * 99);
        dirty = 1;return;
    }
    if (code >= U_OFFLINE_SEED_BASE && code < U_OFFLINE_SEED_BASE + 5) {
        game_input_press(125, 185 + (code - U_OFFLINE_SEED_BASE) * 107);
        dirty = 1;return;
    }
    if (code >= U_SEED_BASE && code < U_SEED_BASE + 5) {
        int i = code - U_SEED_BASE;
        int y = active_phase == GAME_ONLINE_MATCH ?
                175 + i * 104 : 200;
        OnMatch s;int role;
        game_online_ui_snapshot(&s, &role, NULL, NULL, 0, NULL);
        if (role == ON_ROLE_ZOMBIES) y = 201 + i * 113;
        game_input_press(120, y);dirty = 1;return;
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
    size_t ttf_len = 0;
    const unsigned char *ttf = font_ttf_data(&ttf_len);
    for (int i = 0; i < 5; i++)
        fonts[i] = lv_tiny_ttf_create_data(ttf, ttf_len, font_sizes[i]);
    packet_font = lv_tiny_ttf_create_data(ttf, ttf_len, 16);
    if (!fonts[0] || !fonts[1] || !fonts[2] || !fonts[3] || !fonts[4] ||
        !packet_font) {
        lvgl_ui_shutdown();return 0;
    }
    const int art_ids[] = {PV_ART_BREAD, PV_ART_DIMA, PV_ART_KIRILL,
                           PV_ART_DUCK, PV_ART_ROBOT, PV_ART_PEA, PV_ART_WALNUT,
                           PV_ART_SUNFLOWER, PV_ART_JUMPER, PV_ART_LILY};
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
    page = 0;chosen_map = 1;active_phase = -1;
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
        if (packet_font) lv_tiny_ttf_destroy(packet_font);
        packet_font = NULL;
        lv_deinit();
    }
    for (int i = 0; i < PV_ART_COUNT; i++) {
        free((void *)pictures[i].data);
        memset(&pictures[i], 0, sizeof pictures[i]);
    }
    free(drawbuf);drawbuf = NULL;
    free(layer);layer = NULL;
    active_phase = -1;
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
    if (down) {
        int phase = game_phase();
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
    } else pointer_down = 0;
    touch_x = x;touch_y = y;
    return captured;
}

void lvgl_ui_frame(float dt, uint32_t *game_rgba) {
    if (!display || !game_rgba) return;
    int ms = (int)(dt * 1000 + 0.5f);
    if (ms < 1) ms = 1;
    if (ms > 50) ms = 50;
    lv_tick_inc((uint32_t)ms);
    /* Process releases BEFORE deleting/rebuilding widget trees. */
    lv_timer_handler();
    int phase = game_phase();
    OnNetView v = {0};
    if (phase == GAME_ONLINE_ROOMS || phase == GAME_ONLINE_LOBBY ||
        phase == GAME_ONLINE_MATCH) on_net_view(&v);
    uint32_t sig = state_signature(phase, &v);
    if (phase != active_phase || sig != view_sig || dirty) {
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
