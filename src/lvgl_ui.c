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
#include "vendor/lvgl/src/draw/lv_draw_triangle.h"
#include "vendor/lvgl/src/draw/lv_draw_arc.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C(hex) lv_color_hex(0x##hex)
#define DARK C(242424)
#define INK C(111111)
#define PAPER C(E7E7E7)
#define WHITE C(FFFFFF)
#define LIGHT_GRAY C(D7D7D7)
#define MUTED C(555555)
#define BUTTON_WHITE C(FFFFFF)
#define BUTTON_GRAY C(BDBDBD)
#define BUTTON_BLACK C(000000)
#define BUTTON_TEXT C(111111)
#define BUTTON_LIGHT_TEXT C(FFFFFF)
#define WS_BACKGROUND C(14283C)
#define WS_GRID C(2A4056)
#define WS_BROWN C(8D5438)
#define WS_BROWN_DARK C(693E2C)
#define WS_BROWN_LIGHT C(A46745)
#define WS_CREAM C(FFF2D8)
#define WS_GREEN C(76D62C)
#define WS_CYAN C(27D2D6)
#define WS_YELLOW C(FFC33D)
#define WS_RED C(D95C55)
#define WS_DARK_VALUE C(603D2C)

enum {
    U_MENU_PLAY = 1, U_MENU_LEVELS, U_MENU_GARDEN, U_MENU_BOOK, U_MENU_ONLINE,
    U_MENU_BACK, U_INTRO, U_MAP_LAWN, U_MAP_WATER, U_REFRESH,
    U_CREATE, U_CUSTOM_CATALOG, U_SEARCH, U_SEARCH_CLOSE, U_SEARCH_ERASE, U_SEARCH_GO,
    U_ROOMS_BACK, U_LOBBY_EXIT, U_PLANTS, U_ZOMBIES, U_MATCH_BOOK,
    U_MATCH_EXIT, U_MATCH_FINISH, U_MATCH_RETURN,
    U_GARDEN_BOOK, U_GARDEN_EXIT, U_GARDEN_ERASE,
    U_GARDEN_PLANTS, U_GARDEN_GEESE, U_GARDEN_LAWN, U_GARDEN_WATER,
    U_BOOK_BACK, U_BOOK_PLANTS, U_BOOK_ENEMIES,
    U_INTRO_NEXT, U_INTRO_SKIP, U_OFFLINE_BOOK, U_OFFLINE_MENU,
    U_RESULT_NEXT, U_RESULT_RETRY, U_RESULT_MENU,
    U_CUSTOM_REFRESH, U_CUSTOM_BACK, U_CUSTOM_PREV, U_CUSTOM_NEXT,
    U_MENU_WORKSHOP, U_CUSTOM_WORKSHOP, U_WORKSHOP_BACK, U_WORKSHOP_NEW,
    U_WORKSHOP_OPEN_DRAFT, U_WORKSHOP_CATALOG, U_WORKSHOP_DETAIL_EDIT,
    U_WORKSHOP_SAVE, U_WORKSHOP_PLAY, U_WORKSHOP_PUBLISH, U_WORKSHOP_BUILD,
    U_WORKSHOP_EDITMODE, U_WORKSHOP_MULTIMODE, U_WORKSHOP_DELETE,
    U_WORKSHOP_MOVE_PANEL, U_WORKSHOP_GROUP_PANEL, U_WORKSHOP_COLOR_PANEL,
    U_WORKSHOP_TRANSFORM_PANEL, U_WORKSHOP_COPY, U_WORKSHOP_PASTE,
    U_WORKSHOP_DELETE_SELECTED,
    U_WORKSHOP_OBJECT_WIDTH_EDIT, U_WORKSHOP_OBJECT_HEIGHT_EDIT,
    U_WORKSHOP_OBJECT_ANGLE_EDIT,
    U_WORKSHOP_DIALOG_OK, U_WORKSHOP_EASE_PREV, U_WORKSHOP_EASE_NEXT,
    U_WORKSHOP_GROUP_DEC, U_WORKSHOP_GROUP_INC,
    U_WORKSHOP_LAYER_DEC, U_WORKSHOP_LAYER_INC,
    U_WORKSHOP_LAYER2_DEC, U_WORKSHOP_LAYER2_INC,
    U_WORKSHOP_Z_DEC, U_WORKSHOP_Z_INC,
    U_WORKSHOP_TOUCH, U_WORKSHOP_SPAWN, U_WORKSHOP_SILENT,
    U_WORKSHOP_ADD_GROUP,
    U_WORKSHOP_EDIT_TITLE, U_WORKSHOP_EDIT_DESCRIPTION,
    U_WORKSHOP_TRIGGER_EVENT_PREV, U_WORKSHOP_TRIGGER_EVENT_NEXT,
    U_WORKSHOP_TRIGGER_GROUP_EDIT, U_WORKSHOP_TRIGGER_X_EDIT,
    U_WORKSHOP_TRIGGER_Y_EDIT, U_WORKSHOP_TRIGGER_DURATION_EDIT,
    U_WORKSHOP_TRIGGER_ACTION,
    U_LEVEL_BASE = 100, U_ROOM_BASE = 200, U_KEY_BASE = 300,
    U_BOOK_ENTRY_BASE = 700, U_CUSTOM_LEVEL_BASE = 900,
    U_WORKSHOP_CELL_BASE = 1200,
    U_WORKSHOP_PALETTE_BASE = 1400, U_WORKSHOP_COLOR_BASE = 1420,
    U_WORKSHOP_LAYER_BASE = 1450, U_WORKSHOP_LOCK_X_BASE = 1460,
    U_WORKSHOP_LOCK_Y_BASE = 1470, U_WORKSHOP_COLOR_SET_BASE = 1480,
    U_WORKSHOP_BLOCK_VARIANT_BASE = 1490,
    U_WORKSHOP_TRIGGER_BASE = 1500, U_WORKSHOP_PAN_BASE = 1510,
    U_WORKSHOP_NUDGE_BASE = 1520, U_WORKSHOP_SCALE_BASE = 1530,
    U_WORKSHOP_ROTATE_BASE = 1540
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
static int custom_level_page;
static int pointer_down, captured, touch_x, touch_y, dirty;
#define CUSTOM_TOUCH_MAX 10
typedef struct {int id, active, axis, jump, trigger;} CustomTouch;
static CustomTouch custom_touches[CUSTOM_TOUCH_MAX];
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
static char visible_level_ids[8][ON_LEVEL_ID_SIZE];

enum { WS_TOOL_BUILD, WS_TOOL_EDIT, WS_TOOL_MULTI, WS_TOOL_DELETE };
enum { WS_DIALOG_NONE, WS_DIALOG_MOVE, WS_DIALOG_GROUP, WS_DIALOG_COLOR,
       WS_DIALOG_TRIGGER, WS_DIALOG_TRANSFORM };
enum { WS_INPUT_NONE, WS_INPUT_TITLE, WS_INPUT_DESCRIPTION,
       WS_INPUT_TRIGGER_GROUP, WS_INPUT_TRIGGER_X, WS_INPUT_TRIGGER_Y,
       WS_INPUT_TRIGGER_DURATION, WS_INPUT_OBJECT_WIDTH,
       WS_INPUT_OBJECT_HEIGHT, WS_INPUT_OBJECT_ANGLE };
enum { WS_GRID_COLS = 16, WS_GRID_ROWS = 10,
       WS_OBJECT_CAP = ON_LEVEL_OBJECT_CAP - 3, WS_WORLD_LIMIT = ON_LEVEL_WORLD_LIMIT };
enum { WS_SLIDER_MOVE_X, WS_SLIDER_MOVE_Y, WS_SLIDER_MOVE_TIME };
typedef struct {
    int type;
    float x, y, w, h, angle;
    int group_id, layer, layer2, z_order;
    int color_set, color_index, trigger_kind;
    int trigger_event, trigger_action, trigger_group_id;
    int trigger_x, trigger_y, trigger_duration;
} WorkshopObject;
static void workshop_set_trigger_kind(WorkshopObject *object, int kind);
#define WS_OBJECT_MIN_SIZE .1f
#define WS_OBJECT_MAX_WIDTH 64.0f
#define WS_OBJECT_MAX_HEIGHT 40.0f
static WorkshopObject workshop_objects[WS_OBJECT_CAP];
static WorkshopObject workshop_clipboard[WS_OBJECT_CAP];
static uint8_t workshop_selected_flags[WS_OBJECT_CAP];
static int workshop_object_count, workshop_selected = -1;
static int workshop_clipboard_count;
static int workshop_player_selected, workshop_goal_selected, workshop_ground_selected;
static int workshop_tool, workshop_palette_type = ON_LEVEL_BLOCK;
static int workshop_block_variant = ON_LEVEL_BLOCK;
static int workshop_trigger_kind = ON_TRIGGER_KIND_MOVE;
static int workshop_camera_x, workshop_camera_y;
static float workshop_ground_x, workshop_ground_y = 8, workshop_ground_w = 16,
             workshop_ground_h = 2, workshop_ground_angle;
static float workshop_player_x = 1, workshop_player_y = 7,
             workshop_player_w = .65f, workshop_player_h = .85f,
             workshop_player_angle;
static float workshop_goal_x = 14, workshop_goal_y = 6,
             workshop_goal_w = 1, workshop_goal_h = 2, workshop_goal_angle;
static char workshop_notice[110];
static int workshop_dialog, workshop_draft_exists;
static int workshop_move_x, workshop_move_y, workshop_move_time = 1;
static int workshop_easing, workshop_group_id, workshop_next_group_id = 3;
static int workshop_layer;
static int workshop_layer2, workshop_z_order;
static int workshop_lock_x, workshop_lock_y, workshop_touch = 1;
static int workshop_spawn, workshop_silent, workshop_target_group;
static int workshop_color_set, workshop_color_index;
static int workshop_keyboard_active, workshop_input_kind;
static int workshop_input_return_dialog, workshop_text_language, workshop_text_upper;
static lv_obj_t *workshop_active_textarea;
static char workshop_name[ON_LEVEL_TITLE_SIZE] = "Новый уровень";
static char workshop_description[ON_LEVEL_DESCRIPTION_SIZE];
static const uint32_t workshop_colors[4][8] = {
    {0x55c8eau, 0x64bd63u, 0xe56c5bu, 0xffc54eu,
     0x9560bdu, 0xf0f0f0u, 0x6c452fu, 0x243b58u},
    {0xf27652u, 0x67d3a4u, 0xf7db66u, 0x7c99e6u,
     0xd487e5u, 0x64cce0u, 0xffaa73u, 0xece5d1u},
    {0x243b58u, 0x345978u, 0x537798u, 0x7392adu,
     0xa1b4c4u, 0xced9ddu, 0xf0e6d2u, 0xf1c776u},
    {0xffc54eu, 0xff815eu, 0x73e6b7u, 0x67c9fau,
     0xc690ffu, 0xeff176u, 0xf4f0e7u, 0x6e555cu}
};

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
    (void)shadow; /* flat interface: no drop shadows or glossy highlights */
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    if (w >= 22 && h >= 22) {
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_border_color(o, C(000000), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_shadow_width(o, 0, 0);
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
static void vector_refresh_draw(lv_event_t *event);
static lv_obj_t *workshop_arrow_button(lv_obj_t *parent, int x, int y,
                                       int w, int h, int action,
                                       lv_color_t face, int direction);

static void button_palette(int action, lv_color_t *face, lv_color_t *text) {
    *face = BUTTON_GRAY;
    *text = BUTTON_TEXT;
    switch (action) {
    case U_MENU_BACK: case U_SEARCH_CLOSE: case U_ROOMS_BACK:
    case U_LOBBY_EXIT: case U_MATCH_EXIT: case U_MATCH_RETURN:
    case U_GARDEN_EXIT:
    case U_BOOK_BACK: case U_INTRO_SKIP:
    case U_RESULT_MENU: case U_CUSTOM_BACK: case U_WORKSHOP_BACK:
        *face = BUTTON_BLACK;
        *text = BUTTON_LIGHT_TEXT;
        return;
    case U_MENU_PLAY: case U_MENU_LEVELS: case U_MENU_GARDEN:
    case U_MENU_BOOK: case U_MENU_ONLINE: case U_MENU_WORKSHOP:
    case U_CUSTOM_WORKSHOP: case U_INTRO:
    case U_CREATE: case U_SEARCH_GO: case U_MATCH_BOOK: case U_MATCH_FINISH:
    case U_CUSTOM_CATALOG:
    case U_GARDEN_BOOK: case U_OFFLINE_MENU:
    case U_RESULT_NEXT: case U_RESULT_RETRY:
        *face = BUTTON_WHITE;
        return;
    default:
        break;
    }
    if (action >= U_LEVEL_BASE)
        *face = BUTTON_WHITE;
}

static void button_fill(lv_obj_t *o, lv_color_t color) {
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_color(o, color, LV_STATE_PRESSED);
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h,
                        const char *name, int size, int action) {
    lv_obj_t *o = lv_button_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    /* Action roles choose the neutral face and text colors centrally. */
    lv_color_t button_face, button_text;
    button_palette(action, &button_face, &button_text);
    lv_obj_set_style_bg_color(o, button_face, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 5, 0);
    lv_obj_set_style_border_width(o, 3, 0);
    lv_obj_set_style_border_color(o, C(000000), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, button_face, LV_STATE_PRESSED);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (name && name[0])
        label(o, 7, (h - font_sizes[size] - 3) / 2, w - 14, font_sizes[size] + 6,
              name, size, button_text, LV_TEXT_ALIGN_CENTER);
    if (action) lv_obj_add_event_cb(o, pressed, LV_EVENT_CLICKED,
                                     (void *)(intptr_t)action);
    return o;
}

static lv_obj_t *workshop_button(lv_obj_t *parent, int x, int y, int w, int h,
                                  const char *text, int size, int action,
                                  lv_color_t face) {
    lv_obj_t *o = button(parent, x, y, w, h, text, size, action);
    button_fill(o, face);
    return o;
}

static lv_obj_t *art(lv_obj_t *parent, int id, int cx, int cy, int scaled) {
    if (id < 0 || id >= PV_ART_COUNT || !pictures[id].data) return NULL;
    lv_obj_t *o = lv_image_create(parent);
    lv_image_set_src(o, &pictures[id]);
    lv_image_set_scale(o, (uint32_t)(256 * scaled / pictures[id].header.w));
    lv_obj_set_pos(o, cx - pictures[id].header.w / 2,
                   cy - pictures[id].header.h / 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
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
    box(root, 0, 0, GAME_W, 148, 0, back ? BUTTON_GRAY : DARK, 0);
    box(root, 0, 145, GAME_W, 4, 0, back ? BUTTON_BLACK : LIGHT_GRAY, 0);
    label(root, 70, 46, back ? 940 : 1120, 71, title, 4,
          back ? INK : WHITE, LV_TEXT_ALIGN_LEFT);
    if (back) button(root, 1065, 48, 175, 62, back, 2, action);
}

static void menu_screen(lv_obj_t *root) {
    box(root, 0, 0, GAME_W, 148, 0, DARK, 0);
    box(root, 0, 145, GAME_W, 4, 0, LIGHT_GRAY, 0);
    label(root, 48, 48, 470, 60, "Растения против гусей 3", 2,
          WHITE, LV_TEXT_ALIGN_LEFT);
    button(root, 520, 51, 200, 64, "Кампания", 2, U_MENU_LEVELS);
    button(root, 735, 51, 195, 64, "Сад Дзен", 2, U_MENU_GARDEN);
    button(root, 945, 51, 285, 64, "Уровни игроков", 1,
           U_CUSTOM_CATALOG);
    /* The characters stand together on the lawn, not in three square frames. */
    box(root, 55, 176, 1170, 355, 34, C(E5E5E5), 0);
    art(root, PV_ART_BREAD, 250, 322, 228);
    art(root, PV_ART_DIMA, 640, 324, 230);
    art(root, PV_ART_KIRILL, 1029, 322, 225);
    label(root, 93, 448, 310, 55, "Хлебушек", 2, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 482, 448, 310, 55, "Дима в маске", 2, INK, LV_TEXT_ALIGN_CENTER);
    label(root, 871, 448, 310, 55, "Кирилл", 2, INK, LV_TEXT_ALIGN_CENTER);
    button(root, 90, 563, 280, 94, "Умная книга", 2, U_MENU_BOOK);
    button(root, 410, 548, 445, 125, "Начать игру", 3, U_MENU_PLAY);
    button(root, 895, 563, 295, 94, "Играть вдвоём", 2, U_MENU_ONLINE);
}

static void levels_screen(lv_obj_t *root) {
    header(root, "Выбери уровень", "Назад", U_MENU_BACK);
    for (int n = 1; n <= 10; n++) {
        int col = (n - 1) % 5, row = (n - 1) / 5;
        int x = 84 + col * 225, y = 194 + row * 188;
        lv_obj_t *o = button(root, x, y, 206, 153, "", 2, U_LEVEL_BASE + n);
        button_fill(o, n == game_resume_level() ? BUTTON_GRAY : BUTTON_WHITE);
        lv_obj_set_style_border_width(o, n == game_resume_level() ? 5 : 3, 0);
        char num[30];
        snprintf(num, sizeof num, "%02d", n);
        label(o, 17, 13, 170, 62, num, 4, BUTTON_TEXT, LV_TEXT_ALIGN_LEFT);
        if (n == 5 || n == 10)
            label(o, 18, 91, 180, 48, n == 5 ? "Вода" : "Финал",
                  2, BUTTON_TEXT, LV_TEXT_ALIGN_LEFT);
    }
    button(root, 414, 590, 452, 67, "Уровень 0 · история", 2, U_INTRO);
    button(root, 895, 590, 315, 67, "Каталог уровней", 2,
           U_CUSTOM_CATALOG);
}

/* Offline navigation and HUD share the same real LVGL widgets as online. The
 * garden can switch palettes and show either authored map without altering its
 * stored plants/geese. */
static void garden_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    box(root, 0, 0, 1280, 121, 0, BUTTON_GRAY, 0);
    box(root, 0, 118, 1280, 3, 0, C(777777), 0);
    label(root, 12, 3, 232, 36, "Сад Дзен", 2, INK, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *plants = button(root, 6, 42, 112, 31, "Растения", 0, U_GARDEN_PLANTS);
    lv_obj_t *geese = button(root, 128, 42, 112, 31, "Гуси", 0, U_GARDEN_GEESE);
    lv_obj_t *lawn = button(root, 6, 78, 112, 31, "Газон", 0, U_GARDEN_LAWN);
    lv_obj_t *water = button(root, 128, 78, 112, 31, "Вода", 0, U_GARDEN_WATER);
    button_fill(plants, state.garden_mode == 0 ? BUTTON_GRAY : BUTTON_WHITE);
    button_fill(geese, state.garden_mode == 1 ? BUTTON_GRAY : BUTTON_WHITE);
    button_fill(lawn, state.garden_map == 1 ? BUTTON_GRAY : BUTTON_WHITE);
    button_fill(water, state.garden_map == 5 ? BUTTON_GRAY : BUTTON_WHITE);
    if (state.garden_mode == 0) lv_obj_set_style_border_width(plants, 5, 0);
    else lv_obj_set_style_border_width(geese, 5, 0);
    if (state.garden_map == 1) lv_obj_set_style_border_width(lawn, 5, 0);
    else lv_obj_set_style_border_width(water, 5, 0);

    int count = state.garden_mode ? 3 : 5;
    for (int i = 0; i < count; i++) {
        GameBookEntry entry;
        if (!game_book_entry(state.garden_mode, i, &entry)) continue;
        int x = 260 + (state.garden_mode ? 130 : 0) + i * 130;
        int active = state.garden_selection == i ||
                     (drag_phase == GAME_GARDEN && drag_index == i);
        lv_obj_t *slot = button(root, x, 15, 120, 100, "", 0, 0);
        button_fill(slot, active ? BUTTON_GRAY : BUTTON_WHITE);
        lv_obj_set_style_shadow_width(slot, 0, 0);
        if (active) lv_obj_set_style_border_width(slot, 5, 0);
        if (state.garden_mode)
            duck_art(slot, 60, 49, 85, entry.enemy_variant);
        else
            art(slot, entry.art_id, 60, 49, 85);
    }
    button(root, 918, 22, 145, 44, "Книга", 2, U_GARDEN_BOOK);
    button(root, 1079, 22, 179, 44, "В меню", 2, U_GARDEN_EXIT);
    lv_obj_t *erase = button(root, 918, 77, 340, 39, "Убрать", 2, U_GARDEN_ERASE);
    if (state.garden_selection == 5) lv_obj_set_style_border_width(erase, 5, 0);
}

static void book_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    header(root, "Умная книга", "Назад", U_BOOK_BACK);
    box(root, 63, 179, 513, 505, 22, PAPER, 1);
    box(root, 596, 179, 633, 505, 22, WHITE, 1);
    lv_obj_t *plants = button(root, 84, 194, 220, 54, "Растения", 2, U_BOOK_PLANTS);
    lv_obj_t *enemies = button(root, 314, 194, 226, 54, "Противники", 2, U_BOOK_ENEMIES);
    button_fill(plants, state.book_enemy_tab ? BUTTON_WHITE : BUTTON_GRAY);
    button_fill(enemies, state.book_enemy_tab ? BUTTON_GRAY : BUTTON_WHITE);
    lv_obj_set_style_border_width(state.book_enemy_tab ? enemies : plants, 5, 0);
    int count = state.book_enemy_tab ? 4 : 5;
    for (int i = 0; i < count; ++i) {
        GameBookEntry entry;
        if (!game_book_entry(state.book_enemy_tab, i, &entry)) continue;
        int y = 259 + i * 79;
        lv_obj_t *card = button(root, 83, y, 472, 68, "", 0, U_BOOK_ENTRY_BASE + i);
        button_fill(card, state.book_selection == i ? BUTTON_GRAY : BUTTON_WHITE);
        lv_obj_set_style_border_width(card, state.book_selection == i ? 5 : 3, 0);
        if (state.book_enemy_tab && entry.art_id == PV_ART_DUCK)
            duck_art(card, 42, 34, 50, entry.enemy_variant);
        else art(card, entry.art_id, 42, 34, 54);
        label(card, 81, 16, 371, 44, entry.short_name, 2,
              BUTTON_TEXT, LV_TEXT_ALIGN_LEFT);
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
    box(root, 616, 477, 594, 186, 23, C(E7E7E7), 0);
    label(root, 642, 494, 542, 77, entry.description, 2, INK,
          LV_TEXT_ALIGN_CENTER);
    label(root, 642, 575, 542, 78, entry.detail, 2, INK,
          LV_TEXT_ALIGN_CENTER);
}

static void intro_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    lv_obj_t *advance = button(root, 0, 121, 1280, 599, "", 0, U_INTRO_NEXT);
    lv_obj_set_style_bg_opa(advance, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(advance, 0, 0);
    box(root, 0, 0, 1280, 121, 0, BUTTON_GRAY, 0);
    box(root, 0, 118, 1280, 3, 0, BUTTON_BLACK, 0);
    label(root, 69, 40, 872, 57, "Уровень 0 · история Кирилла", 3,
          INK, LV_TEXT_ALIGN_LEFT);
    button(root, 1018, 43, 238, 66, "Пропустить", 2, U_INTRO_SKIP);
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
    lv_obj_t *slot = button(root, 12, y, 223, 100, "", 0, 0);
    button_fill(slot, dragging ? BUTTON_GRAY : BUTTON_WHITE);
    if (dragging) lv_obj_set_style_border_width(slot, 5, 0);
    lv_obj_set_style_shadow_width(slot, 0, 0);
    if (dragging) {
        lv_obj_t *ring = box(slot, 10, 5, 94, 94, 47, C(000000), 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 4, 0);
        lv_obj_set_style_border_color(ring, C(000000), 0);
    }
    if (duck_variant >= 0) duck_art(slot, 57, 51, 94, duck_variant);
    else art(slot, image_id, 57, 51, 94);
    if (waiting)
        label(slot, 95, 31, 128, 48, "Подождите", 1, BUTTON_TEXT,
              LV_TEXT_ALIGN_LEFT);
}

static void play_screen(lv_obj_t *root) {
    GameOfflineUIState state;
    game_offline_ui_snapshot(&state);
    box(root, 0, 0, 1280, 121, 0, DARK, 0);
    box(root, 0, 118, 1280, 3, 0, LIGHT_GRAY, 0);
    box(root, 0, 121, 246, 599, 0, BUTTON_GRAY, 0);
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
          2, drag_notice_left > 0 ? LIGHT_GRAY : C(D0D0D0), LV_TEXT_ALIGN_LEFT);
    button(root, 929, 43, 150, 64, "Книга", 2, U_OFFLINE_BOOK);
    button(root, 1094, 43, 165, 64, "В меню", 2, U_OFFLINE_MENU);
    for (int i = 0; i < 5; ++i) {
        if (i == 4 && state.level != 5) continue; /* no unused lily on dry levels */
        GameBookEntry entry;
        if (!game_book_entry(0, i, &entry)) continue;
        packet(root, 137 + i * 107, entry.art_id, -1,
               state.cooldown[i] > 0, drag_phase == GAME_PLAY && drag_index == i);
    }
}

static void result_screen(lv_obj_t *root, int phase) {
    lv_obj_t *veil = box(root, 0, 0, 1280, 720, 0, C(222222), 0);
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
        button(root, 392, 461, 496, 98, "Следующий уровень", 2, U_RESULT_NEXT);
        button(root, 480, 581, 320, 63, "В меню", 1, U_RESULT_MENU);
    } else if (phase == GAME_WIN)
        button(root, 425, 464, 430, 100, "В меню", 2, U_RESULT_MENU);
    else {
        button(root, 335, 460, 310, 104, "Повторить", 2, U_RESULT_RETRY);
        button(root, 660, 460, 315, 104, "В меню", 2, U_RESULT_MENU);
    }
}

/* Count/status is recomputed on each Firebase snapshot, never hard-coded. */
static void rooms_screen(lv_obj_t *root, const OnNetView *v) {
    header(root, "Играем вместе", "В меню", U_ROOMS_BACK);
    box(root, 43, 162, 1195, 89, 19, PAPER, 1);
    lv_obj_t *lawn = button(root, 64, 191, 207, 54, "Газон", 2, U_MAP_LAWN);
    lv_obj_t *water = button(root, 282, 191, 210, 54, "Вода", 2, U_MAP_WATER);
    button_fill(lawn, chosen_map == 1 ? BUTTON_GRAY : BUTTON_WHITE);
    button_fill(water, chosen_map == 5 ? BUTTON_GRAY : BUTTON_WHITE);
    lv_obj_set_style_border_width(chosen_map == 1 ? lawn : water, 5, 0);
    lv_obj_set_style_border_color(chosen_map == 1 ? lawn : water,
                                  C(000000), 0);
    button(root, 650, 179, 187, 62, "Поиск", 2, U_SEARCH);
    button(root, 856, 179, 208, 62, "+ Создать", 2, U_CREATE);
    button(root, 1070, 179, 149, 62, "Мастерская", 1, U_MENU_WORKSHOP);
    box(root, 43, 271, 1195, 421, 23, PAPER, 1);
    label(root, 70, 286, 660, 52, "Свободные комнаты", 3, INK,
          LV_TEXT_ALIGN_LEFT);
    button(root, 1027, 284, 193, 48, "Обновить", 2, U_REFRESH);
    box(root, 69, 341, 1142, 2, 0, C(BDBDBD), 0);
    if (v->room_count == 0) {
        art(root, PV_ART_PEA, 544, 417, 118);
        art(root, PV_ART_DUCK, 740, 418, 116);
        const char *title = v->notice[0] ? "Нет связи" :
                            !v->connected ? "Ищем комнаты..." :
                            "Здесь пока тихо";
        label(root, 285, 497, 710, 52, title, 3, INK, LV_TEXT_ALIGN_CENTER);
        if (v->notice[0])
            label(root, 253, 548, 774, 38, v->notice, 1,
                  MUTED, LV_TEXT_ALIGN_CENTER);
        button(root, 479, 587, 322, 66,
               v->notice[0] ? "Повторить поиск" : "+ Создать комнату", 2,
               v->notice[0] ? U_REFRESH : U_CREATE);
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
            lv_obj_t *card = button(root, x, y, width, 71, "", 1,
                                    U_ROOM_BASE + i);
            label(card, 29, 14, 217, 48, v->rooms[idx].id, 2, INK,
                  LV_TEXT_ALIGN_LEFT);
            label(card, width - 300, 19, 119, 42,
                  v->rooms[idx].map == 5 ? "Вода" : "Газон",
                  1, BUTTON_TEXT, LV_TEXT_ALIGN_CENTER);
            label(card, width - 174, 19, 158, 42, "Войти  >", 1,
                  BUTTON_TEXT, LV_TEXT_ALIGN_CENTER);
        }
        if (v->room_count > 8) {
            button(root, 77, 660, 168, 43, "Назад", 1, U_ROOM_BASE + 8);
            button(root, 1035, 660, 168, 43, "Дальше", 1,
                   U_ROOM_BASE + 9);
        }
    }
    if (!search_open) return;
    lv_obj_t *veil = box(root, 0, 0, 1280, 720, 0, C(222222), 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_80, 0);
    box(root, 216, 72, 848, 611, 26, PAPER, 1);
    label(root, 259, 111, 590, 58, "Найти комнату по коду", 3, INK,
          LV_TEXT_ALIGN_LEFT);

    button(root, 895, 111, 126, 51, "Назад", 1, U_SEARCH_CLOSE);
    size_t len = strlen(search_code);
    for (int i = 0; i < 6; ++i) {
        int x = 287 + i * 88;
        box(root, x, 207, 75, 64, 12, WHITE, 0);
        char letter[2] = {i < (int)len ? search_code[i] : ' ', 0};
        label(root, x, 216, 75, 51, letter, 3, INK, LV_TEXT_ALIGN_CENTER);
        if (i >= (int)len) box(root, x + 27, 260, 22, 2, 0, C(777777), 0);
    }
    button(root, 844, 207, 148, 64, "Стереть", 1, U_SEARCH_ERASE);
    const char *keys = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 32; ++i) {
        char ch[2] = {keys[i], 0};
        int x = 286 + (i % 9) * 78, y = 286 + (i / 9) * 67;
        if (i >= 27) x += 156; /* centre the last, shorter row */
        button(root, x, y, 68, 55, ch, 2, U_KEY_BASE + i);
    }
    if (local_notice[0])
        label(root, 331, 558, 620, 33, local_notice, 1,
              MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *join = button(root, 395, 599, 490, 64,
                            "Войти в комнату", 2, U_SEARCH_GO);
    if (len != 6) button_fill(join, BUTTON_GRAY);
}

static void custom_levels_screen(lv_obj_t *root, const OnNetView *v) {
    header(root, "Каталог уровней", "Назад", U_CUSTOM_BACK);
    box(root, 43, 165, 1195, 94, 20, PAPER, 1);
    label(root, 68, 179, 770, 34, "ПУБЛИЧНЫЙ КАТАЛОГ УРОВНЕЙ",
          1, MUTED, LV_TEXT_ALIGN_LEFT);
    label(root, 68, 213, 830, 33,
          v->levels_busy ? "Подключаемся к каталогу…" :
          v->levels_notice[0] ? v->levels_notice :
          v->level_count ? "Выбери уровень, чтобы запустить его в C-игре." :
                           "Пока нет опубликованных уровней.",
          2, v->levels_notice[0] ? MUTED : INK, LV_TEXT_ALIGN_LEFT);
    button(root, 824, 185, 177, 54, "Мастерская", 2,
           U_CUSTOM_WORKSHOP);
    lv_obj_t *refresh = button(root, 1015, 185, 193, 54, "", 2,
                                U_CUSTOM_REFRESH);
    label(refresh, 48, 4, 140, 46, "Обновить", 1, BUTTON_TEXT,
          LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(refresh, vector_refresh_draw, LV_EVENT_DRAW_MAIN, NULL);
    memset(visible_level_ids, 0, sizeof visible_level_ids);
    int pages = (v->level_count + 7) / 8;
    if (custom_level_page >= pages) custom_level_page = pages > 0 ? pages - 1 : 0;
    if (v->level_count > 0) {
        for (int i = 0; i < 8 && custom_level_page * 8 + i < v->level_count; i++) {
            int index = custom_level_page * 8 + i;
            int x = 65 + (i % 2) * 579;
            int y = 280 + (i / 2) * 91;
            snprintf(visible_level_ids[i], sizeof visible_level_ids[i], "%s",
                     v->levels[index].id);
            lv_obj_t *card = button(root, x, y, 548, 78, "", 1,
                                    U_CUSTOM_LEVEL_BASE + i);
            lv_obj_set_style_border_width(card, 3, 0);
            lv_obj_set_style_border_color(card, C(000000), 0);
            box(card, 13, 15, 90, 47, 10, BUTTON_GRAY, 0);
            label(card, 15, 17, 86, 43, v->levels[index].id, 2, BUTTON_TEXT,
                  LV_TEXT_ALIGN_CENTER);
            label(card, 119, 7, 410, 32, v->levels[index].title, 2, BUTTON_TEXT,
                  LV_TEXT_ALIGN_LEFT);
            label(card, 121, 39, 410, 33,
                  v->levels[index].description[0] ? v->levels[index].description :
                                                    "Нажми, чтобы играть в C-игре",
                  1, BUTTON_TEXT, LV_TEXT_ALIGN_LEFT);
        }
    } else if (!v->levels_busy) {
        box(root, 220, 322, 840, 190, 25, WHITE, 1);
        art(root, PV_ART_KIRILL, 312, 413, 104);
        label(root, 420, 365, 600, 74, "Уровней пока нет", 3, INK,
              LV_TEXT_ALIGN_LEFT);
        label(root, 422, 432, 576, 58,
              "Опубликованные уровни появятся здесь.",
              1, MUTED, LV_TEXT_ALIGN_LEFT);
    }
    if (pages > 1) {
        button(root, 69, 658, 170, 43, "‹ Назад", 1, U_CUSTOM_PREV);
        char page_text[40];
        snprintf(page_text, sizeof page_text, "%d / %d", custom_level_page + 1, pages);
        label(root, 514, 659, 252, 40, page_text, 1, MUTED, LV_TEXT_ALIGN_CENTER);
        button(root, 1040, 658, 170, 43, "Дальше ›", 1, U_CUSTOM_NEXT);
    }
}

static void workshop_background(lv_obj_t *root, const char *title) {
    box(root, 0, 0, GAME_W, GAME_H, 0, WS_BACKGROUND, 0);
    for (int x = 0; x < GAME_W; x += 64)
        box(root, x, 0, 1, GAME_H, 0, WS_GRID, 0);
    for (int y = 0; y < GAME_H; y += 64)
        box(root, 0, y, GAME_W, 1, 0, WS_GRID, 0);
    box(root, 38, 22, 1204, 676, 16, WS_BROWN, 0);
    box(root, 47, 31, 1186, 85, 12, WS_BROWN_DARK, 0);
    label(root, 77, 47, 890, 55, title, 3, WS_CREAM,
          LV_TEXT_ALIGN_LEFT);
    button(root, 1052, 44, 158, 60, "Назад", 2, U_WORKSHOP_BACK);
}

static void workshop_reset_draft(void) {
    memset(workshop_objects, 0, sizeof workshop_objects);
    memset(workshop_clipboard, 0, sizeof workshop_clipboard);
    memset(workshop_selected_flags, 0, sizeof workshop_selected_flags);
    workshop_object_count = 0;
    workshop_clipboard_count = 0;
    workshop_selected = -1;
    workshop_player_selected = workshop_goal_selected = workshop_ground_selected = 0;
    workshop_notice[0] = 0;
    workshop_tool = WS_TOOL_BUILD;
    workshop_palette_type = ON_LEVEL_BLOCK;
    workshop_block_variant = ON_LEVEL_BLOCK;
    workshop_trigger_kind = ON_TRIGGER_KIND_MOVE;
    workshop_camera_x = workshop_camera_y = 0;
    workshop_ground_x = 0;workshop_ground_y = 8;
    workshop_ground_w = 16;workshop_ground_h = 2;workshop_ground_angle = 0;
    workshop_player_x = 1;workshop_player_y = 7;
    workshop_player_w = .65f;workshop_player_h = .85f;workshop_player_angle = 0;
    workshop_goal_x = 14;workshop_goal_y = 6;
    workshop_goal_w = 1;workshop_goal_h = 2;workshop_goal_angle = 0;
    workshop_dialog = WS_DIALOG_NONE;
    workshop_keyboard_active = 0;workshop_input_kind = WS_INPUT_NONE;
    workshop_input_return_dialog = WS_DIALOG_NONE;
    workshop_active_textarea = NULL;
    workshop_move_x = 1;workshop_move_y = 0;
    workshop_move_time = 1;
    workshop_easing = workshop_group_id = workshop_layer = 0;
    workshop_next_group_id = 3;
    workshop_layer2 = workshop_z_order = 0;
    workshop_lock_x = workshop_lock_y = workshop_spawn = workshop_silent = 0;
    workshop_touch = 1;
    workshop_target_group = 2;
    workshop_color_set = workshop_color_index = 0;
    workshop_draft_exists = 0;
    workshop_name[0] = 0;workshop_description[0] = 0;
    snprintf(workshop_name, sizeof workshop_name, "%s", "Новый уровень");
}

static void workshop_home_screen(lv_obj_t *root) {
    workshop_background(root, "Мастерская · мои уровни");
    box(root, 104, 138, 1072, 385, 14, WS_BROWN_DARK, 0);
    label(root, 139, 154, 900, 44, "ЛОКАЛЬНЫЕ ЧЕРНОВИКИ", 2,
          WS_CREAM, LV_TEXT_ALIGN_LEFT);
    if (workshop_draft_exists) {
        lv_obj_t *row = workshop_button(root, 130, 211, 1018, 134, "", 0,
                                         U_WORKSHOP_OPEN_DRAFT, WS_BROWN_LIGHT);
        label(row, 26, 18, 655, 44, workshop_name, 3, WS_CREAM,
              LV_TEXT_ALIGN_LEFT);
        label(row, 28, 69, 640, 37,
              "Платформенный уровень · локальный черновик", 1,
              WS_CREAM, LV_TEXT_ALIGN_LEFT);
        workshop_button(row, 753, 34, 224, 64, "Открыть", 2,
                        U_WORKSHOP_OPEN_DRAFT, WS_GREEN);
    } else {
        box(root, 130, 211, 1018, 134, 12, WS_BROWN_LIGHT, 0);
        label(root, 170, 246, 930, 43, "Черновиков пока нет", 3,
              WS_CREAM, LV_TEXT_ALIGN_CENTER);
        label(root, 170, 293, 930, 32,
              "Создай уровень или открой опубликованный каталог.", 1,
              WS_CREAM, LV_TEXT_ALIGN_CENTER);
    }
    box(root, 130, 371, 1018, 118, 12, WS_BROWN_LIGHT, 0);
    label(root, 157, 389, 600, 39, "Отдельный платформенный редактор", 2,
          WS_CREAM, LV_TEXT_ALIGN_LEFT);
    label(root, 158, 432, 710, 37,
          "Строй сцену, настраивай объекты, группы, цвет и движение.",
          1, WS_CREAM, LV_TEXT_ALIGN_LEFT);
    workshop_button(root, 130, 558, 360, 82, "+  Новый уровень", 2,
                    U_WORKSHOP_NEW, WS_GREEN);
    workshop_button(root, 820, 558, 328, 82, "Опубликованный каталог", 1,
                    U_WORKSHOP_CATALOG, WS_CYAN);
}

static void workshop_draw_keyboard_dialog(lv_obj_t *root);

static void workshop_details_screen(lv_obj_t *root) {
    workshop_background(root, "Параметры уровня");
    box(root, 132, 142, 1016, 370, 15, WS_BROWN_DARK, 0);
    label(root, 166, 166, 350, 40, "НАЗВАНИЕ", 1, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    workshop_button(root, 163, 207, 950, 74, workshop_name, 2,
                    U_WORKSHOP_EDIT_TITLE, WS_DARK_VALUE);
    label(root, 166, 302, 350, 36, "ОПИСАНИЕ", 1, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    const char *description_preview = workshop_description[0] ?
        workshop_description : "Нажми, чтобы добавить описание уровня.";
    workshop_button(root, 163, 339, 950, 112, description_preview, 1,
                    U_WORKSHOP_EDIT_DESCRIPTION, WS_DARK_VALUE);
    label(root, 166, 463, 680, 31,
          workshop_draft_exists ?
          "Сохранённый черновик · публичный каталог отдельно" :
          "Новый черновик · публичный каталог отдельно",
          0, WS_CREAM, LV_TEXT_ALIGN_LEFT);
    workshop_button(root, 780, 561, 368, 80, "Открыть редактор", 2,
                    U_WORKSHOP_DETAIL_EDIT, WS_GREEN);
    if (workshop_keyboard_active) workshop_draw_keyboard_dialog(root);
}

static float workshop_object_width(int type) {
    switch (type) {
    case ON_LEVEL_GROUND: return 2.0f;
    case ON_LEVEL_COIN: return .55f;
    case ON_LEVEL_ENEMY: return .8f;
    case ON_LEVEL_PLAYER: return .65f;
    default: return 1.0f;
    }
}
static float workshop_object_height(int type) {
    switch (type) {
    case ON_LEVEL_GROUND: return 1.0f;
    case ON_LEVEL_COIN: return .55f;
    case ON_LEVEL_ENEMY: return .8f;
    case ON_LEVEL_PLAYER: return .85f;
    case ON_LEVEL_GOAL: return 2.0f;
    default: return 1.0f;
    }
}
static float workshop_world_coord(float value, float size) {
    if (value < -WS_WORLD_LIMIT) return -(float)WS_WORLD_LIMIT;
    if (value > WS_WORLD_LIMIT - size) return WS_WORLD_LIMIT - size;
    return value;
}
static int workshop_point_in_object(float px, float py, float x, float y,
                                    float w, float h, float angle, int type) {
    float radians = angle * 0.01745329251994329577f;
    float dx = px - (x + w * .5f), dy = py - (y + h * .5f);
    float local_x = cosf(radians) * dx + sinf(radians) * dy;
    float local_y = -sinf(radians) * dx + cosf(radians) * dy;
    if (fabsf(local_x) > w * .5f || fabsf(local_y) > h * .5f) return 0;
    if (type != ON_LEVEL_SLOPE) return 1;
    float nx = local_x / w + .5f, ny = local_y / h + .5f;
    const float ax = 0.0f, ay = 1.0f, bx = 1.0f, by = 0.0f;
    const float cx = 1.0f, cy = 1.0f;
    float d1 = (nx - bx) * (ay - by) - (ax - bx) * (ny - by);
    float d2 = (nx - cx) * (by - cy) - (bx - cx) * (ny - cy);
    float d3 = (nx - ax) * (cy - ay) - (cx - ax) * (ny - ay);
    int has_negative = d1 < -1e-6f || d2 < -1e-6f || d3 < -1e-6f;
    int has_positive = d1 > 1e-6f || d2 > 1e-6f || d3 > 1e-6f;
    return !(has_negative && has_positive);
}
static int workshop_find_cell(int col, int row) {
    float px = col + .5f, py = row + .5f;
    for (int i = workshop_object_count - 1; i >= 0; --i) {
        const WorkshopObject *o = &workshop_objects[i];
        if (workshop_point_in_object(px, py, o->x, o->y, o->w, o->h,
                                     o->angle, o->type))
            return i;
    }
    return -1;
}
static int workshop_selection_at(int col, int row) {
    float px = col + .5f, py = row + .5f;
    int found = workshop_find_cell(col, row);
    if (found >= 0) return found;
    if (workshop_point_in_object(px, py, workshop_player_x, workshop_player_y,
            workshop_player_w, workshop_player_h, workshop_player_angle,
            ON_LEVEL_PLAYER)) return -2;
    if (workshop_point_in_object(px, py, workshop_goal_x, workshop_goal_y,
            workshop_goal_w, workshop_goal_h, workshop_goal_angle,
            ON_LEVEL_GOAL)) return -3;
    if (workshop_point_in_object(px, py, workshop_ground_x, workshop_ground_y,
            workshop_ground_w, workshop_ground_h, workshop_ground_angle,
            ON_LEVEL_GROUND)) return -4;
    return -1;
}
static int workshop_is_selected(int object) {
    if (object == -2) return workshop_player_selected;
    if (object == -3) return workshop_goal_selected;
    if (object == -4) return workshop_ground_selected;
    return object >= 0 && object < workshop_object_count &&
           workshop_selected_flags[object] != 0;
}
static int workshop_selection_count(void) {
    int count = workshop_player_selected + workshop_goal_selected +
                workshop_ground_selected;
    for (int i = 0; i < workshop_object_count; ++i)
        count += workshop_selected_flags[i] != 0;
    return count;
}
static void workshop_clear_selection(void) {
    memset(workshop_selected_flags, 0, sizeof workshop_selected_flags);
    workshop_player_selected = workshop_goal_selected = workshop_ground_selected = 0;
    workshop_selected = -1;
}
static void workshop_select_only(int object) {
    workshop_clear_selection();
    if (object == -2) workshop_player_selected = 1;
    else if (object == -3) workshop_goal_selected = 1;
    else if (object == -4) workshop_ground_selected = 1;
    else if (object >= 0 && object < workshop_object_count)
        workshop_selected_flags[object] = 1;
    else object = -1;
    workshop_selected = object;
}
static int workshop_first_selected(void) {
    if (workshop_player_selected) return -2;
    if (workshop_goal_selected) return -3;
    if (workshop_ground_selected) return -4;
    for (int i = workshop_object_count - 1; i >= 0; --i)
        if (workshop_selected_flags[i]) return i;
    return -1;
}
static void workshop_toggle_selection(int object) {
    if (object == -1) return;
    int selected = workshop_is_selected(object);
    if (object == -2) workshop_player_selected = !selected;
    else if (object == -3) workshop_goal_selected = !selected;
    else if (object == -4) workshop_ground_selected = !selected;
    else if (object >= 0 && object < workshop_object_count)
        workshop_selected_flags[object] = !selected;
    if (selected) {
        if (workshop_selected == object) workshop_selected = workshop_first_selected();
    } else workshop_selected = object;
}
static void workshop_remove_object(int index) {
    if (index < 0 || index >= workshop_object_count) return;
    if (workshop_selected == index) workshop_selected = -1;
    else if (workshop_selected > index) workshop_selected--;
    memmove(&workshop_objects[index], &workshop_objects[index + 1],
            (size_t)(workshop_object_count - index - 1) * sizeof workshop_objects[0]);
    memmove(&workshop_selected_flags[index], &workshop_selected_flags[index + 1],
            (size_t)(workshop_object_count - index - 1) * sizeof workshop_selected_flags[0]);
    --workshop_object_count;
    memset(&workshop_objects[workshop_object_count], 0, sizeof workshop_objects[0]);
    workshop_selected_flags[workshop_object_count] = 0;
    if (workshop_selected < 0 || !workshop_is_selected(workshop_selected))
        workshop_selected = workshop_first_selected();
}
static void workshop_load_selected_properties(int object) {
    if (object >= 0 && object < workshop_object_count) {
        workshop_group_id = workshop_objects[object].group_id;
        workshop_layer = workshop_objects[object].layer;
        workshop_layer2 = workshop_objects[object].layer2;
        workshop_z_order = workshop_objects[object].z_order;
        workshop_color_set = workshop_objects[object].color_set;
        workshop_color_index = workshop_objects[object].color_index;
        if (workshop_objects[object].type == ON_LEVEL_TRIGGER)
            workshop_trigger_kind = workshop_objects[object].trigger_kind;
    } else if (object == -2) workshop_group_id = 1;
    else if (object == -3) workshop_group_id = 2;
    else if (object == -4) workshop_group_id = 3;
}

static void workshop_place_object(int type, int col, int row) {
    if (type == ON_LEVEL_PLAYER) {
        workshop_player_x = workshop_world_coord(col, workshop_player_w);
        workshop_player_y = workshop_world_coord(row, workshop_player_h);
        workshop_select_only(-2);
        return;
    }
    if (type == ON_LEVEL_GOAL) {
        workshop_goal_x = workshop_world_coord(col, workshop_goal_w);
        workshop_goal_y = workshop_world_coord(row, workshop_goal_h);
        workshop_select_only(-3);
        return;
    }
    int found = workshop_find_cell(col, row);
    if (found >= 0) {
        workshop_select_only(found);
        workshop_load_selected_properties(found);
        return;
    }
    if (workshop_object_count >= WS_OBJECT_CAP) return;
    int index = workshop_object_count;
    WorkshopObject *o = &workshop_objects[index];
    *o = (WorkshopObject){0};
    o->type = type;
    o->x = workshop_world_coord((float)col, workshop_object_width(type));
    o->y = workshop_world_coord((float)row, workshop_object_height(type));
    o->w = workshop_object_width(type);o->h = workshop_object_height(type);
    o->group_id = ++workshop_next_group_id;
    o->layer = workshop_layer;
    o->layer2 = workshop_layer2;
    o->z_order = workshop_z_order;
    o->color_set = workshop_color_set;
    o->color_index = workshop_color_index;
    o->trigger_event = ON_TRIGGER_TOUCH;
    o->trigger_group_id = 2; /* goal group */
    o->trigger_x = 1;o->trigger_y = 0;o->trigger_duration = 3;
    workshop_set_trigger_kind(o, workshop_trigger_kind);
    workshop_object_count++;
    workshop_select_only(index);
    workshop_load_selected_properties(index);
}

static void workshop_cell_tap(int cell) {
    if (cell < 0 || cell >= WS_GRID_COLS * WS_GRID_ROWS) return;
    int col = cell % WS_GRID_COLS + workshop_camera_x;
    int row = cell / WS_GRID_COLS + workshop_camera_y;
    int found = workshop_selection_at(col, row);
    if (workshop_tool == WS_TOOL_BUILD) {
        int type = workshop_palette_type == ON_LEVEL_BLOCK ?
                   workshop_block_variant : workshop_palette_type;
        workshop_place_object(type, col, row);
    } else if (workshop_tool == WS_TOOL_EDIT) {
        workshop_select_only(found);
        workshop_load_selected_properties(found);
    } else if (workshop_tool == WS_TOOL_MULTI) {
        workshop_toggle_selection(found);
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count)
            workshop_load_selected_properties(workshop_selected);
    } else if (found >= 0) {
        workshop_remove_object(found);
    }
}

static int workshop_geometry_refs(int object, float **x, float **y,
                                  float **w, float **h, float **angle) {
    if (object == -2) {
        *x = &workshop_player_x;*y = &workshop_player_y;
        *w = &workshop_player_w;*h = &workshop_player_h;
        *angle = &workshop_player_angle;return 1;
    }
    if (object == -3) {
        *x = &workshop_goal_x;*y = &workshop_goal_y;
        *w = &workshop_goal_w;*h = &workshop_goal_h;
        *angle = &workshop_goal_angle;return 1;
    }
    if (object == -4) {
        *x = &workshop_ground_x;*y = &workshop_ground_y;
        *w = &workshop_ground_w;*h = &workshop_ground_h;
        *angle = &workshop_ground_angle;return 1;
    }
    if (object >= 0 && object < workshop_object_count) {
        WorkshopObject *o = &workshop_objects[object];
        *x = &o->x;*y = &o->y;*w = &o->w;*h = &o->h;*angle = &o->angle;
        return 1;
    }
    return 0;
}
static void workshop_rotate_selected(float degrees);
static lv_obj_t *workshop_rotation_button(lv_obj_t *parent, int x, int y,
                                           int w, int h, int action,
                                           lv_color_t face, int direction,
                                           const char *caption, int caption_size);
static int workshop_primary_geometry(float **x, float **y, float **w,
                                     float **h, float **angle) {
    return workshop_selection_count() > 0 &&
           workshop_geometry_refs(workshop_selected, x, y, w, h, angle);
}
static void workshop_scale_id(int id, int width_axis, float factor) {
    float *x, *y, *w, *h, *angle;
    if (!workshop_geometry_refs(id, &x, &y, &w, &h, &angle)) return;
    (void)angle;
    float *size = width_axis ? w : h;
    float maximum = width_axis ? WS_OBJECT_MAX_WIDTH : WS_OBJECT_MAX_HEIGHT;
    float old_size = *size;
    float new_size = fmaxf(WS_OBJECT_MIN_SIZE,
                           fminf(maximum, old_size * factor));
    *size = new_size;
    if (width_axis) *x = workshop_world_coord(*x - (new_size - old_size) * .5f,
                                              new_size);
    else *y = workshop_world_coord(*y - (new_size - old_size) * .5f,
                                   new_size);
}
static void workshop_set_selected_size(int width_axis, float size) {
    float *x, *y, *w, *h, *angle;
    if (!workshop_primary_geometry(&x, &y, &w, &h, &angle)) return;
    (void)x;(void)y;(void)angle;
    float maximum = width_axis ? WS_OBJECT_MAX_WIDTH : WS_OBJECT_MAX_HEIGHT;
    float current = width_axis ? *w : *h;
    size = fmaxf(WS_OBJECT_MIN_SIZE, fminf(maximum, size));
    if (current <= 0) return;
    float factor = size / current;
    const int builtins[] = {-2, -3, -4};
    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; ++i)
        if (workshop_is_selected(builtins[i]))
            workshop_scale_id(builtins[i], width_axis, factor);
    for (int i = 0; i < workshop_object_count; ++i)
        if (workshop_selected_flags[i])
            workshop_scale_id(i, width_axis, factor);
}
static void workshop_set_selected_angle(float degrees) {
    float *x, *y, *w, *h, *angle;
    if (!workshop_primary_geometry(&x, &y, &w, &h, &angle)) return;
    (void)x;(void)y;(void)w;(void)h;
    workshop_rotate_selected(degrees - *angle);
}

static void workshop_move_selected(float dx, float dy) {
    const int builtins[] = {-2, -3, -4};
    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; ++i) {
        int id = builtins[i];
        if (!workshop_is_selected(id)) continue;
        float *x, *y, *w, *h, *angle;
        if (!workshop_geometry_refs(id, &x, &y, &w, &h, &angle)) continue;
        (void)angle;*x = workshop_world_coord(*x + dx, *w);
        *y = workshop_world_coord(*y + dy, *h);
    }
    for (int i = 0; i < workshop_object_count; ++i) {
        if (!workshop_selected_flags[i]) continue;
        WorkshopObject *o = &workshop_objects[i];
        o->x = workshop_world_coord(o->x + dx, o->w);
        o->y = workshop_world_coord(o->y + dy, o->h);
    }
}
static void workshop_resize_selected(int width_axis, float amount) {
    const int builtins[] = {-2, -3, -4};
    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; ++i) {
        int id = builtins[i];
        if (!workshop_is_selected(id)) continue;
        float *x, *y, *w, *h, *angle;
        if (!workshop_geometry_refs(id, &x, &y, &w, &h, &angle)) continue;
        (void)angle;
        float *size = width_axis ? w : h;
        float maximum = width_axis ? WS_OBJECT_MAX_WIDTH : WS_OBJECT_MAX_HEIGHT;
        *size = fmaxf(WS_OBJECT_MIN_SIZE, fminf(maximum, *size + amount));
        if (width_axis) *x = workshop_world_coord(*x, *size);
        else *y = workshop_world_coord(*y, *size);
    }
    for (int i = 0; i < workshop_object_count; ++i) {
        if (!workshop_selected_flags[i]) continue;
        WorkshopObject *o = &workshop_objects[i];
        float *size = width_axis ? &o->w : &o->h;
        float maximum = width_axis ? WS_OBJECT_MAX_WIDTH : WS_OBJECT_MAX_HEIGHT;
        *size = fmaxf(WS_OBJECT_MIN_SIZE, fminf(maximum, *size + amount));
        if (width_axis) o->x = workshop_world_coord(o->x, *size);
        else o->y = workshop_world_coord(o->y, *size);
    }
}
static void workshop_rotate_selected(float degrees) {
    const int builtins[] = {-2, -3, -4};
    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; ++i) {
        int id = builtins[i];
        if (!workshop_is_selected(id)) continue;
        float *x, *y, *w, *h, *angle;
        if (!workshop_geometry_refs(id, &x, &y, &w, &h, &angle)) continue;
        (void)x;(void)y;(void)w;(void)h;
        *angle = fmodf(*angle + degrees, 360.0f);
        if (*angle < 0) *angle += 360.0f;
    }
    for (int i = 0; i < workshop_object_count; ++i) {
        if (!workshop_selected_flags[i]) continue;
        WorkshopObject *o = &workshop_objects[i];
        o->angle = fmodf(o->angle + degrees, 360.0f);
        if (o->angle < 0) o->angle += 360.0f;
    }
}
static void workshop_copy_selection(void) {
    workshop_clipboard_count = 0;
    for (int i = 0; i < workshop_object_count &&
                    workshop_clipboard_count < WS_OBJECT_CAP; ++i) {
        if (workshop_selected_flags[i])
            workshop_clipboard[workshop_clipboard_count++] = workshop_objects[i];
    }
    if (workshop_ground_selected && workshop_clipboard_count < WS_OBJECT_CAP) {
        workshop_clipboard[workshop_clipboard_count++] = (WorkshopObject){
            .type=ON_LEVEL_GROUND,
            .x=workshop_ground_x,.y=workshop_ground_y,
            .w=workshop_ground_w,.h=workshop_ground_h,
            .angle=workshop_ground_angle,.group_id=3,
            .layer=workshop_layer,.layer2=workshop_layer2,
            .z_order=workshop_z_order,.color_set=workshop_color_set,
            .color_index=workshop_color_index,.trigger_kind=workshop_trigger_kind
        };
    }
    if (workshop_clipboard_count)
        snprintf(workshop_notice, sizeof workshop_notice,
                 "Скопировано объектов: %d.", workshop_clipboard_count);
    else
        snprintf(workshop_notice, sizeof workshop_notice, "%s",
                 "Выбери объекты для копирования.");
}
static void workshop_paste_selection(void) {
    if (!workshop_clipboard_count) {
        snprintf(workshop_notice, sizeof workshop_notice, "%s",
                 "Сначала скопируй выбранные объекты.");return;
    }
    if (workshop_object_count + workshop_clipboard_count > WS_OBJECT_CAP) {
        snprintf(workshop_notice, sizeof workshop_notice, "%s",
                 "Нет места для вставки: достигнут лимит объектов.");return;
    }
    workshop_clear_selection();
    for (int i = 0; i < workshop_clipboard_count; ++i) {
        int index = workshop_object_count++;
        workshop_objects[index] = workshop_clipboard[i];
        WorkshopObject *o = &workshop_objects[index];
        o->x = workshop_world_coord(o->x + 1.0f, o->w);
        o->y = workshop_world_coord(o->y + 1.0f, o->h);
        workshop_selected_flags[index] = 1;
        workshop_selected = index;
    }
    snprintf(workshop_notice, sizeof workshop_notice,
             "Вставлено объектов: %d.", workshop_clipboard_count);
}
static void workshop_delete_selected(void) {
    int removed = 0;
    for (int i = workshop_object_count - 1; i >= 0; --i) {
        if (!workshop_selected_flags[i]) continue;
        workshop_remove_object(i);removed++;
    }
    if (removed)
        snprintf(workshop_notice, sizeof workshop_notice,
                 "Удалены выбранные объекты: %d.", removed);
    else
        snprintf(workshop_notice, sizeof workshop_notice, "%s",
                 "Уникальные элементы уровня обязательны.");
}

