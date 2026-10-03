/* Deterministic C-only Firebase REST emulator for the actual native game.
 * Verifies UI -> async task queue -> protocol -> Firebase tree -> snapshots,
 * on both host and guest paths, without contacting anyone's real rooms. */
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "online_net.h"
#ifdef PVG3_LVGL_TEST
#include "lvgl_ui.h"
#include "game_view.h"
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct {
    int present, map, host_role, guest_role;
    char room_id[ON_ROOM_ID_SIZE], host_id[ON_PLAYER_ID_SIZE];
    char guest_id[ON_PLAYER_ID_SIZE];
    int64_t host_ping, guest_ping;
    char state[ON_STATE_JSON_CAP], command[256];
} db;
static const char *FAKE_GUEST = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char *FAKE_HOST = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *TEST_LEVEL_INDEX =
    "{\"104\":{\"id\":\"104\",\"title\":\"Невероятное приключение через тайный мост к финишу\","
    "\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\",\"updatedAt\":1}}";
static const char *TEST_LEVEL =
    "{\"format\":\"PVG3-PUBLISHED-LEVEL\",\"version\":1,\"id\":\"104\","
    "\"title\":\"Невероятное приключение через тайный мост к финишу\","
    "\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\","
    "\"project\":{\"format\":\"PVG3-MAKER\","
    "\"version\":1,\"title\":\"Невероятное приключение через тайный мост к финишу\",\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\",\"levelId\":\"104\","
    "\"templateId\":\"classic\",\"width\":16,\"height\":10,\"objects\":["
    "{\"id\":1,\"type\":\"ground\",\"name\":\"Ground\",\"x\":0,\"y\":9,"
    "\"w\":16,\"h\":1,\"angle\":0,\"color\":\"#64844c\",\"number\":0,\"visible\":true},"
    "{\"id\":2,\"type\":\"player\",\"name\":\"Start\",\"x\":1,\"y\":8.12,"
    "\"w\":0.65,\"h\":0.85,\"angle\":0,\"color\":\"#7db9dd\",\"number\":0,\"visible\":true},"
    "{\"id\":3,\"type\":\"goal\",\"name\":\"Goal\",\"x\":14.7,\"y\":7.8,"
    "\"w\":0.8,\"h\":1.2,\"angle\":0,\"color\":\"#e8cf77\",\"number\":0,\"visible\":true}]},\"updatedAt\":1}";