static const char *workshop_object_name(int type) {
    switch (type) {
    case ON_LEVEL_BLOCK: return "Блок";
    case ON_LEVEL_GROUND: return "Платформа";
    case ON_LEVEL_HAZARD: return "Шипы";
    case ON_LEVEL_SLOPE: return "Склон";
    case ON_LEVEL_COIN: return "Монета";
    case ON_LEVEL_ENEMY: return "Гусь";
    case ON_LEVEL_PLAYER: return "Игрок";
    case ON_LEVEL_GOAL: return "Финиш";
    default: return "Триггер";
    }
}
static const char *workshop_trigger_name(int kind) {
    return kind == ON_TRIGGER_KIND_ROTATE ? "Разворот" :
           kind == ON_TRIGGER_KIND_FOREVER ? "Вечно" :
           kind == ON_TRIGGER_KIND_INVISIBILITY ? "Невидимость" :
           kind == ON_TRIGGER_KIND_NO_COLLISION ? "Нет столкновения" : "Движение";
}
static int workshop_trigger_action_for_kind(int kind) {
    return kind == ON_TRIGGER_KIND_ROTATE ? ON_TRIGGER_ROTATE :
           kind == ON_TRIGGER_KIND_FOREVER ? ON_TRIGGER_ACTIVATE :
           kind == ON_TRIGGER_KIND_INVISIBILITY ? ON_TRIGGER_INVISIBLE :
           kind == ON_TRIGGER_KIND_NO_COLLISION ? ON_TRIGGER_NO_COLLISION :
           ON_TRIGGER_MOVE;
}
static void workshop_set_trigger_kind(WorkshopObject *object, int kind) {
    if (!object || kind < ON_TRIGGER_KIND_MOVE ||
        kind > ON_TRIGGER_KIND_NO_COLLISION) return;
    object->trigger_kind = kind;
    object->trigger_action = workshop_trigger_action_for_kind(kind);
    if (kind == ON_TRIGGER_KIND_ROTATE && object->trigger_duration < 1)
        object->trigger_duration = 3;
}

static int workshop_object_art(int type, int trigger_kind) {
    switch (type) {
    case ON_LEVEL_BLOCK: return PV_ART_LEVEL_BLOCK;
    case ON_LEVEL_GROUND: return PV_ART_LEVEL_PLATFORM;
    case ON_LEVEL_HAZARD: return PV_ART_LEVEL_SPIKE;
    case ON_LEVEL_SLOPE: return PV_ART_LEVEL_SLOPE;
    case ON_LEVEL_COIN: return PV_ART_COIN;
    case ON_LEVEL_ENEMY: return PV_ART_DUCK;
    case ON_LEVEL_PLAYER: return PV_ART_BREAD;
    case ON_LEVEL_GOAL: return PV_ART_LEVEL_FLAG;
    case ON_LEVEL_TRIGGER:
        return trigger_kind == ON_TRIGGER_KIND_ROTATE ? PV_ART_LEVEL_TRIGGER_ROTATE :
               trigger_kind == ON_TRIGGER_KIND_FOREVER ? PV_ART_LEVEL_TRIGGER_FOREVER :
               trigger_kind == ON_TRIGGER_KIND_INVISIBILITY ? PV_ART_LEVEL_TRIGGER_INVISIBILITY :
               trigger_kind == ON_TRIGGER_KIND_NO_COLLISION ? PV_ART_LEVEL_TRIGGER_NO_COLLISION :
               PV_ART_LEVEL_TRIGGER;
    default: return -1;
    }
}