static int64_t epoch_ms(void) {
    struct timespec ts;clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void pause_ms(int ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}
static const char *role_name(int role) {
    return role == ON_ROLE_PLANTS ? "\"plants\"" :
           role == ON_ROLE_ZOMBIES ? "\"zombies\"" : "null";
}
static char room_body[ON_STATE_JSON_CAP + 1024];
static const char *build_room(void) {
    if (!db.present) return "null";
    int n = snprintf(room_body, sizeof room_body,
        "{\"version\":1,\"map\":%d,\"host\":{\"id\":\"%s\","
        "\"ping\":%" PRId64 ",\"role\":%s},\"guest\":%s",
        db.map, db.host_id, db.host_ping, role_name(db.host_role),
        db.guest_id[0] ? "{" : "null");
    assert(n > 0 && (size_t)n < sizeof room_body);
    if (db.guest_id[0]) {
        n += snprintf(room_body + n, sizeof room_body - (size_t)n,
           "\"id\":\"%s\",\"ping\":%" PRId64 ",\"role\":%s}",
           db.guest_id, db.guest_ping, role_name(db.guest_role));
        assert(n > 0 && (size_t)n < sizeof room_body);
    }
    n += snprintf(room_body + n, sizeof room_body - (size_t)n,
        ",\"state\":%s,\"command\":%s}",
        db.state[0] ? db.state : "null", db.command[0] ? db.command : "null");
    assert(n > 0 && (size_t)n < sizeof room_body);
    return room_body;
}
static int answer(char *response, size_t cap, const char *text, int status) {
    size_t length = strlen(text);
    if (!response || length >= cap) return -2;
    memcpy(response, text, length + 1);
    return status;
}
int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t cap) {
    if (!strcmp(path, "levels-index.json") && !strcmp(method, "GET"))
        return answer(response, cap, TEST_LEVEL_INDEX, 200);
    if (!strcmp(path, "levels/104.json") && !strcmp(method, "GET"))
        return answer(response, cap, TEST_LEVEL, 200);
    if (!strcmp(path, "rooms.json") && !strcmp(method, "GET")) {
        if (!db.present) return answer(response, cap, "null", 200);
        char all[ON_STATE_JSON_CAP + 1050];
        int n = snprintf(all, sizeof all, "{\"%s\":%s}", db.room_id, build_room());
        assert(n > 0 && (size_t)n < sizeof all);
        return answer(response, cap, all, 200);
    }
    if (strncmp(path, "rooms/", 6) || strlen(path) < 17 ||
        (path[12] != '/' && path[12] != '.')) return -1;
    char id[ON_ROOM_ID_SIZE] = {0};memcpy(id, path + 6, 6);
    if (!on_protocol_valid_room_id(id)) return -1;
    const char *sub = path + 12;
    if (!strcmp(sub, ".json") && !strcmp(method, "PUT")) {
        if (if_match && !strcmp(if_match, "null_etag") && db.present)
            return answer(response, cap, "null", 412);
        OnRoomData r;
        if (!body || !on_protocol_room(body, &r) || !r.present)
            return answer(response, cap, "null", 400);
        memset(&db, 0, sizeof db);
        db.present = 1;db.map = r.map;db.host_ping = r.host_ping;
        strcpy(db.room_id, id);strcpy(db.host_id, r.host_id);
        return answer(response, cap, body, 200);
    }
    if (!db.present || strcmp(id, db.room_id))
        return answer(response, cap, "null", !strcmp(method, "GET") ? 200 : 404);
    if (!strcmp(sub, ".json")) {
        if (!strcmp(method, "GET")) return answer(response, cap, build_room(), 200);
        if (!strcmp(method, "DELETE")) {
            db.present = 0;
            return answer(response, cap, "null", 200);
        }
    }
    if (!strcmp(sub, "/guest.json")) {
        if (!strcmp(method, "PUT")) {
            if (if_match && !strcmp(if_match, "null_etag") && db.guest_id[0])
                return answer(response, cap, "null", 412);
            char guest[ON_PLAYER_ID_SIZE] = {0};int64_t ping;
            int ok = sscanf(body, "{\"id\":\"%32[0-9a-f]\",\"ping\":%" SCNd64 "}",
                            guest, &ping);
            if (ok != 2 || !on_protocol_valid_player_id(guest)) return -1;
            strcpy(db.guest_id, guest);db.guest_ping = ping;
            return answer(response, cap, body, 200);
        }
        if (!strcmp(method, "DELETE")) {
            db.guest_id[0] = 0;db.guest_role = 0;
            return answer(response, cap, "null", 200);
        }
    }
    if (!strcmp(sub, "/host/role.json") && !strcmp(method, "PUT")) {
        db.host_role = !strcmp(body, "\"plants\"") ? ON_ROLE_PLANTS : ON_ROLE_ZOMBIES;
        return answer(response, cap, body, 200);
    }
    if (!strcmp(sub, "/guest/role.json") && !strcmp(method, "PUT")) {
        db.guest_role = !strcmp(body, "\"plants\"") ? ON_ROLE_PLANTS : ON_ROLE_ZOMBIES;
        return answer(response, cap, body, 200);
    }
    if (!strcmp(sub, "/state.json") && !strcmp(method, "PUT")) {
        assert(strlen(body) < sizeof db.state);
        OnMatch m;assert(on_protocol_match(body, &m));
        strcpy(db.state, body);
        return answer(response, cap, body, 200);
    }
    if (!strcmp(sub, "/command.json") && !strcmp(method, "PUT")) {
        assert(strlen(body) < sizeof db.command);
        strcpy(db.command, body);
        return answer(response, cap, body, 200);
    }
    if (!strcmp(sub, "/host/ping.json") && !strcmp(method, "PUT")) {
        db.host_ping = strtoll(body, NULL, 10);return answer(response, cap, body, 200);
    }
    if (!strcmp(sub, "/guest/ping.json") && !strcmp(method, "PUT")) {
        db.guest_ping = strtoll(body, NULL, 10);return answer(response, cap, body, 200);
    }
    return -1;
}
static OnNetView view(void) {OnNetView v = {0};on_net_view(&v);return v;}
static void tick_pump(int n) {
    for (int i = 0; i < n; i++) {
        on_net_pump_once();game_tick(.016f, NULL);
        pause_ms(30);
    }
}
static void wait_poll(void) {
    /* Poll interval is deliberately throttled; manual test advances time. */
    pause_ms(710);
    tick_pump(3);
}
static void expect_state(OnMatch *out) {
    assert(db.state[0]);
    assert(on_protocol_match(db.state, out));
}
static void isolate_saves(uint8_t *before, uint8_t *after, size_t size) {
    assert(game_save_export(after, size) && !memcmp(before, after, size));
}
/* Optional local visual check: PVG3_ONLINE_SHOTS=1 ./native_online_integration_test */
static void screenshot(const char *name) {
    if (!getenv("PVG3_ONLINE_SHOTS")) return;
    static uint32_t pixels[GAME_W * GAME_H];
    char path[80];snprintf(path, sizeof path, "shots/%s.ppm", name);
    FILE *f = fopen(path, "wb");assert(f);
    game_tick(0, pixels);
    fprintf(f, "P6\n%d %d\n255\n", GAME_W, GAME_H);
    for (int i = 0; i < GAME_W * GAME_H; i++) {
        uint32_t p = pixels[i];
        fputc(p & 255, f);fputc((p >> 8) & 255, f);
        fputc((p >> 16) & 255, f);
    }
    assert(!fclose(f));
}
#ifdef PVG3_LVGL_TEST
/* Exercises actual LVGL pointer events and the existing in-memory Firebase.
 * Also writes screenshots if PVG3_LVGL_SHOTS=1; never touches the live DB. */
static uint32_t ui_pixels[GAME_W * GAME_H];
static void ui_snapshot(const char *name) {
    game_tick(0, lvgl_ui_fullscreen(game_phase()) ? NULL : ui_pixels);
    lvgl_ui_frame(.050f, ui_pixels);
    if (!getenv("PVG3_LVGL_SHOTS")) return;
    char path[100];snprintf(path, sizeof path, "shots/lvgl_%s.ppm", name);
    FILE *f = fopen(path, "wb");assert(f);
    fprintf(f, "P6\n%d %d\n255\n", GAME_W, GAME_H);
    for (int i = 0; i < GAME_W * GAME_H; ++i) {
        uint32_t pixel = ui_pixels[i];
        fputc(pixel & 255, f);fputc((pixel >> 8) & 255, f);
        fputc((pixel >> 16) & 255, f);
    }
    assert(!fclose(f));
}
static void ui_tap(int x, int y) {
    assert(lvgl_ui_pointer(x, y, 1));ui_snapshot("tap_down");
    assert(lvgl_ui_pointer(x, y, 0));ui_snapshot("tap_up");
}
static void ui_quick_tap(int x, int y) {
    /* Android may deliver DOWN+UP before one render frame; never miss a tap. */
    assert(lvgl_ui_pointer(x, y, 1));
    assert(lvgl_ui_pointer(x, y, 0));
    ui_snapshot("quick_tap");
}
static void ui_board_tap(int x, int y) {
    assert(!lvgl_ui_pointer(x, y, 1));
    game_input_press(x, y);ui_snapshot("board_down");
    assert(!lvgl_ui_pointer(x, y, 0));
    game_input_release(x, y);ui_snapshot("board_up");
}
static void ui_drag(int x0, int y0, int x1, int y1,
                    const char *hover_shot) {
    assert(lvgl_ui_pointer(x0, y0, 1));ui_snapshot("drag_start");
    assert(lvgl_ui_move((x0 + x1) / 2, (y0 + y1) / 2));
    ui_snapshot("drag_move");
    assert(lvgl_ui_move(x1, y1));ui_snapshot(hover_shot);
    assert(lvgl_ui_pointer(x1, y1, 0));ui_snapshot("drag_drop");
}
static int run_lvgl_test(void) {
    static uint8_t before[20000], after[20000];
    size_t bytes = game_save_size();assert(bytes < sizeof before);
    game_init();assert(game_save_export(before, bytes));
    assert(lvgl_ui_init());
    game_set_lvgl_ui(1);
    ui_snapshot("menu");
    ui_tap(557, 82);assert(game_phase() == GAME_CUSTOM_LEVELS);
    tick_pump(2);ui_snapshot("custom_levels");
    OnNetView catalog = view();
    assert(catalog.level_count == 1 && !strcmp(catalog.levels[0].id, "104") &&
           !strcmp(catalog.levels[0].title,
                   "Невероятное приключение через тайный мост к финишу") &&
           !strcmp(catalog.levels[0].description,
                   "Найди скрытый мост и монеты, затем доберись до финиша по платформам."));
    ui_tap(185, 318);tick_pump(3);
    assert(game_phase() == GAME_CUSTOM_PLAY);
    float custom_x = game_debug_custom_player_x();
    assert(lvgl_ui_pointer(230, 591, 1));
    for (int i = 0; i < 8; ++i) game_tick(.05f, NULL);
    assert(lvgl_ui_pointer(230, 591, 0));
    assert(game_debug_custom_player_x() > custom_x + 50);
    ui_tap(1140, 55);assert(game_phase() == GAME_CUSTOM_LEVELS);
    ui_snapshot("custom_level_return");
    ui_tap(1150, 76);assert(game_phase() == GAME_MENU);
    ui_snapshot("menu_after_custom");
    ui_tap(1090, 79);assert(game_phase() == GAME_GARDEN);
    ui_snapshot("garden");
    /* Unoccupied soil/wood must be pixel-for-pixel from the author's PNG,
     * not the old checkerboard, fake-green wash or a hidden wooden path. */
    int w, h;
    const uint32_t *map = game_art_rgba(PV_ART_LAWN, &w, &h);
    assert(map && w == 500 && h == 500);
    int wood_x = 190, soil_x = 1100, clear_y = 625;
    assert(ui_pixels[clear_y * GAME_W + wood_x] ==
           map[(clear_y * h / GAME_H) * w + wood_x * (w / 2) / 250]);
    assert(ui_pixels[clear_y * GAME_W + soil_x] ==
           map[(clear_y * h / GAME_H) * w + w / 2 +
               (soil_x - 250) * (w - w / 2) / (GAME_W - 250)]);
    uint8_t garden[GAME_GARDEN_CELLS], garden_after[GAME_GARDEN_CELLS];
    ui_tap(320, 55);ui_board_tap(424, 176); /* no more tap -> tap planting */
    game_garden_export(garden);assert(garden[1] == 0);
    ui_drag(320, 55, 424, 176, "garden_hover");
    game_garden_export(garden);assert(garden[1] == 1);
    ui_board_tap(535, 176); /* garden drag does not leave a seed selected */
    game_garden_export(garden);assert(garden[2] == 0);
    ui_tap(989, 44);assert(game_phase() == GAME_BOOK);
    ui_snapshot("book_plants");
    ui_tap(430, 220);ui_snapshot("book_enemies");
    ui_tap(225, 420);
    GameOfflineUIState offline;
    game_offline_ui_snapshot(&offline);
    assert(offline.book_enemy_tab == 1 && offline.book_selection == 2);
    ui_snapshot("book_bucket");
    ui_tap(225, 525);ui_snapshot("book_robot");
    ui_tap(1150, 76);assert(game_phase() == GAME_GARDEN);
    ui_tap(1150, 44);assert(game_phase() == GAME_MENU);
    assert(lvgl_ui_pointer(829, 78, 1));
    assert(lvgl_ui_cancel());ui_snapshot("cancel_menu_button");
    assert(game_phase() == GAME_MENU);
    ui_quick_tap(829, 78);assert(game_phase() == GAME_SELECT);
    ui_snapshot("levels");
    ui_tap(640, 620);assert(game_phase() == GAME_INTRO);
    ui_snapshot("intro_bread");
    ui_tap(640, 402);ui_tap(640, 402);
    game_offline_ui_snapshot(&offline);assert(offline.intro_step == 2);
    ui_snapshot("intro_kirill");
    ui_tap(1130, 76);assert(game_phase() == GAME_SELECT);
    ui_tap(1070, 270);assert(game_phase() == GAME_PLAY && game_level() == 5);
    ui_snapshot("offline_water");
    const uint32_t *water_map = game_art_rgba(PV_ART_WATER, &w, &h);
    assert(water_map && w == 500 && h == 500);
    int canal_x = 980, canal_y = 300;
    int src_canal_y = h / 5 + (canal_y - 232) * (h * 27 / 50 - h / 5) / 224;
    int src_canal_x = w / 2 + (canal_x - 250) * (w - w / 2) / (GAME_W - 250);
    assert(ui_pixels[canal_y * GAME_W + canal_x] ==
           water_map[src_canal_y * w + src_canal_x]);
    /* The lily illustration must survive recharge; no black/blank packet. */
    uint32_t lily_icon_pixel = ui_pixels[600 * GAME_W + 50];
    assert(((lily_icon_pixel >> 8) & 255u) > 180u);
    int initial_coins = game_debug_coin_balance();
    ui_tap(120, 610);ui_board_tap(535, 288);
    assert(!game_debug_lily_at(1, 2) && game_debug_coin_balance() == initial_coins);
    ui_drag(120, 610, 535, 288, "offline_lily_hover");
    assert(game_debug_lily_at(1, 2) && game_debug_coin_balance() == initial_coins - 25);
    assert(game_debug_cooldown(4) > 0);ui_snapshot("offline_lily_cooldown");
    assert(ui_pixels[600 * GAME_W + 50] == lily_icon_pixel);
    /* Cooldown leaves the drawing visible, refuses touches, has no seconds. */
    ui_tap(120, 610);ui_board_tap(650, 288);
    assert(!game_debug_lily_at(1, 3) && game_debug_coin_balance() == initial_coins - 25);
    ui_drag(120, 180, 650, 288, "offline_invalid_hover"); /* needs a lily */
    assert(game_debug_plant_type(1, 3) == -1 &&
           game_debug_coin_balance() == initial_coins - 25);
    ui_drag(120, 180, 535, 288, "offline_pea_hover");
    assert(game_debug_plant_type(1, 2) == 0 &&
           game_debug_coin_balance() == initial_coins - 125);
    ui_snapshot("offline_dragged_pea");
    /* CANCEL after touching another packet cannot accidentally plant it. */
    assert(lvgl_ui_pointer(120, 287, 1));ui_snapshot("drag_cancel_start");
    assert(lvgl_ui_move(651, 178));assert(lvgl_ui_cancel());
    ui_snapshot("drag_cancelled");
    assert(game_debug_plant_type(0, 3) == -1);
    ui_tap(1160, 73);assert(game_phase() == GAME_MENU);
    /* Online interaction must not modify the offline campaign or garden. */
    assert(game_save_export(before, bytes));
    ui_tap(1030, 611);assert(game_phase() == GAME_ONLINE_ROOMS);
    tick_pump(1);ui_snapshot("rooms_empty");
    /* Catalog browsing and custom levels remain separate from online rooms. */
    ui_tap(727, 210);ui_snapshot("search");
    ui_tap(307, 313);ui_snapshot("search_typed");
    ui_tap(948, 234);ui_tap(948, 137);
    ui_tap(384, 219);ui_tap(1060, 207);
    tick_pump(2);assert(db.present && db.map == 5);
    assert(game_phase() == GAME_ONLINE_LOBBY);
    ui_snapshot("lobby_wait");
    strcpy(db.guest_id, FAKE_GUEST);
    db.guest_ping = epoch_ms();db.guest_role = ON_ROLE_ZOMBIES;
    wait_poll();ui_snapshot("lobby_ready");
    ui_tap(352, 438);tick_pump(3);
    assert(game_phase() == GAME_ONLINE_MATCH && db.host_role == ON_ROLE_PLANTS);
    ui_snapshot("match_plants");
    ui_tap(120, 580);ui_board_tap(535, 288);
    OnMatch match;int role;
    game_online_ui_snapshot(&match, &role, NULL, NULL, 0, NULL);
    assert(!match.lilies[1 * ON_COLS + 2] && match.plant_cash == 250);
    ui_drag(120, 580, 535, 288, "online_lily_hover");tick_pump(2);
    game_online_ui_snapshot(&match, &role, NULL, NULL, 0, NULL);
    assert(role == ON_ROLE_PLANTS && match.lilies[1 * ON_COLS + 2] &&
           match.plant_cash == 225 && match.plant_cooldown[ON_LILY] > 0);
    ui_snapshot("match_lily_cooldown");
    ui_tap(1141, 75);tick_pump(2);
    assert(game_phase() == GAME_ONLINE_ROOMS && !db.present);
    /* List join remains distinct from room creation and search. */
    db.present = 1;db.map = 1;db.host_ping = epoch_ms();
    strcpy(db.room_id, "ABCDEF");strcpy(db.host_id, FAKE_HOST);
    db.host_role = ON_ROLE_PLANTS;db.guest_id[0] = 0;
    db.guest_role = 0;db.state[0] = db.command[0] = 0;
    on_net_refresh();tick_pump(3);
    assert(view().room_count == 1);
    ui_snapshot("rooms_list");
    ui_tap(163, 378);tick_pump(2);
    assert(game_phase() == GAME_ONLINE_LOBBY && view().slot == ON_SLOT_GUEST);
    ui_snapshot("lobby_joined");
    ui_tap(925, 430);tick_pump(2);
    assert(db.guest_role == ON_ROLE_ZOMBIES);
    ui_snapshot("lobby_guest");
    OnMatch hosted;on_match_new(&hosted, 1);
    assert(on_protocol_match_json(&hosted, db.state, sizeof db.state));
    wait_poll();tick_pump(2);
    assert(game_phase() == GAME_ONLINE_MATCH);
    ui_snapshot("match_zombies");
    ui_drag(95, 200, 913, 187, "online_zombie_hover");tick_pump(2);
    assert(strstr(db.command, "\"kind\":\"spawn\"") &&
           strstr(db.command, "\"seq\":1"));
    ui_tap(1150, 75);tick_pump(2);
    assert(game_phase() == GAME_ONLINE_ROOMS && !db.guest_id[0]);
    /* The fake host ends the previous match before the second join-by-code. */
    db.state[0] = db.command[0] = 0;
    db.host_ping = epoch_ms();
    on_net_refresh();tick_pump(2);
    ui_snapshot("rooms_after_guest");
    ui_tap(727, 210);
    /* Search is separate from room creation actions. */
    for (int i = 0; i < 6; ++i) ui_tap(316 + i * 78, 314);
    ui_snapshot("search_complete");
    ui_tap(640, 625);tick_pump(2);
    assert(game_phase() == GAME_ONLINE_LOBBY && view().slot == ON_SLOT_GUEST);
    ui_snapshot("lobby_by_code");
    ui_tap(1150, 75);tick_pump(2);
    ui_tap(1140, 77);
    assert(game_phase() == GAME_MENU);
    isolate_saves(before, after, bytes);
    game_garden_export(garden_after);
    assert(!memcmp(garden, garden_after, sizeof garden));
    lvgl_ui_shutdown();
    game_set_lvgl_ui(0);
    on_net_shutdown();
    puts("Legacy and LVGL UI, native published-level list/play and joystick, drag planting, both online roles and saves passed");
    return 0;
}
#endif