static int workshop_target_object_id(int group_id) {
    if (group_id <= 0 || group_id == 2) return 3; /* built-in finish */
    if (group_id == 1) return 2; /* built-in player */
    if (group_id == 3) return 1; /* built-in ground */
    for (int i = 0; i < workshop_object_count; ++i)
        if (workshop_objects[i].group_id == group_id) return 4 + i;
    return 0;
}
static void workshop_build_preview(OnPublishedLevel *level) {
    memset(level, 0, sizeof *level);
    snprintf(level->id, sizeof level->id, "%s", "1"); /* replaced by publisher */
    snprintf(level->title, sizeof level->title, "%s", workshop_name);
    snprintf(level->description, sizeof level->description, "%s",
             workshop_description);
    level->width = 16;level->height = 10;
    OnLevelObject *o = &level->objects[level->object_count++];
    *o = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
                         .x=workshop_ground_x,.y=workshop_ground_y,
                         .w=workshop_ground_w,.h=workshop_ground_h,
                         .angle=workshop_ground_angle,
                         .color=0x65a845u,.number=3,.visible=1};
    snprintf(o->name, sizeof o->name, "%s", "Платформа");
    o = &level->objects[level->object_count++];
    *o = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
                         .x=workshop_player_x,.y=workshop_player_y,
                         .w=workshop_player_w,.h=workshop_player_h,
                         .angle=workshop_player_angle,
                         .color=0x5ab7e8u,.number=1,.visible=1};
    snprintf(o->name, sizeof o->name, "%s", "Игрок");
    o = &level->objects[level->object_count++];
    *o = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
                         .x=workshop_goal_x,.y=workshop_goal_y,
                         .w=workshop_goal_w,.h=workshop_goal_h,
                         .angle=workshop_goal_angle,
                         .color=0x69d16cu,.number=2,.visible=1};
    snprintf(o->name, sizeof o->name, "%s", "Финиш");
    for (int i = 0; i < workshop_object_count &&
                    level->object_count < ON_LEVEL_OBJECT_CAP; ++i) {
        const WorkshopObject *src = &workshop_objects[i];
        OnLevelObject *dst = &level->objects[level->object_count++];
        *dst = (OnLevelObject){
            .id=4+i,
            .type=src->type,
            .x=src->x, .y=src->y, .w=src->w, .h=src->h, .angle=src->angle,
            .color=workshop_colors[src->color_set % 4][src->color_index % 8],
            .number=src->group_id, .visible=1,
            .trigger_kind=src->trigger_kind,
            .trigger_event=src->trigger_event,
            .trigger_action=src->trigger_action,
            .target_id=workshop_target_object_id(src->trigger_group_id),
            .trigger_value=src->trigger_kind == ON_TRIGGER_KIND_ROTATE ?
                           0.0f : (float)src->trigger_x,
            .trigger_value_y=src->trigger_kind == ON_TRIGGER_KIND_MOVE ?
                             (float)src->trigger_y : 0.0f,
            .trigger_color=workshop_colors[workshop_color_set % 4][workshop_color_index % 8],
            .trigger_group_id=src->trigger_group_id,
            .trigger_has_group=src->type == ON_LEVEL_TRIGGER,
            .trigger_duration=src->trigger_duration,
            .trigger_has_duration=src->type == ON_LEVEL_TRIGGER &&
                                  src->trigger_kind == ON_TRIGGER_KIND_ROTATE
        };
        snprintf(dst->name, sizeof dst->name, "%s", workshop_object_name(src->type));
    }
}

static void workshop_dialog_shade(lv_obj_t *root) {
    lv_obj_t *shade = box(root, 0, 0, GAME_W, GAME_H, 0, C(10131D), 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_80, 0);
    lv_obj_set_style_border_width(shade, 0, 0);
    lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);
}

static void workshop_draw_group_dialog(lv_obj_t *root) {
    workshop_dialog_shade(root);
    box(root, 164, 20, 952, 680, 17, WS_BROWN, 0);
    box(root, 174, 30, 932, 76, 12, WS_BROWN_DARK, 0);
    label(root, 205, 44, 850, 49, "Группы, слои и порядок", 3,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);
    const char *names[] = {"ID группы", "Слой L", "Слой L2", "Z-порядок"};
    int values[] = {workshop_group_id, workshop_layer, workshop_layer2,
                    workshop_z_order};
    int dec[] = {U_WORKSHOP_GROUP_DEC, U_WORKSHOP_LAYER_DEC,
                 U_WORKSHOP_LAYER2_DEC, U_WORKSHOP_Z_DEC};
    int inc[] = {U_WORKSHOP_GROUP_INC, U_WORKSHOP_LAYER_INC,
                 U_WORKSHOP_LAYER2_INC, U_WORKSHOP_Z_INC};
    int tops[] = {132, 218, 304, 390};
    for (int i = 0; i < 4; ++i) {
        label(root, 218, tops[i] + 7, 232, 44, names[i], 2,
              WS_CREAM, LV_TEXT_ALIGN_LEFT);
        workshop_button(root, 481, tops[i], 58, 54, "−", 2, dec[i], BUTTON_GRAY);
        char text[24];snprintf(text, sizeof text, "%d", values[i]);
        box(root, 547, tops[i], 136, 54, 8, WS_DARK_VALUE, 0);
        label(root, 550, tops[i] + 6, 130, 42, text, 2,
              WS_CREAM, LV_TEXT_ALIGN_CENTER);
        workshop_button(root, 691, tops[i], 58, 54, "+", 2, inc[i], BUTTON_GRAY);
    }
    workshop_button(root, 803, 388, 205, 59, "+  Добавить группу", 1,
                    U_WORKSHOP_ADD_GROUP, WS_CYAN);
    label(root, 207, 485, 850, 38, "БЫСТРЫЙ ВЫБОР СЛОЯ", 1,
          WS_YELLOW, LV_TEXT_ALIGN_CENTER);
    const char *layers[] = {"B5", "B4", "B3", "B2", "B1",
                            "T1", "T2", "T3", "T4", "По ум."};
    for (int i = 0; i < 10; ++i) {
        int x = 198 + i * 88;
        workshop_button(root, x, 530, 80, 52, layers[i], 0,
                        U_WORKSHOP_LAYER_BASE + i,
                        workshop_layer == i ? WS_CYAN : BUTTON_GRAY);
    }
    workshop_button(root, 500, 620, 280, 61, "ОК", 3,
                    U_WORKSHOP_DIALOG_OK, WS_GREEN);
}

static void workshop_slider_changed(lv_event_t *ev) {
    lv_obj_t *slider = lv_event_get_target(ev);
    switch ((intptr_t)lv_event_get_user_data(ev)) {
    case WS_SLIDER_MOVE_X:
        workshop_move_x = lv_slider_get_value(slider);break;
    case WS_SLIDER_MOVE_Y:
        workshop_move_y = lv_slider_get_value(slider);break;
    case WS_SLIDER_MOVE_TIME:
        workshop_move_time = lv_slider_get_value(slider);break;
    }
    dirty = 1;
}

static lv_obj_t *workshop_slider(lv_obj_t *root, int x, int y, int w, int h,
                                 int min, int max, int value, int kind) {
    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_remove_style_all(slider);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, h);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, WS_DARK_VALUE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, C(000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, WS_YELLOW, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, WS_CREAM, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, C(000000), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_width(slider, 24, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 24, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 12, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, workshop_slider_changed,
                        LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)kind);
    return slider;
}

static void workshop_lock_option(lv_obj_t *root, int x, int y, int label_width,
                                 const char *text, int code, int active) {
    workshop_button(root, x, y, 44, 44, active ? "X" : "", 1, code,
                    active ? WS_CYAN : BUTTON_GRAY);
    label(root, x + 54, y + 2, label_width, 40, text, 0,
          WS_CREAM, LV_TEXT_ALIGN_LEFT);
}

static void workshop_draw_color_dialog(lv_obj_t *root) {
    workshop_dialog_shade(root);
    box(root, 185, 68, 910, 584, 17, WS_BROWN, 0);
    box(root, 196, 79, 888, 72, 12, WS_BROWN_DARK, 0);
    label(root, 225, 93, 830, 46, "Цвет объекта", 3,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);
    static const char *const sets[] = {
        "Базовая", "Яркая", "Холодная", "Пастель"
    };
    for (int i = 0; i < 4; ++i) {
        workshop_button(root, 226 + i * 211, 181, 194, 55, sets[i], 0,
                        U_WORKSHOP_COLOR_SET_BASE + i,
                        workshop_color_set == i ? WS_CYAN : WS_BROWN_DARK);
    }
    for (int i = 0; i < 8; ++i) {
        int col = i % 4, row = i / 4;
        int x = 276 + col * 177, y = 278 + row * 116;
        lv_color_t color = lv_color_hex(workshop_colors[workshop_color_set][i]);
        lv_obj_t *swatch = workshop_button(root, x, y, 150, 88, "", 0,
                        U_WORKSHOP_COLOR_BASE + i, color);
        if (workshop_color_index == i) {
            lv_obj_set_style_border_color(swatch, WS_CREAM, 0);
            lv_obj_set_style_border_width(swatch, 6, 0);
        }
    }
    char selected[56];
    snprintf(selected, sizeof selected, "Палитра %d · выбери цвет",
             workshop_color_set + 1);
    label(root, 230, 500, 820, 40, selected, 1,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 500, 566, 280, 64, "ОК", 3,
                    U_WORKSHOP_DIALOG_OK, WS_GREEN);
}

static void workshop_draw_move_dialog(lv_obj_t *root) {
    workshop_dialog_shade(root);
    box(root, 92, 19, 1096, 682, 17, WS_BROWN, 0);
    box(root, 103, 30, 1074, 76, 12, WS_BROWN_DARK, 0);
    label(root, 143, 45, 880, 47, "Настроить команду перемещения", 3,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);

    char value[32];
    label(root, 145, 130, 122, 42, "Смещение X", 0, WS_CREAM,
          LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof value, "%d", workshop_move_x);
    box(root, 268, 125, 105, 52, 8, WS_DARK_VALUE, 0);
    label(root, 271, 131, 99, 40, value, 2, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_slider(root, 389, 138, 232, 26, -16, 16, workshop_move_x,
                    WS_SLIDER_MOVE_X);

    label(root, 649, 130, 120, 42, "Смещение Y", 0, WS_CREAM,
          LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof value, "%d", workshop_move_y);
    box(root, 771, 125, 105, 52, 8, WS_DARK_VALUE, 0);
    label(root, 774, 131, 99, 40, value, 2, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_slider(root, 891, 138, 258, 26, -8, 8, workshop_move_y,
                    WS_SLIDER_MOVE_Y);

    label(root, 145, 190, 466, 37, "БЛОКИРОВКА X", 2, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    label(root, 661, 190, 470, 37, "БЛОКИРОВКА Y", 2, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    static const char *const lock_x_names[] = {
        "Игрок", "Камера", "Режим цели", "Направление"
    };
    static const char *const lock_y_names[] = {
        "Игрок", "Камера", "Малый шаг", "Динамический режим"
    };
    const int x_flags[] = {1, 2, 4, 8};
    const int y_flags[] = {1, 2, 4, 8};
    const int x_positions[] = {149, 374, 149, 374};
    const int y_positions[] = {665, 890, 665, 890};
    const int option_rows[] = {231, 231, 286, 286};
    for (int i = 0; i < 4; ++i) {
        workshop_lock_option(root, x_positions[i], option_rows[i], 155,
            lock_x_names[i], U_WORKSHOP_LOCK_X_BASE + i,
            (workshop_lock_x & x_flags[i]) != 0);
        workshop_lock_option(root, y_positions[i], option_rows[i], 174,
            lock_y_names[i], U_WORKSHOP_LOCK_Y_BASE + i,
            (workshop_lock_y & y_flags[i]) != 0);
    }

    label(root, 145, 361, 193, 42, "Время перемещения", 0,
          WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof value, "%.2f c", workshop_move_time * .5f);
    box(root, 339, 356, 113, 52, 8, WS_DARK_VALUE, 0);
    label(root, 342, 362, 107, 40, value, 1, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_slider(root, 467, 369, 237, 26, 0, 20, workshop_move_time,
                    WS_SLIDER_MOVE_TIME);
    label(root, 735, 361, 143, 42, "Плавность", 2,
          WS_YELLOW, LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 884, 356, 52, 52, "‹", 2,
                    U_WORKSHOP_EASE_PREV, BUTTON_GRAY);
    static const char *const easing_names[] = {"Нет", "Вход", "Выход", "Плавно"};
    box(root, 943, 356, 183, 52, 8, WS_DARK_VALUE, 0);
    label(root, 947, 361, 175, 42, easing_names[workshop_easing & 3], 2,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 1132, 356, 52, 52, "›", 2,
                    U_WORKSHOP_EASE_NEXT, BUTTON_GRAY);

    workshop_button(root, 145, 463, 187, 56, "Касание", 1,
                    U_WORKSHOP_TOUCH, workshop_touch ? WS_CYAN : BUTTON_GRAY);
    workshop_button(root, 345, 463, 187, 56, "Появление", 1,
                    U_WORKSHOP_SPAWN, workshop_spawn ? WS_CYAN : BUTTON_GRAY);
    workshop_button(root, 545, 463, 187, 56, "Без звука", 1,
                    U_WORKSHOP_SILENT, workshop_silent ? WS_CYAN : BUTTON_GRAY);
    label(root, 790, 450, 374, 40, "ID целевой группы", 2,
          WS_YELLOW, LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 841, 493, 56, 52, "−", 2,
                    U_WORKSHOP_GROUP_DEC, BUTTON_GRAY);
    snprintf(value, sizeof value, "%d", workshop_target_group);
    box(root, 906, 493, 153, 52, 8, WS_DARK_VALUE, 0);
    label(root, 909, 499, 147, 40, value, 2, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 1068, 493, 56, 52, "+", 2,
                    U_WORKSHOP_GROUP_INC, BUTTON_GRAY);
    workshop_button(root, 500, 608, 280, 64, "ОК", 3,
                    U_WORKSHOP_DIALOG_OK, WS_GREEN);
}

static const char * const workshop_kb_ru_lower[] = {
    "й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "\n",
    "ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э", "\n",
    "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "ё", "ъ", "\n",
    "EN", "123", "Аа", "DEL", "SPACE", "CANCEL", "OK", ""
};
static const char * const workshop_kb_ru_upper[] = {
    "Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х", "\n",
    "Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э", "\n",
    "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", "Ё", "Ъ", "\n",
    "EN", "123", "аА", "DEL", "SPACE", "CANCEL", "OK", ""
};
static const char * const workshop_kb_en_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "1", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "2", "\n",
    "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "3", "\n",
    "РУ", "123", "Aa", "DEL", "SPACE", "CANCEL", "OK", ""
};
static const char * const workshop_kb_en_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "1", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "2", "\n",
    "Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", "3", "\n",
    "РУ", "123", "aA", "DEL", "SPACE", "CANCEL", "OK", ""
};
static const char * const workshop_kb_number[] = {
    "1", "2", "3", "CANCEL", "\n",
    "4", "5", "6", "OK", "\n",
    "7", "8", "9", "DEL", "\n",
    "+/-", "0", ".", "LEFT", "RIGHT", ""
};
static const lv_buttonmatrix_ctrl_t workshop_kb_number_ctrl[17] = {
    1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, 2,
    1, 1, 1, 1, 1
};
static const char * const workshop_kb_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL", "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    "CANCEL", "LEFT", "SPACE", "RIGHT", "OK", ""
};
#define WS_KB_CTRL (LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT)
#define WS_KB_ROW_CTRL WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, \
                      WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL
static const lv_buttonmatrix_ctrl_t workshop_kb_ctrl[40] = {
    WS_KB_ROW_CTRL, WS_KB_ROW_CTRL, WS_KB_ROW_CTRL,
    WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL, WS_KB_CTRL,
    WS_KB_CTRL, WS_KB_CTRL
};
#undef WS_KB_ROW_CTRL
#undef WS_KB_CTRL

static void workshop_keyboard_special_mode(lv_obj_t *keyboard) {
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_SPECIAL);
    lv_buttonmatrix_set_button_width(keyboard, 10, 2);
    lv_buttonmatrix_set_button_width(keyboard, 11, 2);
    lv_buttonmatrix_set_button_width(keyboard, 35, 2);
    lv_buttonmatrix_set_button_width(keyboard, 36, 2);
    lv_buttonmatrix_set_button_width(keyboard, 37, 6);
    lv_buttonmatrix_set_button_width(keyboard, 38, 2);
    lv_buttonmatrix_set_button_width(keyboard, 39, 2);
}

static void workshop_keyboard_text_mode(lv_obj_t *keyboard) {
    lv_keyboard_mode_t mode = workshop_text_language ?
        (workshop_text_upper ? LV_KEYBOARD_MODE_USER_4 : LV_KEYBOARD_MODE_USER_3) :
        (workshop_text_upper ? LV_KEYBOARD_MODE_USER_2 : LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_mode(keyboard, mode);
    lv_buttonmatrix_set_button_width(keyboard, 33, 2);
    lv_buttonmatrix_set_button_width(keyboard, 34, 2);
    lv_buttonmatrix_set_button_width(keyboard, 35, 2);
    lv_buttonmatrix_set_button_width(keyboard, 36, 2);
    lv_buttonmatrix_set_button_width(keyboard, 37, 5);
    lv_buttonmatrix_set_button_width(keyboard, 38, 3);
    lv_buttonmatrix_set_button_width(keyboard, 39, 2);
}

static int workshop_input_is_trigger(int kind) {
    return kind >= WS_INPUT_TRIGGER_GROUP && kind <= WS_INPUT_TRIGGER_DURATION;
}
static int workshop_input_is_geometry(int kind) {
    return kind >= WS_INPUT_OBJECT_WIDTH && kind <= WS_INPUT_OBJECT_ANGLE;
}
static const char *workshop_input_title(void) {
    switch (workshop_input_kind) {
    case WS_INPUT_TITLE: return "Название уровня";
    case WS_INPUT_DESCRIPTION: return "Описание уровня";
    case WS_INPUT_TRIGGER_GROUP: return "Группа триггера · 0–9999";
    case WS_INPUT_TRIGGER_X: return "Смещение X · −9999…9999";
    case WS_INPUT_TRIGGER_Y: return "Смещение Y · −9999…9999";
    case WS_INPUT_TRIGGER_DURATION: return "Время вращения · 1–9999 секунд";
    case WS_INPUT_OBJECT_WIDTH: return "Ширина объекта · 0,1–64";
    case WS_INPUT_OBJECT_HEIGHT: return "Высота объекта · 0,1–40";
    case WS_INPUT_OBJECT_ANGLE: return "Свободный угол · 0–360 градусов";
    default: return "Ввод значения";
    }
}

static void workshop_cancel_keyboard_input(void) {
    workshop_keyboard_active = 0;
    workshop_input_kind = WS_INPUT_NONE;
    workshop_active_textarea = NULL;
    workshop_dialog = workshop_input_return_dialog;
    dirty = 1;
}

static void workshop_finish_keyboard_input(void) {
    if (!workshop_active_textarea) {workshop_cancel_keyboard_input();return;}
    const char *text = lv_textarea_get_text(workshop_active_textarea);
    if (!text) text = "";
    if (workshop_input_kind == WS_INPUT_TITLE) {
        snprintf(workshop_name, sizeof workshop_name, "%s", text);
    } else if (workshop_input_kind == WS_INPUT_DESCRIPTION) {
        snprintf(workshop_description, sizeof workshop_description, "%s", text);
    } else if (workshop_input_is_geometry(workshop_input_kind)) {
        char *end = NULL;
        float value = strtof(text, &end);
        if (end == text || !end || *end || !isfinite(value)) value = 0;
        if (workshop_input_kind == WS_INPUT_OBJECT_WIDTH)
            workshop_set_selected_size(1, value);
        else if (workshop_input_kind == WS_INPUT_OBJECT_HEIGHT)
            workshop_set_selected_size(0, value);
        else workshop_set_selected_angle(value);
    } else if (workshop_input_is_trigger(workshop_input_kind) &&
               workshop_selected >= 0 &&
               workshop_selected < workshop_object_count &&
               workshop_objects[workshop_selected].type == ON_LEVEL_TRIGGER) {
        char *end = NULL;
        long value = strtol(text, &end, 10);
        if (end == text || !end || *end) value = 0;
        WorkshopObject *o = &workshop_objects[workshop_selected];
        if (workshop_input_kind == WS_INPUT_TRIGGER_GROUP) {
            if (value < 0) value = 0;
            if (value > 9999) value = 9999;
            o->trigger_group_id = (int)value;
        } else if (workshop_input_kind == WS_INPUT_TRIGGER_X) {
            if (value < -9999) value = -9999;
            if (value > 9999) value = 9999;
            o->trigger_x = (int)value;
        } else if (workshop_input_kind == WS_INPUT_TRIGGER_Y) {
            if (value < -9999) value = -9999;
            if (value > 9999) value = 9999;
            o->trigger_y = (int)value;
        } else if (workshop_input_kind == WS_INPUT_TRIGGER_DURATION) {
            if (value < 1) value = 1;
            if (value > 9999) value = 9999;
            o->trigger_duration = (int)value;
        }
    }
    workshop_keyboard_active = 0;
    workshop_input_kind = WS_INPUT_NONE;
    workshop_active_textarea = NULL;
    workshop_dialog = workshop_input_return_dialog;
    dirty = 1;
}

static void workshop_keyboard_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *keyboard = lv_event_get_target(event);
    lv_obj_t *textarea = lv_keyboard_get_textarea(keyboard);
    uint32_t selected = lv_keyboard_get_selected_button(keyboard);
    const char *text = lv_keyboard_get_button_text(keyboard, selected);
    if (!textarea || !text) return;
    lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
    if (!strcmp(text, "OK") || !strcmp(text, LV_SYMBOL_OK)) {
        workshop_finish_keyboard_input();return;
    }
    if (!strcmp(text, "CANCEL") || !strcmp(text, LV_SYMBOL_CLOSE) ||
        !strcmp(text, LV_SYMBOL_KEYBOARD)) {
        workshop_cancel_keyboard_input();return;
    }
    if (!strcmp(text, "EN")) {
        workshop_text_language = 1;workshop_text_upper = 0;
        workshop_keyboard_text_mode(keyboard);return;
    }
    if (!strcmp(text, "РУ")) {
        workshop_text_language = 0;workshop_text_upper = 0;
        workshop_keyboard_text_mode(keyboard);return;
    }
    if (!strcmp(text, "Аа") || !strcmp(text, "аА") ||
        !strcmp(text, "Aa") || !strcmp(text, "aA") || !strcmp(text, "abc")) {
        if (mode == LV_KEYBOARD_MODE_SPECIAL) {
            workshop_text_upper = 0;workshop_keyboard_text_mode(keyboard);
        } else {
            workshop_text_upper = !workshop_text_upper;
            workshop_keyboard_text_mode(keyboard);
        }
        return;
    }
    if (!strcmp(text, "123")) {
        workshop_keyboard_special_mode(keyboard);return;
    }
    if (!strcmp(text, "DEL") || !strcmp(text, LV_SYMBOL_BACKSPACE)) {
        lv_textarea_delete_char(textarea);return;
    }
    if (!strcmp(text, "SPACE") || !strcmp(text, " ")) {
        lv_textarea_add_char(textarea, ' ');return;
    }
    if (!strcmp(text, LV_SYMBOL_LEFT) || !strcmp(text, "LEFT")) {
        lv_textarea_cursor_left(textarea);return;
    }
    if (!strcmp(text, LV_SYMBOL_RIGHT) || !strcmp(text, "RIGHT")) {
        lv_textarea_cursor_right(textarea);return;
    }
    if (!strcmp(text, "Enter") || !strcmp(text, LV_SYMBOL_NEW_LINE)) {
        if (workshop_input_kind == WS_INPUT_DESCRIPTION)
            lv_textarea_add_char(textarea, '\n');
        else workshop_finish_keyboard_input();
        return;
    }
    if (!strcmp(text, "+/-")) {
        const char *current = lv_textarea_get_text(textarea);
        char updated[32];
        if (current[0] == '-') snprintf(updated, sizeof updated, "%s", current + 1);
        else snprintf(updated, sizeof updated, "-%s", current);
        lv_textarea_set_text(textarea, updated);
        lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);return;
    }
    lv_textarea_add_text(textarea, text);
}

static void workshop_open_keyboard_input(int input_kind) {
    if (workshop_input_is_trigger(input_kind) &&
        (workshop_selected < 0 || workshop_selected >= workshop_object_count ||
         workshop_objects[workshop_selected].type != ON_LEVEL_TRIGGER)) return;
    if (workshop_input_is_geometry(input_kind) && !workshop_selection_count()) return;
    workshop_input_kind = input_kind;
    workshop_input_return_dialog = workshop_dialog;
    workshop_keyboard_active = 1;
    workshop_text_language = 0;workshop_text_upper = 0;
    workshop_active_textarea = NULL;
    dirty = 1;
}

static void workshop_draw_keyboard_dialog(lv_obj_t *root) {
    if (!workshop_keyboard_active) return;
    workshop_dialog_shade(root);
    box(root, 36, 18, 1208, 684, 17, WS_BROWN, 0);
    box(root, 48, 30, 1184, 64, 12, WS_BROWN_DARK, 0);
    label(root, 72, 40, 1136, 45, workshop_input_title(), 3,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *textarea = lv_textarea_create(root);
    workshop_active_textarea = textarea;
    lv_obj_set_pos(textarea, 82, 104);
    lv_obj_set_size(textarea, 1116, 96);
    lv_obj_set_style_bg_color(textarea, WS_DARK_VALUE, 0);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(textarea, WS_CREAM, 0);
    lv_obj_set_style_text_font(textarea, f(2), 0);
    lv_obj_set_style_border_color(textarea, WS_CREAM, 0);
    lv_obj_set_style_border_width(textarea, 2, 0);
    lv_obj_set_style_radius(textarea, 8, 0);
    lv_textarea_set_cursor_click_pos(textarea, true);
    int numeric = workshop_input_kind >= WS_INPUT_TRIGGER_GROUP;
    lv_textarea_set_one_line(textarea, numeric || workshop_input_kind == WS_INPUT_TITLE);
    lv_textarea_set_max_length(textarea,
        workshop_input_kind == WS_INPUT_TITLE ? 80 :
        workshop_input_kind == WS_INPUT_DESCRIPTION ? 160 : 10);
    if (numeric) lv_textarea_set_accepted_chars(textarea, "-0123456789.");
    char initial[ON_LEVEL_DESCRIPTION_SIZE] = {0};
    if (workshop_input_kind == WS_INPUT_TITLE)
        snprintf(initial, sizeof initial, "%s", workshop_name);
    else if (workshop_input_kind == WS_INPUT_DESCRIPTION)
        snprintf(initial, sizeof initial, "%s", workshop_description);
    else if (workshop_input_is_trigger(workshop_input_kind) &&
             workshop_selected >= 0 && workshop_selected < workshop_object_count) {
        const WorkshopObject *o = &workshop_objects[workshop_selected];
        int value = workshop_input_kind == WS_INPUT_TRIGGER_GROUP ? o->trigger_group_id :
                    workshop_input_kind == WS_INPUT_TRIGGER_X ? o->trigger_x :
                    workshop_input_kind == WS_INPUT_TRIGGER_Y ? o->trigger_y :
                    o->trigger_duration;
        snprintf(initial, sizeof initial, "%d", value);
    } else if (workshop_input_is_geometry(workshop_input_kind)) {
        float *x, *y, *w, *h, *angle;
        if (workshop_primary_geometry(&x, &y, &w, &h, &angle)) {
            (void)x;(void)y;
            float value = workshop_input_kind == WS_INPUT_OBJECT_WIDTH ? *w :
                          workshop_input_kind == WS_INPUT_OBJECT_HEIGHT ? *h : *angle;
            snprintf(initial, sizeof initial, "%.2f", value);
        }
    }
    lv_textarea_set_text(textarea, initial);
    lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_t *keyboard = lv_keyboard_create(root);
    lv_obj_set_size(keyboard, 1116, 460);
    lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 82, 218);
    lv_obj_set_style_bg_color(keyboard, WS_BROWN_DARK, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(keyboard, f(1), LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, workshop_kb_ru_lower,
                        workshop_kb_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_2, workshop_kb_ru_upper,
                        workshop_kb_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_3, workshop_kb_en_lower,
                        workshop_kb_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_4, workshop_kb_en_upper,
                        workshop_kb_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_NUMBER, workshop_kb_number,
                        workshop_kb_number_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, workshop_kb_special,
                        workshop_kb_ctrl);
    if (numeric) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    else workshop_keyboard_text_mode(keyboard);
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, workshop_keyboard_event, LV_EVENT_VALUE_CHANGED, NULL);
}