int main(void) {
#ifdef PVG3_LVGL_TEST
    if (getenv("PVG3_LVGL_TEST")) return run_lvgl_test();
#endif
    static uint8_t before[20000], after[20000];
    size_t size = game_save_size();assert(size <= sizeof before);
    game_init();assert(game_save_export(before, size));
    game_input_press(1058, 615);
    assert(game_phase() == GAME_ONLINE_ROOMS);
    tick_pump(1);assert(view().room_count == 0);
    game_input_press(400, 175); /* water option */
    game_input_press(975, 175); /* '+' creates the room directly */
    tick_pump(2);
    assert(game_phase() == GAME_ONLINE_LOBBY);
    screenshot("online_lobby");
    OnNetView v = view();
    assert(v.slot == ON_SLOT_HOST && db.present && db.map == 5 &&
           !strcmp(v.room_id, db.room_id) && v.room_id[0]);
    strcpy(db.guest_id, FAKE_GUEST);
    db.guest_ping = epoch_ms();db.guest_role = ON_ROLE_ZOMBIES;
    wait_poll();
    game_input_press(400, 385); /* choose plants on the illustrated role card */
    tick_pump(3);
    assert(db.host_role == ON_ROLE_PLANTS);
    assert(game_phase() == GAME_ONLINE_MATCH);
    screenshot("online_match_plants");
    tick_pump(4); /* publish first match snapshot */
    OnMatch match;expect_state(&match);
    assert(match.map == 5 && match.plant_cash == 250);
    game_input_press(120, 591); /* lily on the author's water map */
    game_input_press(535, 288);
    tick_pump(3);pause_ms(430);tick_pump(3);
    expect_state(&match);
    assert(match.lilies[1 * ON_COLS + 2] && match.plant_cash == 225);
    game_input_press(120, 368); /* Kirill's sunflower produces coins */
    game_input_press(307, 176); /* land, column 0 */
    for (int i = 0; i < 107; i++) game_tick(.05f, NULL);
    pause_ms(430);tick_pump(4);
    expect_state(&match);
    assert(match.plants[0].type == ON_SUNFLOWER && match.coin_count == 1 &&
           match.plant_cash == 175);
    game_input_press((int)match.coins[0].x, (int)match.coins[0].y);
    pause_ms(430);tick_pump(4);
    expect_state(&match);
    assert(match.coin_count == 0 && match.plant_cash == 200);
    OnCommand guest_spawn = {.kind=ON_CMD_SPAWN,.row=1,.type=ON_CONE,.seq=1};
    assert(on_protocol_command_json(&guest_spawn, FAKE_GUEST,
                                    db.command, sizeof db.command));
    wait_poll();
    game_tick(.016f, NULL); /* host authenticates/ACKs guest command */
    pause_ms(430);tick_pump(4);
    expect_state(&match);
    assert(match.ack_guest == 1 && match.duck_count == 1 &&
           match.ducks[0].type == ON_CONE && match.ducks[0].max_hp == 420);
    game_input_press(1151, 49); /* leave, room owner deletes only own room */
    tick_pump(2);
    assert(game_phase() == GAME_ONLINE_ROOMS && !db.present);
    game_input_press(1140, 50); /* return to offline campaign */
    assert(game_phase() == GAME_MENU);
    isolate_saves(before, after, size);

    game_init();assert(game_save_export(before, size));
    memset(&db, 0, sizeof db);
    db.present = 1;db.map = 1;db.host_ping = epoch_ms();
    strcpy(db.room_id, "ABCDEF");strcpy(db.host_id, FAKE_HOST);
    db.host_role = ON_ROLE_PLANTS;
    game_input_press(1058, 615);
    tick_pump(1);assert(view().room_count == 1);
    game_input_press(140, 343); /* first available room, no search required */
    tick_pump(2);
    assert(game_phase() == GAME_ONLINE_LOBBY);
    v = view();assert(v.slot == ON_SLOT_GUEST &&
                      !strcmp(db.guest_id, v.guest_id));
    game_input_press(880, 360); /* guest chooses the duck/zombie side */
    tick_pump(2);assert(db.guest_role == ON_ROLE_ZOMBIES);
    OnMatch hosted;on_match_new(&hosted, 1);
    assert(on_protocol_match_json(&hosted, db.state, sizeof db.state));
    wait_poll();
    assert(game_phase() == GAME_ONLINE_MATCH);
    screenshot("online_match_zombies");
    game_input_press(95, 200); /* standard duck card */
    game_input_press(913, 187); /* row 0 */
    tick_pump(2);
    assert(strstr(db.command, "\"kind\":\"spawn\"") &&
           strstr(db.command, "\"seq\":1") &&
           strstr(db.command, db.guest_id));
    v = view();assert(v.pending);
    OnCommand ack = {.kind=ON_CMD_SPAWN,.row=0,.type=ON_DUCK};
    assert(on_match_apply(&hosted, ON_ROLE_ZOMBIES, &ack));
    hosted.ack_guest = 1;
    assert(on_protocol_match_json(&hosted, db.state, sizeof db.state));
    wait_poll();
    v = view();assert(!v.pending && v.state.ack_guest == 1 &&
                       v.state.duck_count == 1);
    game_input_press(1151, 49);tick_pump(2);
    assert(game_phase() == GAME_ONLINE_ROOMS && db.present && !db.guest_id[0]);
    game_input_press(1140, 50);
    isolate_saves(before, after, size);

    /* Reverse roles: the APK guest can also control plants and collect a
     * sunflower coin. coinId must not overwrite the guest player id. */
    game_init();assert(game_save_export(before, size));
    memset(&db, 0, sizeof db);
    db.present = 1;db.map = 1;db.host_ping = epoch_ms();
    strcpy(db.room_id, "FEDCBA");strcpy(db.host_id, FAKE_HOST);
    db.host_role = ON_ROLE_ZOMBIES;
    game_input_press(1058, 615);tick_pump(1);
    game_input_press(140, 343);tick_pump(2);
    assert(game_phase() == GAME_ONLINE_LOBBY);
    game_input_press(400, 385);tick_pump(2);
    assert(db.guest_role == ON_ROLE_PLANTS);
    on_match_new(&hosted, 1);
    hosted.coin_count = 1;
    hosted.coins[0] = (OnCoin){.id=42,.x=333,.y=204,.life=16};
    hosted.next_id = 43;
    assert(on_protocol_match_json(&hosted, db.state, sizeof db.state));
    wait_poll();assert(game_phase() == GAME_ONLINE_MATCH);
    game_input_press(333, 204);tick_pump(2);
    assert(strstr(db.command, "\"kind\":\"coin\"") &&
           strstr(db.command, "\"coinId\":42") &&
           strstr(db.command, db.guest_id));
    ack = (OnCommand){.kind=ON_CMD_COIN,.id=42};
    assert(on_match_apply(&hosted, ON_ROLE_PLANTS, &ack));
    hosted.ack_guest = 1;
    assert(on_protocol_match_json(&hosted, db.state, sizeof db.state));
    wait_poll();
    v = view();assert(!v.pending && v.state.plant_cash == 275 &&
                       v.state.coin_count == 0);
    game_input_press(1151, 49);tick_pump(2);
    game_input_press(1140, 50);isolate_saves(before, after, size);
    on_net_shutdown();
    puts("Native Firebase REST host/guest, both roles, coins, ACK and offline saves passed");
    return 0;
}