static void workshop_draw_trigger_dialog(lv_obj_t *root) {
    if (workshop_selected < 0 || workshop_selected >= workshop_object_count) return;
    WorkshopObject *o = &workshop_objects[workshop_selected];
    if (o->type != ON_LEVEL_TRIGGER) return;
    static const char *const events[] = {
        "Касание", "Подбор монеты", "Кнопка действия", "Старт уровня"
    };
    workshop_dialog_shade(root);
    box(root, 92, 34, 1096, 652, 17, WS_BROWN, 0);
    box(root, 103, 45, 1074, 76, 12, WS_BROWN_DARK, 0);
    char title[72];
    snprintf(title, sizeof title, "Настройка триггера · %s",
             workshop_trigger_name(o->trigger_kind));
    label(root, 130, 58, 1018, 50, title, 3, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    label(root, 158, 151, 260, 42, "Событие", 2, WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    workshop_button(root, 433, 144, 66, 58, "‹", 2,
                    U_WORKSHOP_TRIGGER_EVENT_PREV, BUTTON_GRAY);
    box(root, 507, 144, 330, 58, 8, WS_DARK_VALUE, 0);
    label(root, 514, 151, 316, 44, events[o->trigger_event % 4], 2,
          WS_CREAM, LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 845, 144, 66, 58, "›", 2,
                    U_WORKSHOP_TRIGGER_EVENT_NEXT, BUTTON_GRAY);
    label(root, 158, 229, 310, 42,
          o->trigger_kind == ON_TRIGGER_KIND_FOREVER ? "Группа" : "Целевая группа",
          2, WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    char value[32];snprintf(value, sizeof value, "%d", o->trigger_group_id);
    workshop_button(root, 490, 216, 300, 64, value, 2,
                    U_WORKSHOP_TRIGGER_GROUP_EDIT, WS_DARK_VALUE);
    if (o->trigger_kind == ON_TRIGGER_KIND_MOVE) {
        label(root, 158, 326, 260, 42, "Смещение X", 2,
              WS_YELLOW, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof value, "%d", o->trigger_x);
        workshop_button(root, 490, 311, 300, 64, value, 2,
                        U_WORKSHOP_TRIGGER_X_EDIT, WS_DARK_VALUE);
        label(root, 158, 423, 260, 42, "Смещение Y", 2,
              WS_YELLOW, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof value, "%d", o->trigger_y);
        workshop_button(root, 490, 408, 300, 64, value, 2,
                        U_WORKSHOP_TRIGGER_Y_EDIT, WS_DARK_VALUE);
        label(root, 810, 322, 300, 94, "Оба смещения задаются\nотдельно, до ±9999.",
              1, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    } else if (o->trigger_kind == ON_TRIGGER_KIND_ROTATE) {
        label(root, 158, 354, 260, 42, "Время, секунды", 2,
              WS_YELLOW, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof value, "%d с", o->trigger_duration);
        workshop_button(root, 490, 339, 300, 64, value, 2,
                        U_WORKSHOP_TRIGGER_DURATION_EDIT, WS_DARK_VALUE);
        label(root, 810, 330, 300, 90,
              "Группа вращается\nсо скоростью 1 оборот/с.",
              1, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    } else if (o->trigger_kind == ON_TRIGGER_KIND_FOREVER) {
        const char *action = o->trigger_action == ON_TRIGGER_ACTIVATE ?
                             "Активировать навсегда" : "Деактивировать навсегда";
        label(root, 158, 354, 300, 42, "Действие", 2,
              WS_YELLOW, LV_TEXT_ALIGN_LEFT);
        workshop_button(root, 490, 339, 460, 64, action, 1,
                        U_WORKSHOP_TRIGGER_ACTION, WS_CYAN);
        label(root, 158, 435, 950, 46,
              "Группа останется в выбранном состоянии до другого триггера.",
              1, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    } else {
        const char *description = o->trigger_kind == ON_TRIGGER_KIND_INVISIBILITY ?
            "Объекты исчезнут с экрана, но сохранят столкновения." :
            "После события у объектов группы отключится столкновение.";
        label(root, 158, 354, 950, 76, description, 2,
              WS_CREAM, LV_TEXT_ALIGN_CENTER);
    }
    label(root, 158, 480, 260, 26, "Вид триггера", 0,
          WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    static const char *const trigger_names[] = {
        "Движение", "Разворот", "Вечно", "Невидимость", "Нет столкновения"
    };
    static const int trigger_x[] = {158, 364, 570, 776, 982};
    for (int i = 0; i < 5; ++i)
        workshop_button(root, trigger_x[i], 508, 190, 40, trigger_names[i], 0,
                        U_WORKSHOP_TRIGGER_BASE + i,
                        o->trigger_kind == i ? WS_CYAN : BUTTON_GRAY);
    workshop_button(root, 500, 596, 280, 64, "ОК", 3,
                    U_WORKSHOP_DIALOG_OK, WS_GREEN);
}

static void workshop_draw_transform_dialog(lv_obj_t *root) {
    if (!workshop_selection_count()) return;
    float *x, *y, *w, *h, *angle;
    if (!workshop_primary_geometry(&x, &y, &w, &h, &angle)) return;
    (void)x;(void)y;
    workshop_dialog_shade(root);
    box(root, 142, 50, 996, 620, 17, WS_BROWN, 0);
    box(root, 154, 62, 972, 72, 12, WS_BROWN_DARK, 0);
    label(root, 176, 71, 928, 52,
          "Размер и свободный поворот объекта", 3, WS_CREAM,
          LV_TEXT_ALIGN_CENTER);
    char value[48];
    label(root, 195, 164, 220, 46, "Ширина", 2, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof value, "%.2f", *w);
    workshop_button(root, 416, 155, 252, 64, value, 2,
                    U_WORKSHOP_OBJECT_WIDTH_EDIT, WS_DARK_VALUE);
    workshop_button(root, 683, 155, 78, 64, "- 0.1", 1,
                    U_WORKSHOP_SCALE_BASE, BUTTON_GRAY);
    workshop_button(root, 770, 155, 78, 64, "+ 0.1", 1,
                    U_WORKSHOP_SCALE_BASE + 1, BUTTON_GRAY);

    label(root, 195, 258, 220, 46, "Высота", 2, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof value, "%.2f", *h);
    workshop_button(root, 416, 249, 252, 64, value, 2,
                    U_WORKSHOP_OBJECT_HEIGHT_EDIT, WS_DARK_VALUE);
    workshop_button(root, 683, 249, 78, 64, "- 0.1", 1,
                    U_WORKSHOP_SCALE_BASE + 2, BUTTON_GRAY);
    workshop_button(root, 770, 249, 78, 64, "+ 0.1", 1,
                    U_WORKSHOP_SCALE_BASE + 3, BUTTON_GRAY);

    label(root, 195, 352, 220, 46, "Угол, градусы", 2, WS_YELLOW,
          LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof value, "%.2f", *angle);
    workshop_button(root, 416, 343, 252, 64, value, 2,
                    U_WORKSHOP_OBJECT_ANGLE_EDIT, WS_DARK_VALUE);
    workshop_rotation_button(root, 683, 343, 78, 64,
                             U_WORKSHOP_ROTATE_BASE, BUTTON_GRAY, -1,
                             "-45°", 0);
    workshop_rotation_button(root, 770, 343, 78, 64,
                             U_WORKSHOP_ROTATE_BASE + 1, BUTTON_GRAY, 1,
                             "+45°", 0);

    char note[96];
    snprintf(note, sizeof note,
             "Поворот здесь не меняет триггеры. Выбрано: %d",
             workshop_selection_count());
    label(root, 202, 451, 876, 54, note, 1, WS_CREAM,
          LV_TEXT_ALIGN_CENTER);
    workshop_button(root, 500, 578, 280, 64, "Готово", 3,
                    U_WORKSHOP_DIALOG_OK, WS_GREEN);
}

static void workshop_draw_world_object(lv_obj_t *root, int gx, int gy, int cell,
                                       int type, float x, float y, float w, float h,
                                       float angle, int art_id, lv_color_t color,
                                       int selected) {
    if (w <= 0 || h <= 0) return;
    float radians = angle * 0.01745329251994329577f;
    float cx = gx + (x + w * .5f - workshop_camera_x) * cell;
    float cy = gy + (y + h * .5f - workshop_camera_y) * cell;
    float half_w = (fabsf(cosf(radians)) * w + fabsf(sinf(radians)) * h) * cell * .5f;
    float half_h = (fabsf(sinf(radians)) * w + fabsf(cosf(radians)) * h) * cell * .5f;
    float right = gx + WS_GRID_COLS * cell;
    float bottom = gy + WS_GRID_ROWS * cell;
    if (cx + half_w <= gx || cx - half_w >= right ||
        cy + half_h <= gy || cy - half_h >= bottom) return;
    int shape_w = (int)lroundf(w * cell);
    int shape_h = (int)lroundf(h * cell);
    if (shape_w < 3) shape_w = 3;
    if (shape_h < 3) shape_h = 3;
    int left = (int)lroundf(cx - shape_w * .5f);
    int top = (int)lroundf(cy - shape_h * .5f);
    int rotation = (int)lroundf(angle * 10.0f);
    if (type != ON_LEVEL_SLOPE) {
        lv_obj_t *shape = box(root, left, top, shape_w, shape_h, 4, color, 0);
        lv_obj_set_style_transform_rotation(shape, rotation, 0);
    }
    if (art_id >= 0 && art_id < PV_ART_COUNT && pictures[art_id].data) {
        if (type == ON_LEVEL_GROUND && fabsf(angle) < .01f && w >= 2.0f) {
            int count = (int)ceilf(w / 2.0f);
            for (int i = 0; i < count; ++i) {
                float tile_x = x + i * 2.0f;
                float tile_w = fminf(2.0f, x + w - tile_x);
                if (tile_w <= 0) continue;
                int tile_cx = (int)lroundf(gx +
                    (tile_x + tile_w * .5f - workshop_camera_x) * cell);
                int tile_cy = (int)lroundf(cy);
                int target_w = (int)lroundf(tile_w * cell) - 4;
                int target_h = shape_h - 4;
                int source_w = pictures[art_id].header.w;
                int source_h = pictures[art_id].header.h;
                int image_w = target_w;
                if (image_w * source_h > target_h * source_w)
                    image_w = target_h * source_w / source_h;
                lv_obj_t *image = art(root, art_id, tile_cx, tile_cy, image_w);
                if (image) lv_image_set_rotation(image, rotation);
            }
        } else {
            int target_w = shape_w - 8;
            int target_h = shape_h - 8;
            if (target_w < 1) target_w = 1;
            if (target_h < 1) target_h = 1;
            int source_w = pictures[art_id].header.w;
            int source_h = pictures[art_id].header.h;
            int image_w = target_w;
            if (image_w * source_h > target_h * source_w)
                image_w = target_h * source_w / source_h;
            lv_obj_t *image = art(root, art_id, (int)lroundf(cx),
                                  (int)lroundf(cy), image_w);
            if (image) lv_image_set_rotation(image, rotation);
        }
    }
    if (selected) {
        lv_obj_t *outline = box(root, left, top, shape_w, shape_h, 0, color, 0);
        lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(outline, WS_CYAN, 0);
        lv_obj_set_style_border_width(outline, 4, 0);
        lv_obj_set_style_radius(outline, 0, 0);
        lv_obj_set_style_transform_rotation(outline, rotation, 0);
    }
}

static void workshop_editor_screen(lv_obj_t *root, const OnNetView *view) {
    box(root, 0, 0, GAME_W, GAME_H, 0, WS_BACKGROUND, 0);
    for (int x = 0; x < GAME_W; x += 64)
        box(root, x, 0, 1, GAME_H, 0, WS_GRID, 0);
    for (int y = 0; y < GAME_H; y += 64)
        box(root, 0, y, GAME_W, 1, 0, WS_GRID, 0);
    box(root, 0, 0, GAME_W, 102, 0, WS_BROWN_DARK, 0);
    label(root, 182, 17, 560, 60, workshop_name, 3, WS_CREAM,
          LV_TEXT_ALIGN_LEFT);
    button(root, 22, 20, 138, 60, "Назад", 2, U_WORKSHOP_BACK);
    workshop_button(root, 747, 20, 144, 60, "Публиковать", 1,
                    U_WORKSHOP_PUBLISH, WS_GREEN);
    workshop_button(root, 906, 20, 142, 60, "Сохранить", 1,
                    U_WORKSHOP_SAVE, WS_CYAN);
    workshop_button(root, 1062, 20, 194, 60, "Предпросмотр", 1,
                    U_WORKSHOP_PLAY, WS_GREEN);

    const int gx = 44, gy = 132, cell = 42;
    const int map_width = WS_GRID_COLS * cell;
    const int map_height = WS_GRID_ROWS * cell;
    box(root, gx - 10, gy - 10, map_width + 20,
        map_height + 20, 8, WS_BROWN_DARK, 0);
    lv_obj_t *map_clip = lv_obj_create(root);
    lv_obj_remove_style_all(map_clip);
    lv_obj_set_pos(map_clip, gx, gy);
    lv_obj_set_size(map_clip, map_width, map_height);
    lv_obj_remove_flag(map_clip,
        LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    box(map_clip, 0, 0, map_width, map_height, 0, WS_BACKGROUND, 0);
    for (int col = 0; col <= WS_GRID_COLS; ++col)
        box(map_clip, col * cell, 0, 1, map_height, 0, WS_GRID, 0);
    for (int row = 0; row <= WS_GRID_ROWS; ++row)
        box(map_clip, 0, row * cell, map_width, 1, 0, WS_GRID, 0);

    workshop_draw_world_object(map_clip, 0, 0, cell, ON_LEVEL_GROUND,
        workshop_ground_x, workshop_ground_y, workshop_ground_w, workshop_ground_h,
        workshop_ground_angle, PV_ART_LEVEL_PLATFORM, C(476B4E),
        workshop_is_selected(-4));
    workshop_draw_world_object(map_clip, 0, 0, cell, ON_LEVEL_PLAYER,
        workshop_player_x, workshop_player_y, workshop_player_w, workshop_player_h,
        workshop_player_angle, PV_ART_BREAD, C(8D5438),
        workshop_is_selected(-2));
    workshop_draw_world_object(map_clip, 0, 0, cell, ON_LEVEL_GOAL,
        workshop_goal_x, workshop_goal_y, workshop_goal_w, workshop_goal_h,
        workshop_goal_angle, PV_ART_LEVEL_FLAG, C(8D5438),
        workshop_is_selected(-3));
    for (int i = 0; i < workshop_object_count; ++i) {
        const WorkshopObject *o = &workshop_objects[i];
        lv_color_t color = lv_color_hex(
            workshop_colors[o->color_set % 4][o->color_index % 8]);
        workshop_draw_world_object(map_clip, 0, 0, cell, o->type,
            o->x, o->y, o->w, o->h, o->angle,
            workshop_object_art(o->type, o->trigger_kind), color,
            workshop_is_selected(i));
    }
    for (int row = 0; row < WS_GRID_ROWS; ++row)
        for (int col = 0; col < WS_GRID_COLS; ++col) {
            lv_obj_t *cell_button = button(root, gx + col * cell,
                                           gy + row * cell, cell, cell,
                                           "", 0,
                                           U_WORKSHOP_CELL_BASE + row * WS_GRID_COLS + col);
            lv_obj_set_style_bg_opa(cell_button, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(cell_button, LV_OPA_TRANSP, LV_STATE_PRESSED);
            lv_obj_set_style_border_width(cell_button, 0, 0);
            lv_obj_set_style_border_width(cell_button, 0, LV_STATE_PRESSED);
            lv_obj_set_style_radius(cell_button, 0, 0);
        }

    box(root, 744, 122, 506, 438, 11, WS_BROWN, 0);
    label(root, 769, 140, 450, 36, "РЕЖИМ РЕДАКТОРА", 1,
          WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    workshop_button(root, 760, 181, 108, 56, "Строить", 0,
                    U_WORKSHOP_BUILD, workshop_tool == WS_TOOL_BUILD ? WS_CYAN : BUTTON_GRAY);
    workshop_button(root, 875, 181, 108, 56, "Выбрать", 0,
                    U_WORKSHOP_EDITMODE, workshop_tool == WS_TOOL_EDIT ? WS_CYAN : BUTTON_GRAY);
    workshop_button(root, 990, 181, 108, 56, "Мульти", 0,
                    U_WORKSHOP_MULTIMODE, workshop_tool == WS_TOOL_MULTI ? WS_CYAN : BUTTON_GRAY);
    workshop_button(root, 1105, 181, 108, 56, "Удалить", 0,
                    U_WORKSHOP_DELETE, workshop_tool == WS_TOOL_DELETE ? WS_CYAN : BUTTON_GRAY);
    label(root, 769, 250, 450, 31, "ВЫБРАННЫЙ ОБЪЕКТ", 1,
          WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    char object_info[110];
    if (workshop_selection_count() > 1) {
        snprintf(object_info, sizeof object_info, "Выбрано объектов: %d",
                 workshop_selection_count());
    } else if (workshop_selected == -2)
        snprintf(object_info, sizeof object_info, "Игрок · X %.1f  Y %.1f",
                 workshop_player_x, workshop_player_y);
    else if (workshop_selected == -3)
        snprintf(object_info, sizeof object_info, "Финиш · X %.1f  Y %.1f",
                 workshop_goal_x, workshop_goal_y);
    else if (workshop_selected == -4)
        snprintf(object_info, sizeof object_info, "Платформа · X %.1f  Y %.1f",
                 workshop_ground_x, workshop_ground_y);
    else if (workshop_selected >= 0 && workshop_selected < workshop_object_count) {
        const WorkshopObject *o = &workshop_objects[workshop_selected];
        if (o->type == ON_LEVEL_TRIGGER)
            snprintf(object_info, sizeof object_info, "Триггер · %s · X %.1f Y %.1f",
                     workshop_trigger_name(o->trigger_kind), o->x, o->y);
        else
            snprintf(object_info, sizeof object_info, "%s · X %.1f Y %.1f",
                     workshop_object_name(o->type), o->x, o->y);
    } else snprintf(object_info, sizeof object_info, "Выбери категорию и клетку карты");
    label(root, 770, 282, 446, 42, object_info, 1,
          WS_CREAM, LV_TEXT_ALIGN_LEFT);
    int selected_trigger = workshop_selected >= 0 &&
        workshop_selected < workshop_object_count &&
        workshop_objects[workshop_selected].type == ON_LEVEL_TRIGGER;
    workshop_button(root, 760, 336, 145, 50,
                    selected_trigger ? "Настроить" : "Движение", 0,
                    U_WORKSHOP_MOVE_PANEL, BUTTON_GRAY);
    workshop_button(root, 917, 336, 140, 50, "Группа", 0,
                    U_WORKSHOP_GROUP_PANEL, BUTTON_GRAY);
    workshop_button(root, 1068, 336, 154, 50, "Цвет", 0,
                    U_WORKSHOP_COLOR_PANEL, BUTTON_GRAY);
    int show_block_variants = workshop_palette_type == ON_LEVEL_BLOCK &&
                              workshop_tool == WS_TOOL_BUILD;
    int show_trigger_variants = workshop_palette_type == ON_LEVEL_TRIGGER &&
                                workshop_tool == WS_TOOL_BUILD;
    if (show_block_variants) {
        label(root, 769, 394, 448, 27, "ВАРИАНТЫ БЛОКОВ", 0,
              WS_YELLOW, LV_TEXT_ALIGN_LEFT);
        static const int block_types[] = {
            ON_LEVEL_BLOCK, ON_LEVEL_SLOPE, ON_LEVEL_GROUND
        };
        static const char *const block_names[] = {"Блок", "Склон", "Платформа"};
        static const int block_x[] = {760, 916, 1072};
        for (int i = 0; i < 3; ++i)
            workshop_button(root, block_x[i], 420, 148, 38, block_names[i], 0,
                            U_WORKSHOP_BLOCK_VARIANT_BASE + i,
                            workshop_block_variant == block_types[i] ? WS_CYAN : BUTTON_GRAY);
    }
    if (show_trigger_variants) {
        label(root, 769, 394, 448, 27, "ВАРИАНТЫ ТРИГГЕРА", 0,
              WS_YELLOW, LV_TEXT_ALIGN_LEFT);
        static const char *const trigger_names[] = {
            "Движение", "Разворот", "Вечно", "Невидимость", "Нет столкновения"
        };
        static const int trigger_x[] = {760, 916, 1072, 760, 998};
        static const int trigger_y[] = {420, 420, 420, 461, 461};
        static const int trigger_w[] = {148, 148, 148, 222, 222};
        for (int i = 0; i < 5; ++i)
            workshop_button(root, trigger_x[i], trigger_y[i], trigger_w[i], 38,
                            trigger_names[i], 0, U_WORKSHOP_TRIGGER_BASE + i,
                            workshop_trigger_kind == i ? WS_CYAN : BUTTON_GRAY);
    }
    char camera_label[48];
    snprintf(camera_label, sizeof camera_label, "Карта X %d  Y %d",
             workshop_camera_x, workshop_camera_y);
    int camera_y = (show_block_variants || show_trigger_variants) ? 507 : 414;
    label(root, 768, camera_y, 214, 38, camera_label, 0,
          WS_CREAM, LV_TEXT_ALIGN_LEFT);
    workshop_arrow_button(root, 984, camera_y - 3, 52, 45,
                          U_WORKSHOP_PAN_BASE, WS_CYAN, -1);
    workshop_arrow_button(root, 1040, camera_y - 3, 52, 45,
                          U_WORKSHOP_PAN_BASE + 1, WS_CYAN, 0);
    workshop_arrow_button(root, 1096, camera_y - 3, 52, 45,
                          U_WORKSHOP_PAN_BASE + 2, WS_CYAN, 2);
    workshop_arrow_button(root, 1152, camera_y - 3, 52, 45,
                          U_WORKSHOP_PAN_BASE + 3, WS_CYAN, 1);

    const int palette_types[] = {ON_LEVEL_BLOCK, ON_LEVEL_HAZARD, ON_LEVEL_COIN,
        ON_LEVEL_ENEMY, ON_LEVEL_PLAYER, ON_LEVEL_GOAL, ON_LEVEL_TRIGGER};
    const char *palette_names[] = {"Блоки", "Шипы", "Монета", "Гусь",
        "Игрок", "Финиш", "Триггер"};
    for (int i = 0; i < 7; ++i) {
        int x = 39 + i * 98;
        int art_id = workshop_object_art(palette_types[i], workshop_trigger_kind);
        workshop_button(root, x, 565, 92, 62, "", 0,
                        U_WORKSHOP_PALETTE_BASE + i,
                        workshop_palette_type == palette_types[i] ? WS_CYAN : BUTTON_GRAY);
        if (art_id >= 0) art(root, art_id, x + 46, 583, 26);
        label(root, x + 1, 599, 90, 24, palette_names[i], 0,
              BUTTON_TEXT, LV_TEXT_ALIGN_CENTER);
    }
    label(root, 43, 637, 193, 28, "ДВИГАТЬ ОБЪЕКТЫ", 0,
          WS_YELLOW, LV_TEXT_ALIGN_LEFT);
    workshop_arrow_button(root, 43, 666, 46, 44,
                          U_WORKSHOP_NUDGE_BASE, WS_GREEN, -1);
    workshop_arrow_button(root, 92, 666, 46, 44,
                          U_WORKSHOP_NUDGE_BASE + 1, WS_GREEN, 0);
    workshop_arrow_button(root, 141, 666, 46, 44,
                          U_WORKSHOP_NUDGE_BASE + 2, WS_GREEN, 2);
    workshop_arrow_button(root, 190, 666, 46, 44,
                          U_WORKSHOP_NUDGE_BASE + 3, WS_GREEN, 1);
    label(root, 252, 637, 96, 28, "ПОВОРОТ", 0,
          WS_YELLOW, LV_TEXT_ALIGN_CENTER);
    workshop_rotation_button(root, 252, 666, 46, 44,
                             U_WORKSHOP_ROTATE_BASE, WS_GREEN, -1, NULL, 0);
    workshop_rotation_button(root, 303, 666, 46, 44,
                             U_WORKSHOP_ROTATE_BASE + 1, WS_GREEN, 1, NULL, 0);
    label(root, 357, 660, 363, 49,
          workshop_selection_count() ?
          "Стрелки двигают выбор; закруглённые кнопки вращают его на 45°." :
          "Выбери объекты: зелёные стрелки двигают, закруглённые поворачивают.",
          0, WS_CREAM, LV_TEXT_ALIGN_LEFT);
    const char *publish_status = workshop_notice[0] ? workshop_notice :
        view->level_publish_busy ? "Публикуем уровень в общий каталог…" :
        view->level_publish_notice[0] ? view->level_publish_notice :
        view->level_publish_id[0] ? "Уровень опубликован. ID показан ниже." :
        "Зелёные стрелки двигают выделение, бирюзовые — карту.";
    label(root, 747, 562, 493, 64, publish_status,
          1, WS_CREAM, LV_TEXT_ALIGN_CENTER);
    if (view->level_publish_id[0]) {
        char published_id[48];
        snprintf(published_id, sizeof published_id, "ID %s", view->level_publish_id);
        label(root, 747, 628, 493, 32, published_id, 1, WS_YELLOW,
              LV_TEXT_ALIGN_CENTER);
    }
    workshop_button(root, 749, 670, 118, 43, "Копировать", 0,
                    U_WORKSHOP_COPY, BUTTON_GRAY);
    workshop_button(root, 873, 670, 118, 43, "Вставить", 0,
                    U_WORKSHOP_PASTE, BUTTON_GRAY);
    workshop_button(root, 997, 670, 118, 43, "Удалить", 0,
                    U_WORKSHOP_DELETE_SELECTED, WS_RED);
    workshop_button(root, 1121, 670, 128, 43, "Размер/угол", 0,
                    U_WORKSHOP_TRANSFORM_PANEL, WS_CYAN);

    if (workshop_dialog == WS_DIALOG_MOVE) workshop_draw_move_dialog(root);
    else if (workshop_dialog == WS_DIALOG_GROUP) workshop_draw_group_dialog(root);
    else if (workshop_dialog == WS_DIALOG_COLOR) workshop_draw_color_dialog(root);
    else if (workshop_dialog == WS_DIALOG_TRIGGER) workshop_draw_trigger_dialog(root);
    else if (workshop_dialog == WS_DIALOG_TRANSFORM) workshop_draw_transform_dialog(root);
    if (workshop_keyboard_active) workshop_draw_keyboard_dialog(root);
}

static void workshop_preview(void) {
    OnPublishedLevel level;
    workshop_build_preview(&level);
    (void)game_workshop_preview(&level);
}

/* Direction and jump symbols are vector geometry, not image assets: each
 * arrow is a rectangle shaft and a triangle head positioned by formulas. */
static void vector_arrow_draw(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    int cx = (bounds.x1 + bounds.x2) / 2;
    int cy = (bounds.y1 + bounds.y2) / 2;
    int width = bounds.x2 - bounds.x1 + 1;
    int height = bounds.y2 - bounds.y1 + 1;
    int size = width < height ? width : height;
    int head_length = size * 34 / 100;
    int head_half = size * 28 / 100;
    int shaft_half = size * 9 / 100;
    if (head_length < 8) head_length = 8;
    if (head_half < 6) head_half = 6;
    if (shaft_half < 2) shaft_half = 2;
    lv_area_t shaft;
    lv_draw_rect_dsc_t shaft_dsc;
    lv_draw_rect_dsc_init(&shaft_dsc);
    shaft_dsc.bg_color = BUTTON_TEXT;
    shaft_dsc.bg_opa = LV_OPA_COVER;
    shaft_dsc.radius = 3;
    lv_draw_triangle_dsc_t head;
    lv_draw_triangle_dsc_init(&head);
    head.color = BUTTON_TEXT;
    head.opa = LV_OPA_COVER;
    if (direction == -1) { /* left */
        int base = cx - head_length / 3;
        shaft = (lv_area_t){base, cy - shaft_half, cx + head_length / 2,
                            cy + shaft_half};
        head.p[0] = (lv_point_precise_t){cx - head_length, cy};
        head.p[1] = (lv_point_precise_t){base, cy - head_half};
        head.p[2] = (lv_point_precise_t){base, cy + head_half};
    } else if (direction == 1) { /* right */
        int base = cx + head_length / 3;
        shaft = (lv_area_t){cx - head_length / 2, cy - shaft_half,
                            base, cy + shaft_half};
        head.p[0] = (lv_point_precise_t){cx + head_length, cy};
        head.p[1] = (lv_point_precise_t){base, cy - head_half};
        head.p[2] = (lv_point_precise_t){base, cy + head_half};
    } else if (direction == 2) { /* down */
        int base = cy + head_length / 3;
        shaft = (lv_area_t){cx - shaft_half, cy - head_length / 2,
                            cx + shaft_half, base};
        head.p[0] = (lv_point_precise_t){cx, cy + head_length};
        head.p[1] = (lv_point_precise_t){cx - head_half, base};
        head.p[2] = (lv_point_precise_t){cx + head_half, base};
    } else { /* up */
        int base = cy - head_length / 3;
        shaft = (lv_area_t){cx - shaft_half, base,
                            cx + shaft_half, cy + head_length / 2};
        head.p[0] = (lv_point_precise_t){cx, cy - head_length};
        head.p[1] = (lv_point_precise_t){cx - head_half, base};
        head.p[2] = (lv_point_precise_t){cx + head_half, base};
    }
    lv_draw_rect(layer, &shaft_dsc, &shaft);
    lv_draw_triangle(layer, &head);
}

static void vector_refresh_draw(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    int cx = bounds.x1 + 28;
    int cy = (bounds.y1 + bounds.y2) / 2;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = (lv_point_t){cx, cy};
    arc.radius = 11;
    arc.width = 3;
    arc.color = BUTTON_TEXT;
    arc.opa = LV_OPA_COVER;
    arc.rounded = 1;
    arc.start_angle = 48;
    arc.end_angle = 318;
    lv_draw_arc(layer, &arc);
    lv_draw_triangle_dsc_t head;
    lv_draw_triangle_dsc_init(&head);
    head.color = BUTTON_TEXT;
    head.opa = LV_OPA_COVER;
    head.p[0] = (lv_point_precise_t){cx + 13, cy - 13};
    head.p[1] = (lv_point_precise_t){cx + 1, cy - 11};
    head.p[2] = (lv_point_precise_t){cx + 11, cy - 1};
    lv_draw_triangle(layer, &head);
}

static void vector_rotation_draw(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    int width = bounds.x2 - bounds.x1 + 1;
    int height = bounds.y2 - bounds.y1 + 1;
    int size = width < height ? width : height;
    int radius = size * 24 / 100;
    if (radius < 7) radius = 7;
    int cx = (bounds.x1 + bounds.x2) / 2;
    int cy = bounds.y1 + height * 39 / 100;
    float sign = direction < 0 ? -1.0f : 1.0f;
    float start = direction < 0 ? 135.0f : 45.0f;
    float sweep = direction < 0 ? -270.0f : 270.0f;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = BUTTON_TEXT;
    line.opa = LV_OPA_COVER;
    line.width = size < 56 ? 2 : 3;
    line.round_start = 1;
    line.round_end = 1;
    const int segments = 20;
    for (int i = 0; i < segments; ++i) {
        float a0 = (start + sweep * i / segments) * 0.01745329251994329577f;
        float a1 = (start + sweep * (i + 1) / segments) * 0.01745329251994329577f;
        line.p1 = (lv_point_precise_t){cx + lroundf(cosf(a0) * radius),
                                       cy + lroundf(sinf(a0) * radius)};
        line.p2 = (lv_point_precise_t){cx + lroundf(cosf(a1) * radius),
                                       cy + lroundf(sinf(a1) * radius)};
        lv_draw_line(layer, &line);
    }
    float end = (start + sweep) * 0.01745329251994329577f;
    float tip_x = cx + cosf(end) * radius;
    float tip_y = cy + sinf(end) * radius;
    float tangent_x = -sinf(end) * sign;
    float tangent_y = cosf(end) * sign;
    float arrow_length = size < 56 ? 6.0f : 8.0f;
    float arrow_half = size < 56 ? 3.5f : 4.5f;
    float base_x = tip_x - tangent_x * arrow_length;
    float base_y = tip_y - tangent_y * arrow_length;
    float side_x = -tangent_y * arrow_half;
    float side_y = tangent_x * arrow_half;
    lv_draw_triangle_dsc_t head;
    lv_draw_triangle_dsc_init(&head);
    head.color = BUTTON_TEXT;
    head.opa = LV_OPA_COVER;
    head.p[0] = (lv_point_precise_t){lroundf(tip_x), lroundf(tip_y)};
    head.p[1] = (lv_point_precise_t){lroundf(base_x + side_x),
                                     lroundf(base_y + side_y)};
    head.p[2] = (lv_point_precise_t){lroundf(base_x - side_x),
                                     lroundf(base_y - side_y)};
    lv_draw_triangle(layer, &head);
}

static lv_obj_t *workshop_rotation_button(lv_obj_t *parent, int x, int y,
                                           int w, int h, int action,
                                           lv_color_t face, int direction,
                                           const char *caption, int caption_size) {
    lv_obj_t *rotate = workshop_button(parent, x, y, w, h, "", 0,
                                       action, face);
    lv_obj_add_event_cb(rotate, vector_rotation_draw, LV_EVENT_DRAW_MAIN,
                        (void *)(intptr_t)direction);
    if (caption && caption[0]) {
        int caption_height = caption_size == 0 ? 19 : 22;
        label(rotate, 2, h - caption_height - 2, w - 4, caption_height,
              caption, caption_size, BUTTON_TEXT, LV_TEXT_ALIGN_CENTER);
    }
    return rotate;
}

static lv_obj_t *workshop_arrow_button(lv_obj_t *parent, int x, int y,
                                       int w, int h, int action,
                                       lv_color_t face, int direction) {
    lv_obj_t *arrow = workshop_button(parent, x, y, w, h, "", 0, action, face);
    lv_obj_add_event_cb(arrow, vector_arrow_draw, LV_EVENT_DRAW_MAIN,
                        (void *)(intptr_t)direction);
    return arrow;
}

static void vector_arrow(lv_obj_t *parent, int x, int y, int direction) {
    lv_obj_t *icon = box(parent, x, y, 74, 74, 0, BUTTON_WHITE, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_add_event_cb(icon, vector_arrow_draw, LV_EVENT_DRAW_MAIN,
                        (void *)(intptr_t)direction);
}

static void custom_platformer_screen(lv_obj_t *root) {
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_t *bar = box(root, 0, 0, GAME_W, 103, 0, BUTTON_GRAY, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    char title[ON_LEVEL_TITLE_SIZE + 28];
    snprintf(title, sizeof title, "ID %s  ·  %s", "", "");
    OnNetView view;
    on_net_view(&view);
    snprintf(title, sizeof title, "ID %s  ·  %s",
             view.loaded_level.id[0] ? view.loaded_level.id : "—",
             view.loaded_level.title[0] ? view.loaded_level.title : "Уровень");
    label(root, 27, 18, 750, 66, title, 2, INK, LV_TEXT_ALIGN_LEFT);
    label(root, 790, 24, 228, 56, "Кнопки · мультитач", 1, MUTED,
          LV_TEXT_ALIGN_CENTER);
    button(root, 1033, 21, 215, 62, "К уровням", 2,
           U_CUSTOM_BACK);

    lv_obj_t *back = box(root, 58, 525, 100, 170, 12, BUTTON_WHITE, 0);
    vector_arrow(back, 13, 17, -1);
    label(back, 3, 111, 94, 43, "НАЗАД", 1, BUTTON_TEXT,
          LV_TEXT_ALIGN_CENTER);
    lv_obj_t *forward = box(root, 178, 525, 100, 170, 12, BUTTON_WHITE, 0);
    vector_arrow(forward, 13, 17, 1);
    label(forward, 1, 111, 98, 43, "ВПЕРЁД", 0, BUTTON_TEXT,
          LV_TEXT_ALIGN_CENTER);

    lv_obj_t *trigger = box(root, 910, 567, 128, 99, 19, BUTTON_WHITE, 1);
    lv_obj_set_style_border_width(trigger, 2, 0);
    lv_obj_set_style_border_color(trigger, C(000000), 0);
    label(trigger, 5, 21, 118, 56, "ДЕЙСТВИЕ", 1, BUTTON_TEXT,
          LV_TEXT_ALIGN_CENTER);
    lv_obj_t *jump = box(root, 1081, 525, 169, 169, 84, BUTTON_GRAY, 1);
    lv_obj_set_style_border_width(jump, 3, 0);
    lv_obj_set_style_border_color(jump, C(000000), 0);
    vector_arrow(jump, 47, 13, 0);
    label(jump, 4, 102, 161, 44, "ПРЫЖОК", 1, BUTTON_TEXT,
          LV_TEXT_ALIGN_CENTER);
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
                                side == ON_ROLE_PLANTS ? U_PLANTS : U_ZOMBIES);
        button_fill(card, chosen ? BUTTON_GRAY : BUTTON_WHITE);
        lv_obj_set_style_border_width(card, chosen ? 5 : 3, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        art(card, side == ON_ROLE_PLANTS ? PV_ART_PEA : PV_ART_DUCK,
            264, 121, 210);
        label(card, 32, 224, 463, 61,
              side == ON_ROLE_PLANTS ? "Растения" : "Зомби", 3, BUTTON_TEXT,
              LV_TEXT_ALIGN_CENTER);
        box(card, 129, 285, 269, 6, 0,
            chosen ? C(000000) : BUTTON_GRAY, 0);
        if (taken)
            label(card, 155, 288, 220, 29, "Сторона занята", 1,
                  BUTTON_TEXT, LV_TEXT_ALIGN_CENTER);
    }
    box(root, 90, 609, 1100, 78, 17, DARK, 0);
    const char *status = v->notice[0] ? v->notice : !v->guest_id[0] ?
                         "Ждём друга" : v->busy ? "Сохраняем выбор..." :
                         !mine ? "Выбери сторону" : !other ?
                         "Ждём выбор друга" : "Начинаем бой...";
    label(root, 138, 623, 1004, 49, status, 2,
          v->notice[0] ? LIGHT_GRAY : WHITE, LV_TEXT_ALIGN_CENTER);
}

static void match_screen(lv_obj_t *root, const OnNetView *v) {
    OnMatch state;
    int role = 0;
    char hint[110];float hint_left = 0;
    game_online_ui_snapshot(&state, &role, NULL, hint, sizeof hint, &hint_left);
    int plants = role == ON_ROLE_PLANTS;
    box(root, 0, 0, 1280, 120, 0, BUTTON_GRAY, 0);
    box(root, 0, 118, 1280, 3, 0, BUTTON_BLACK, 0);
    lv_obj_t *rail = box(root, 0, 121, 246, 599, 0, C(2A2A2A), 0);
    lv_obj_set_style_bg_opa(rail, 115, 0);
    art(root, PV_ART_COIN, 40, 82, 40);
    char cash[24];
    snprintf(cash, sizeof cash, "%d", plants ? state.plant_cash : state.zombie_cash);
    label(root, 72, 53, 157, 58, cash, 3, INK, LV_TEXT_ALIGN_LEFT);
    char title[86];
    snprintf(title, sizeof title, "Комната %s  ·  осталось: %d",
             v->room_id, state.left + state.duck_count);
    label(root, 272, 32, 634, 45, title, 2, INK, LV_TEXT_ALIGN_LEFT);
    const char *message = drag_notice_left > 0 ? drag_notice :
                          hint_left > 0 ? hint : v->notice[0] ? v->notice :
                          v->pending ? "Ждём ход..." : !v->guest_id[0] ?
                          "Друг вышел" : plants && state.map == 5 ?
                          "Тяни кувшинку на воду" : plants ?
                          "Тяни растение на клетку" : "Тяни утку на ряд";
    label(root, 272, 75, 655, 42, message, 2, MUTED,
          LV_TEXT_ALIGN_LEFT);
    button(root, 930, 43, 150, 66, "Книга", 2, U_MATCH_BOOK);
    button(root, 1094, 43, 165, 66, "Выйти", 2, U_MATCH_EXIT);
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
        button(root, 15, 615, 217, 80, "Закончить", 2, U_MATCH_FINISH);
    if (state.winner) {
        lv_obj_t *shade = box(root, 0, 0, 1280, 720, 0, C(222222), 0);
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
        button(root, 410, 493, 460, 65,
               "Вернуться к комнатам", 2, U_MATCH_RETURN);
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
    if (phase == GAME_WORKSHOP || phase == GAME_WORKSHOP_DETAILS ||
        phase == GAME_WORKSHOP_EDIT) {
        h = mix(h, &workshop_draft_exists, sizeof workshop_draft_exists);
        h = mix(h, workshop_name, strlen(workshop_name));
        h = mix(h, workshop_description, strlen(workshop_description));
        h = mix(h, &workshop_object_count, sizeof workshop_object_count);
        h = mix(h, workshop_objects,
                (size_t)workshop_object_count * sizeof workshop_objects[0]);
        h = mix(h, workshop_selected_flags,
                (size_t)workshop_object_count * sizeof workshop_selected_flags[0]);
        h = mix(h, &workshop_clipboard_count, sizeof workshop_clipboard_count);
        h = mix(h, workshop_clipboard,
                (size_t)workshop_clipboard_count * sizeof workshop_clipboard[0]);
        h = mix(h, &workshop_player_selected, sizeof workshop_player_selected);
        h = mix(h, &workshop_goal_selected, sizeof workshop_goal_selected);
        h = mix(h, &workshop_ground_selected, sizeof workshop_ground_selected);
        h = mix(h, &workshop_selected, sizeof workshop_selected);
        h = mix(h, &workshop_tool, sizeof workshop_tool);
        h = mix(h, &workshop_palette_type, sizeof workshop_palette_type);
        h = mix(h, &workshop_block_variant, sizeof workshop_block_variant);
        h = mix(h, &workshop_trigger_kind, sizeof workshop_trigger_kind);
        h = mix(h, &workshop_camera_x, sizeof workshop_camera_x);
        h = mix(h, &workshop_camera_y, sizeof workshop_camera_y);
        h = mix(h, &workshop_ground_x, sizeof workshop_ground_x);
        h = mix(h, &workshop_ground_y, sizeof workshop_ground_y);
        h = mix(h, &workshop_ground_w, sizeof workshop_ground_w);
        h = mix(h, &workshop_ground_h, sizeof workshop_ground_h);
        h = mix(h, &workshop_ground_angle, sizeof workshop_ground_angle);
        h = mix(h, &workshop_player_x, sizeof workshop_player_x);
        h = mix(h, &workshop_player_y, sizeof workshop_player_y);
        h = mix(h, &workshop_player_w, sizeof workshop_player_w);
        h = mix(h, &workshop_player_h, sizeof workshop_player_h);
        h = mix(h, &workshop_player_angle, sizeof workshop_player_angle);
        h = mix(h, &workshop_goal_x, sizeof workshop_goal_x);
        h = mix(h, &workshop_goal_y, sizeof workshop_goal_y);
        h = mix(h, &workshop_goal_w, sizeof workshop_goal_w);
        h = mix(h, &workshop_goal_h, sizeof workshop_goal_h);
        h = mix(h, &workshop_goal_angle, sizeof workshop_goal_angle);
        h = mix(h, workshop_notice, strlen(workshop_notice));
        h = mix(h, &workshop_dialog, sizeof workshop_dialog);
        h = mix(h, &workshop_keyboard_active, sizeof workshop_keyboard_active);
        h = mix(h, &workshop_input_kind, sizeof workshop_input_kind);
        h = mix(h, &workshop_move_x, sizeof workshop_move_x);
        h = mix(h, &workshop_move_y, sizeof workshop_move_y);
        h = mix(h, &workshop_move_time, sizeof workshop_move_time);
        h = mix(h, &workshop_easing, sizeof workshop_easing);
        h = mix(h, &workshop_lock_x, sizeof workshop_lock_x);
        h = mix(h, &workshop_lock_y, sizeof workshop_lock_y);
        h = mix(h, &workshop_touch, sizeof workshop_touch);
        h = mix(h, &workshop_spawn, sizeof workshop_spawn);
        h = mix(h, &workshop_silent, sizeof workshop_silent);
        h = mix(h, &workshop_target_group, sizeof workshop_target_group);
        h = mix(h, &workshop_layer, sizeof workshop_layer);
        h = mix(h, &workshop_layer2, sizeof workshop_layer2);
        h = mix(h, &workshop_z_order, sizeof workshop_z_order);
        h = mix(h, &workshop_group_id, sizeof workshop_group_id);
        h = mix(h, &workshop_next_group_id, sizeof workshop_next_group_id);
        h = mix(h, &workshop_color_set, sizeof workshop_color_set);
        h = mix(h, &workshop_color_index, sizeof workshop_color_index);
        h = mix(h, &v->level_publish_busy, sizeof v->level_publish_busy);
        h = mix(h, v->level_publish_id, strlen(v->level_publish_id));
        h = mix(h, v->level_publish_notice, strlen(v->level_publish_notice));
    } else if (phase == GAME_ONLINE_ROOMS) {
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
    } else if (phase == GAME_CUSTOM_LEVELS) {
        h = mix(h, &v->level_count, sizeof v->level_count);
        h = mix(h, v->levels, (size_t)v->level_count * sizeof v->levels[0]);
        h = mix(h, &v->levels_busy, sizeof v->levels_busy);
        h = mix(h, v->levels_notice, strlen(v->levels_notice));
    } else if (phase == GAME_CUSTOM_PLAY) {
        h = mix(h, v->loaded_level.id, strlen(v->loaded_level.id));
        h = mix(h, v->loaded_level.title, strlen(v->loaded_level.title));
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
        if (phase == GAME_GARDEN) {
            h = mix(h, &state.garden_selection, sizeof state.garden_selection);
            h = mix(h, &state.garden_mode, sizeof state.garden_mode);
            h = mix(h, &state.garden_map, sizeof state.garden_map);
        }
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
        GameOfflineUIState state;
        game_offline_ui_snapshot(&state);
        int count = state.garden_mode ? 3 : 5;
        int offset = state.garden_mode ? 130 : 0;
        for (int i = 0; i < count; ++i)
            if (x >= 260 + offset + i * 130 &&
                x < 380 + offset + i * 130) return i;
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
    /* A flat, transparent drop target with a black outline; no glowing halo. */
    drag_target = box(root, 0, 0, ON_CELL_W, ON_CELL_H, 0, C(000000), 0);
    lv_obj_set_style_bg_opa(drag_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drag_target, 3, 0);
    lv_obj_set_style_border_color(drag_target, C(000000), 0);
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
    if (phase == GAME_GARDEN) {
        GameOfflineUIState state;
        game_offline_ui_snapshot(&state);
        if (game_book_entry(state.garden_mode, drag_index, &entry)) {
            if (state.garden_mode)
                duck_art(drag_ghost, 55, 55, 96, entry.enemy_variant);
            else
                art(drag_ghost, entry.art_id, 55, 55, 95);
        }
    } else if (game_book_entry(0, drag_index, &entry)) {
        art(drag_ghost, entry.art_id, 55, 55, 95);
    }
    drag_position(drag_x, drag_y);
}

static void drop_packet(int phase, int index, int x, int y) {
    if (!over_board(x, y) || phase != game_phase()) return;
    if (phase == GAME_GARDEN) {
        GameOfflineUIState state;
        game_offline_ui_snapshot(&state);
        game_input_press(320 + (state.garden_mode ? 130 : 0) + index * 130, 60);
    } else if (phase == GAME_PLAY)
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
    if (code >= U_WORKSHOP_LOCK_X_BASE &&
        code < U_WORKSHOP_LOCK_X_BASE + 4) {
        workshop_lock_x ^= 1 << (code - U_WORKSHOP_LOCK_X_BASE);
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_LOCK_Y_BASE &&
        code < U_WORKSHOP_LOCK_Y_BASE + 4) {
        workshop_lock_y ^= 1 << (code - U_WORKSHOP_LOCK_Y_BASE);
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_COLOR_SET_BASE &&
        code < U_WORKSHOP_COLOR_SET_BASE + 4) {
        workshop_color_set = code - U_WORKSHOP_COLOR_SET_BASE;
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_CELL_BASE &&
        code < U_WORKSHOP_CELL_BASE + WS_GRID_COLS * WS_GRID_ROWS) {
        workshop_cell_tap(code - U_WORKSHOP_CELL_BASE);
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_PALETTE_BASE &&
        code < U_WORKSHOP_PALETTE_BASE + 7) {
        static const int types[] = {ON_LEVEL_BLOCK, ON_LEVEL_HAZARD, ON_LEVEL_COIN,
            ON_LEVEL_ENEMY, ON_LEVEL_PLAYER, ON_LEVEL_GOAL, ON_LEVEL_TRIGGER};
        workshop_palette_type = types[code - U_WORKSHOP_PALETTE_BASE];
        if (workshop_palette_type == ON_LEVEL_BLOCK)
            workshop_block_variant = ON_LEVEL_BLOCK;
        workshop_tool = WS_TOOL_BUILD;
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_BLOCK_VARIANT_BASE &&
        code < U_WORKSHOP_BLOCK_VARIANT_BASE + 3) {
        static const int variants[] = {
            ON_LEVEL_BLOCK, ON_LEVEL_SLOPE, ON_LEVEL_GROUND
        };
        workshop_block_variant = variants[code - U_WORKSHOP_BLOCK_VARIANT_BASE];
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_TRIGGER_BASE && code < U_WORKSHOP_TRIGGER_BASE + 5) {
        workshop_trigger_kind = code - U_WORKSHOP_TRIGGER_BASE;
        if (workshop_dialog == WS_DIALOG_TRIGGER && workshop_selected >= 0 &&
            workshop_selected < workshop_object_count &&
            workshop_objects[workshop_selected].type == ON_LEVEL_TRIGGER)
            workshop_set_trigger_kind(&workshop_objects[workshop_selected],
                                      workshop_trigger_kind);
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_NUDGE_BASE && code < U_WORKSHOP_NUDGE_BASE + 4) {
        int direction = code - U_WORKSHOP_NUDGE_BASE;
        if (!workshop_selection_count()) {
            snprintf(workshop_notice, sizeof workshop_notice, "%s",
                     "Сначала выбери объект.");
        } else {
            float dx = direction == 0 ? -1.0f : direction == 3 ? 1.0f : 0.0f;
            float dy = direction == 1 ? -1.0f : direction == 2 ? 1.0f : 0.0f;
            workshop_move_selected(dx, dy);
            workshop_notice[0] = 0;
        }
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_PAN_BASE && code < U_WORKSHOP_PAN_BASE + 4) {
        int direction = code - U_WORKSHOP_PAN_BASE;
        if (direction == 0 && workshop_camera_x > -WS_WORLD_LIMIT) workshop_camera_x--;
        else if (direction == 1 && workshop_camera_y > -WS_WORLD_LIMIT) workshop_camera_y--;
        else if (direction == 2 && workshop_camera_y < WS_WORLD_LIMIT - WS_GRID_ROWS) workshop_camera_y++;
        else if (direction == 3 && workshop_camera_x < WS_WORLD_LIMIT - WS_GRID_COLS) workshop_camera_x++;
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_COLOR_BASE &&
        code < U_WORKSHOP_COLOR_BASE + 8) {
        workshop_color_index = code - U_WORKSHOP_COLOR_BASE;
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count) {
            workshop_objects[workshop_selected].color_set = workshop_color_set;
            workshop_objects[workshop_selected].color_index = workshop_color_index;
        }
        dirty = 1;return;
    }
    if (code >= U_WORKSHOP_LAYER_BASE &&
        code < U_WORKSHOP_LAYER_BASE + 10) {
        workshop_layer = code - U_WORKSHOP_LAYER_BASE;
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count)
            workshop_objects[workshop_selected].layer = workshop_layer;
        dirty = 1;return;
    }
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
    if (code >= U_CUSTOM_LEVEL_BASE && code < U_CUSTOM_LEVEL_BASE + 8) {
        int n = code - U_CUSTOM_LEVEL_BASE;
        if (visible_level_ids[n][0]) game_custom_level_request(visible_level_ids[n]);
        return;
    }
    if (code == U_CUSTOM_PREV) {if (custom_level_page > 0) custom_level_page--;dirty = 1;return;}
    if (code == U_CUSTOM_NEXT) {custom_level_page++;dirty = 1;return;}
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
    case U_MENU_WORKSHOP: case U_CUSTOM_WORKSHOP: game_workshop_open();break;
    case U_CUSTOM_CATALOG: custom_level_page = 0;game_custom_levels_open();break;
    case U_WORKSHOP_BACK: game_workshop_back();break;
    case U_WORKSHOP_NEW:
        workshop_reset_draft();game_workshop_open_details();break;
    case U_WORKSHOP_OPEN_DRAFT: game_workshop_open_details();break;
    case U_WORKSHOP_CATALOG:
        custom_level_page = 0;game_custom_levels_open();break;
    case U_WORKSHOP_DETAIL_EDIT: game_workshop_open_editor();break;
    case U_WORKSHOP_EDIT_TITLE:
        workshop_open_keyboard_input(WS_INPUT_TITLE);break;
    case U_WORKSHOP_EDIT_DESCRIPTION:
        workshop_open_keyboard_input(WS_INPUT_DESCRIPTION);break;
    case U_WORKSHOP_TRIGGER_GROUP_EDIT:
        workshop_open_keyboard_input(WS_INPUT_TRIGGER_GROUP);break;
    case U_WORKSHOP_TRIGGER_X_EDIT:
        workshop_open_keyboard_input(WS_INPUT_TRIGGER_X);break;
    case U_WORKSHOP_TRIGGER_Y_EDIT:
        workshop_open_keyboard_input(WS_INPUT_TRIGGER_Y);break;
    case U_WORKSHOP_TRIGGER_DURATION_EDIT:
        workshop_open_keyboard_input(WS_INPUT_TRIGGER_DURATION);break;
    case U_WORKSHOP_TRIGGER_EVENT_PREV:
    case U_WORKSHOP_TRIGGER_EVENT_NEXT:
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count &&
            workshop_objects[workshop_selected].type == ON_LEVEL_TRIGGER) {
            int step = code == U_WORKSHOP_TRIGGER_EVENT_NEXT ? 1 : 3;
            WorkshopObject *o = &workshop_objects[workshop_selected];
            o->trigger_event = (o->trigger_event + step) % 4;
        }
        dirty = 1;break;
    case U_WORKSHOP_TRIGGER_ACTION:
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count &&
            workshop_objects[workshop_selected].type == ON_LEVEL_TRIGGER) {
            WorkshopObject *o = &workshop_objects[workshop_selected];
            o->trigger_action = o->trigger_action == ON_TRIGGER_ACTIVATE ?
                                ON_TRIGGER_UNACTIVATE : ON_TRIGGER_ACTIVATE;
        }
        dirty = 1;break;
    case U_WORKSHOP_SAVE:
        workshop_draft_exists = 1;game_workshop_back();break;
    case U_WORKSHOP_PLAY: workshop_preview();break;
    case U_WORKSHOP_PUBLISH: {
        OnPublishedLevel level;workshop_build_preview(&level);
        workshop_draft_exists = 1;
        if (!on_net_level_publish(&level)) dirty = 1;
        break;
    }
    case U_WORKSHOP_BUILD:
        workshop_tool = WS_TOOL_BUILD;workshop_notice[0] = 0;dirty = 1;break;
    case U_WORKSHOP_EDITMODE:
        workshop_tool = WS_TOOL_EDIT;workshop_notice[0] = 0;dirty = 1;break;
    case U_WORKSHOP_MULTIMODE:
        workshop_tool = WS_TOOL_MULTI;workshop_notice[0] = 0;dirty = 1;break;
    case U_WORKSHOP_DELETE:
        workshop_tool = WS_TOOL_DELETE;workshop_notice[0] = 0;dirty = 1;break;
    case U_WORKSHOP_COPY:
        workshop_copy_selection();dirty = 1;break;
    case U_WORKSHOP_PASTE:
        workshop_paste_selection();dirty = 1;break;
    case U_WORKSHOP_DELETE_SELECTED:
        workshop_delete_selected();dirty = 1;break;
    case U_WORKSHOP_TRANSFORM_PANEL:
        if (!workshop_selection_count())
            snprintf(workshop_notice, sizeof workshop_notice, "%s",
                     "Сначала выбери объект для размера и угла.");
        else {workshop_dialog = WS_DIALOG_TRANSFORM;workshop_notice[0] = 0;}
        dirty = 1;break;
    case U_WORKSHOP_OBJECT_WIDTH_EDIT:
        workshop_open_keyboard_input(WS_INPUT_OBJECT_WIDTH);break;
    case U_WORKSHOP_OBJECT_HEIGHT_EDIT:
        workshop_open_keyboard_input(WS_INPUT_OBJECT_HEIGHT);break;
    case U_WORKSHOP_OBJECT_ANGLE_EDIT:
        workshop_open_keyboard_input(WS_INPUT_OBJECT_ANGLE);break;
    case U_WORKSHOP_MOVE_PANEL:
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count &&
            workshop_objects[workshop_selected].type == ON_LEVEL_TRIGGER) {
            workshop_dialog = WS_DIALOG_TRIGGER;
        } else {
            workshop_dialog = WS_DIALOG_MOVE;
            if (workshop_selected >= 0 && workshop_selected < workshop_object_count)
                workshop_target_group = workshop_objects[workshop_selected].group_id;
        }
        dirty = 1;break;
    case U_WORKSHOP_GROUP_PANEL:
        workshop_dialog = WS_DIALOG_GROUP;
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count) {
            WorkshopObject *o = &workshop_objects[workshop_selected];
            workshop_group_id = o->group_id;workshop_layer = o->layer;
            workshop_layer2 = o->layer2;workshop_z_order = o->z_order;
        }
        dirty = 1;break;
    case U_WORKSHOP_COLOR_PANEL:
        workshop_dialog = WS_DIALOG_COLOR;
        if (workshop_selected >= 0 && workshop_selected < workshop_object_count) {
            workshop_color_set = workshop_objects[workshop_selected].color_set;
            workshop_color_index = workshop_objects[workshop_selected].color_index;
        }
        dirty = 1;break;
    case U_WORKSHOP_DIALOG_OK:
        if (workshop_dialog == WS_DIALOG_MOVE &&
            workshop_selected >= 0 && workshop_selected < workshop_object_count) {
            WorkshopObject *o = &workshop_objects[workshop_selected];
            float moved_x = o->x + (workshop_lock_x ? 0 : workshop_move_x);
            float moved_y = o->y + (workshop_lock_y ? 0 : workshop_move_y);
            o->x = workshop_world_coord(moved_x, o->w);
            o->y = workshop_world_coord(moved_y, o->h);
        } else if (workshop_dialog == WS_DIALOG_GROUP &&
                   workshop_selected >= 0 && workshop_selected < workshop_object_count) {
            WorkshopObject *o = &workshop_objects[workshop_selected];
            o->group_id = workshop_group_id;o->layer = workshop_layer;
            o->layer2 = workshop_layer2;o->z_order = workshop_z_order;
            if (workshop_group_id > workshop_next_group_id)
                workshop_next_group_id = workshop_group_id;
        }
        workshop_dialog = WS_DIALOG_NONE;dirty = 1;break;
    case U_WORKSHOP_EASE_PREV: workshop_easing = (workshop_easing + 3) & 3;dirty = 1;break;
    case U_WORKSHOP_EASE_NEXT: workshop_easing = (workshop_easing + 1) & 3;dirty = 1;break;
    case U_WORKSHOP_SCALE_BASE:
        workshop_resize_selected(1, -.1f);dirty = 1;break;
    case U_WORKSHOP_SCALE_BASE + 1:
        workshop_resize_selected(1, .1f);dirty = 1;break;
    case U_WORKSHOP_SCALE_BASE + 2:
        workshop_resize_selected(0, -.1f);dirty = 1;break;
    case U_WORKSHOP_SCALE_BASE + 3:
        workshop_resize_selected(0, .1f);dirty = 1;break;
    case U_WORKSHOP_ROTATE_BASE:
        workshop_rotate_selected(-45.0f);dirty = 1;break;
    case U_WORKSHOP_ROTATE_BASE + 1:
        workshop_rotate_selected(45.0f);dirty = 1;break;
    case U_WORKSHOP_GROUP_DEC:
        if (workshop_dialog == WS_DIALOG_MOVE) {
            if (workshop_target_group > 0) workshop_target_group--;
        } else if (workshop_group_id > 0) workshop_group_id--;
        dirty = 1;break;
    case U_WORKSHOP_GROUP_INC:
        if (workshop_dialog == WS_DIALOG_MOVE) {
            if (workshop_target_group < 999) workshop_target_group++;
        } else if (workshop_group_id < 999) workshop_group_id++;
        dirty = 1;break;
    case U_WORKSHOP_LAYER_DEC: if (workshop_layer > 0) workshop_layer--;dirty = 1;break;
    case U_WORKSHOP_LAYER_INC: if (workshop_layer < 9) workshop_layer++;dirty = 1;break;
    case U_WORKSHOP_LAYER2_DEC: if (workshop_layer2 > 0) workshop_layer2--;dirty = 1;break;
    case U_WORKSHOP_LAYER2_INC: if (workshop_layer2 < 9) workshop_layer2++;dirty = 1;break;
    case U_WORKSHOP_Z_DEC: if (workshop_z_order > -99) workshop_z_order--;dirty = 1;break;
    case U_WORKSHOP_Z_INC: if (workshop_z_order < 99) workshop_z_order++;dirty = 1;break;
    case U_WORKSHOP_TOUCH: workshop_touch = !workshop_touch;dirty = 1;break;
    case U_WORKSHOP_SPAWN: workshop_spawn = !workshop_spawn;dirty = 1;break;
    case U_WORKSHOP_SILENT: workshop_silent = !workshop_silent;dirty = 1;break;
    case U_WORKSHOP_ADD_GROUP:
        if (workshop_group_id < 999) workshop_group_id++;
        dirty = 1;break;
    case U_MENU_BACK: game_input_press(1150, 55);break;
    case U_INTRO: game_input_press(640, 605);break;
    case U_MAP_LAWN: chosen_map = 1;dirty = 1;break;
    case U_MAP_WATER: chosen_map = 5;dirty = 1;break;
    case U_REFRESH: on_net_refresh();break;
    case U_CREATE: on_net_create(chosen_map);break;
    case U_CUSTOM_REFRESH: game_custom_levels_refresh();break;
    case U_CUSTOM_BACK: game_custom_level_exit();break;
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
    case U_GARDEN_PLANTS: game_input_press(60, 57);dirty = 1;break;
    case U_GARDEN_GEESE: game_input_press(180, 57);dirty = 1;break;
    case U_GARDEN_LAWN: game_input_press(60, 93);dirty = 1;break;
    case U_GARDEN_WATER: game_input_press(180, 93);dirty = 1;break;
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
    lv_obj_set_style_bg_color(screen, C(E2E2E2), 0);
    lv_obj_set_style_bg_opa(screen,
                            lvgl_ui_fullscreen(phase) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    if (phase == GAME_MENU) menu_screen(screen);
    else if (phase == GAME_SELECT) levels_screen(screen);
    else if (phase == GAME_WORKSHOP) workshop_home_screen(screen);
    else if (phase == GAME_WORKSHOP_DETAILS) workshop_details_screen(screen);
    else if (phase == GAME_WORKSHOP_EDIT) workshop_editor_screen(screen, net);
    else if (phase == GAME_CUSTOM_LEVELS) custom_levels_screen(screen, net);
    else if (phase == GAME_CUSTOM_PLAY) custom_platformer_screen(screen);
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
                           PV_ART_JUMPER, PV_ART_LILY, PV_ART_COIN,
                           PV_ART_LEVEL_BLOCK, PV_ART_LEVEL_PLATFORM,
                           PV_ART_LEVEL_TRIGGER, PV_ART_LEVEL_TRIGGER_ROTATE,
                           PV_ART_LEVEL_TRIGGER_FOREVER,
                           PV_ART_LEVEL_TRIGGER_INVISIBILITY,
                           PV_ART_LEVEL_TRIGGER_NO_COLLISION,
                           PV_ART_LEVEL_FLAG, PV_ART_LEVEL_SPIKE,
                           PV_ART_LEVEL_SLOPE};
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

#ifdef PVG3_LVGL_TEST
int lvgl_ui_test_art_loaded(int id) {
    return id >= 0 && id < PV_ART_COUNT && pictures[id].data != NULL;
}
int lvgl_ui_test_workshop_level(OnPublishedLevel *level) {
    if (!level) return 0;
    workshop_build_preview(level);
    return 1;
}
#endif

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
           phase == GAME_WORKSHOP || phase == GAME_WORKSHOP_DETAILS ||
           phase == GAME_WORKSHOP_EDIT || phase == GAME_BOOK ||
           phase == GAME_ONLINE_ROOMS || phase == GAME_ONLINE_LOBBY ||
           phase == GAME_CUSTOM_LEVELS;
}

static CustomTouch *custom_touch_find(int id) {
    for (int i = 0; i < CUSTOM_TOUCH_MAX; ++i)
        if (custom_touches[i].active && custom_touches[i].id == id)
            return &custom_touches[i];
    return NULL;
}

static CustomTouch *custom_touch_allocate(int id) {
    CustomTouch *touch = custom_touch_find(id);
    if (touch) return touch;
    for (int i = 0; i < CUSTOM_TOUCH_MAX; ++i) {
        if (custom_touches[i].active) continue;
        memset(&custom_touches[i], 0, sizeof custom_touches[i]);
        custom_touches[i].id = id;
        custom_touches[i].active = 1;
        return &custom_touches[i];
    }
    return NULL;
}

static void custom_controls_sync(void) {
    int left = 0, right = 0, jump = 0, trigger = 0;
    for (int i = 0; i < CUSTOM_TOUCH_MAX; ++i) {
        const CustomTouch *touch = &custom_touches[i];
        if (!touch->active) continue;
        left |= touch->axis < 0;
        right |= touch->axis > 0;
        jump |= touch->jump;
        trigger |= touch->trigger;
    }
    game_custom_control((int)right - (int)left, jump, trigger);
}

static void custom_controls_clear(void) {
    memset(custom_touches, 0, sizeof custom_touches);
    game_custom_control(0, 0, 0);
}

int lvgl_ui_touch_pointer(int pointer_id, int x, int y, int down) {
    if (!display || game_phase() != GAME_CUSTOM_PLAY) return 0;
    if (x < 0) x = 0;
    if (x >= GAME_W) x = GAME_W - 1;
    if (y < 0) y = 0;
    if (y >= GAME_H) y = GAME_H - 1;
    if (down && x >= 1030 && y <= 108) {
        custom_controls_clear();
        game_custom_level_exit();
        dirty = 1;
        return 1;
    }
    CustomTouch *touch = down ? custom_touch_allocate(pointer_id) :
                                 custom_touch_find(pointer_id);
    if (touch) {
        if (!down) memset(touch, 0, sizeof *touch);
        else {
            touch->axis = y >= 525 && y < 695 && x >= 58 && x < 158 ? -1 :
                          y >= 525 && y < 695 && x >= 178 && x < 278 ? 1 : 0;
            touch->jump = x >= 1081 && x < 1250 && y >= 525 && y < 695;
            touch->trigger = x >= 910 && x < 1038 && y >= 567 && y < 666;
        }
    }
    custom_controls_sync();
    return 1;
}

int lvgl_ui_pointer(int x, int y, int down) {
    if (!display) return 0;
    if (x < 0) x = 0;
    if (x >= GAME_W) x = GAME_W - 1;
    if (y < 0) y = 0;
    if (y >= GAME_H) y = GAME_H - 1;
    touch_x = x;touch_y = y;
    int phase = game_phase();
    if (phase == GAME_CUSTOM_PLAY) {
        if (down) {
            captured = pointer_down = 1;
            return lvgl_ui_touch_pointer(-1, x, y, 1);
        }
        int handled = captured;
        (void)lvgl_ui_touch_pointer(-1, x, y, 0);
        captured = pointer_down = 0;
        return handled;
    }
    if (down) {
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
    if (game_phase() == GAME_CUSTOM_PLAY && captured && pointer_down)
        return lvgl_ui_touch_pointer(-1, x, y, 1);
    if (drag_index >= 0) drag_position(x, y);
    else if (captured && pointer_down) lv_indev_read(pointer);
    return captured;
}

int lvgl_ui_cancel(void) {
    int handled = captured;
    if (game_phase() == GAME_CUSTOM_PLAY) custom_controls_clear();
    else memset(custom_touches, 0, sizeof custom_touches);
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
        phase == GAME_ONLINE_MATCH || phase == GAME_CUSTOM_LEVELS ||
        phase == GAME_CUSTOM_PLAY || phase == GAME_WORKSHOP_EDIT) on_net_view(&v);
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
