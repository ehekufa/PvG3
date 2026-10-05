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
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PVG3_LVGL_TEST
#undef assert
#define assert(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "LVGL test assertion failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #expression); \
        fflush(stderr);abort(); \
    } \
} while (0)
#endif

static struct {
    int present, map, host_role, guest_role;
    char room_id[ON_ROOM_ID_SIZE], host_id[ON_PLAYER_ID_SIZE];
    char guest_id[ON_PLAYER_ID_SIZE];
    int64_t host_ping, guest_ping;
    char state[ON_STATE_JSON_CAP], command[256];
} db;
static char uploaded_level_id[ON_LEVEL_ID_SIZE];
static char uploaded_level_body[ON_LEVEL_JSON_CAP];
static OnPublishedLevel fake_level_record;
static char uploaded_index_body[1024];
static const char *FAKE_GUEST = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char *FAKE_HOST = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *TEST_LEVEL_INDEX =
    "{\"104\":{\"id\":\"104\",\"title\":\"Невероятное приключение через тайный мост к финишу\","
    "\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\",\"updatedAt\":1},"
    "\"338069\":{\"id\":\"338069\",\"title\":\"Официальный уровень\","
    "\"description\":\"Авторский уровень PvG3.\",\"updatedAt\":2}}";
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
static int level_child_id(const char *path, const char *root,
                          char id[ON_LEVEL_ID_SIZE]) {
    char prefix[32];snprintf(prefix, sizeof prefix, "%s/", root);
    size_t n = strlen(prefix), length = strlen(path);
    if (strncmp(path, prefix, n) || length <= n + 5 ||
        strcmp(path + length - 5, ".json")) return 0;
    size_t id_length = length - n - 5;
    if (!id_length || id_length >= ON_LEVEL_ID_SIZE) return 0;
    memcpy(id, path + n, id_length);id[id_length] = 0;
    return on_protocol_valid_level_id(id);
}
int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t cap) {
    if (!strcmp(path, "levels-index.json") && !strcmp(method, "GET")) {
        if (!uploaded_index_body[0]) return answer(response, cap, TEST_LEVEL_INDEX, 200);
        char combined[4096];size_t n = strlen(TEST_LEVEL_INDEX);
        int used = snprintf(combined, sizeof combined, "%.*s,\"%s\":%s}",
                            (int)n - 1, TEST_LEVEL_INDEX, uploaded_level_id,
                            uploaded_index_body);
        if (used < 0 || (size_t)used >= sizeof combined) return -2;
        return answer(response, cap, combined, 200);
    }
    char level_id[ON_LEVEL_ID_SIZE];
    if (level_child_id(path, "levels", level_id)) {
        if (!strcmp(method, "GET")) {
            if (!strcmp(level_id, "104")) return answer(response, cap, TEST_LEVEL, 200);
            if (uploaded_level_body[0] && !strcmp(level_id, uploaded_level_id))
                return answer(response, cap, uploaded_level_body, 200);
            return answer(response, cap, "null", 200);
        }
        if (!strcmp(method, "PUT")) {
            if (if_match && !strcmp(if_match, "null_etag") &&
                (!strcmp(level_id, "104") || !strcmp(level_id, uploaded_level_id)))
                return answer(response, cap, "null", 412);
            if (!body || !on_protocol_published_level(body, level_id, &fake_level_record))
                return answer(response, cap, "null", 400);
            if (strlen(body) >= sizeof uploaded_level_body) return -2;
            strcpy(uploaded_level_id, level_id);strcpy(uploaded_level_body, body);
            /* The native HTTP adapters deliberately discard Firebase's
             * successful echo of a large PUT body. */
            return answer(response, cap, "null", 200);
        }
    }
    if (level_child_id(path, "levels-index", level_id)) {
        if (!strcmp(method, "PUT")) {
            if (if_match && !strcmp(if_match, "null_etag") &&
                uploaded_index_body[0] && !strcmp(level_id, uploaded_level_id))
                return answer(response, cap, "null", 412);
            if (!uploaded_level_body[0] || strcmp(level_id, uploaded_level_id) || !body)
                return answer(response, cap, "null", 400);
            char wrapper[1300];
            int n = snprintf(wrapper, sizeof wrapper, "{\"%s\":%s}", level_id, body);
            OnPublishedLevelSummary summary[1];
            if (n < 0 || (size_t)n >= sizeof wrapper ||
                on_protocol_level_index(wrapper, summary, 1) != 1)
                return answer(response, cap, "null", 400);
            snprintf(uploaded_index_body, sizeof uploaded_index_body, "%s", body);
            return answer(response, cap, body, 200);
        }
    }
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
static void sample_level(OnPublishedLevel *level) {
    memset(level, 0, sizeof *level);
    snprintf(level->id, sizeof level->id, "%s", "1");
    snprintf(level->title, sizeof level->title, "%s", "Нативная публикация");
    snprintf(level->description, sizeof level->description, "%s", "Проверка каталога.");
    level->width = 16;level->height = 10;level->object_count = 3;
    level->objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.color=0x65a845u,.visible=1};
    snprintf(level->objects[0].name, sizeof level->objects[0].name, "%s", "Платформа");
    level->objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.color=0x5ab7e8u,.visible=1};
    snprintf(level->objects[1].name, sizeof level->objects[1].name, "%s", "Игрок");
    level->objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.color=0x69d16cu,.visible=1};
    snprintf(level->objects[2].name, sizeof level->objects[2].name, "%s", "Финиш");
}
static void large_level_transport_round_trip(void) {
    static OnPublishedLevel source, loaded;
    memset(&source, 0, sizeof source);
    snprintf(source.id, sizeof source.id, "%s", "1");
    snprintf(source.title, sizeof source.title, "%s", "Предел публикации");
    snprintf(source.description, sizeof source.description, "%s", "20 000 объектов всех типов.");
    source.width = 16;source.height = 10;source.object_count = ON_LEVEL_OBJECT_CAP;
    for (int i = 0; i < ON_LEVEL_OBJECT_CAP; ++i) {
        int type = i == 0 ? ON_LEVEL_PLAYER : i == 1 ? ON_LEVEL_GOAL :
                   i == 2 ? ON_LEVEL_GROUND : i == 3 ? ON_LEVEL_COIN : ON_LEVEL_BLOCK;
        source.objects[i] = (OnLevelObject){.id=i + 1,.type=type,
            .x=(float)(i % 16),.y=(float)((i / 16) % 10),.w=1,.h=1,
            .color=0x55c8eau,.number=i % 10000,.visible=1};
    }
    assert(on_net_level_publish(&source));
    tick_pump(1);
    OnNetView published = view();
    assert(!published.level_publish_busy && published.level_publish_id[0] &&
           strlen(uploaded_level_body) > 1024u * 1024u);
    on_net_level_fetch(uploaded_level_id);
    tick_pump(1);
    OnNetView fetched = view();
    assert(fetched.level_loaded && !strcmp(fetched.loaded_level_id, uploaded_level_id));
    assert(on_net_take_loaded_level(&loaded));
    assert(loaded.object_count == ON_LEVEL_OBJECT_CAP &&
           loaded.objects[ON_LEVEL_OBJECT_CAP - 1].id == ON_LEVEL_OBJECT_CAP);
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
static void assert_platformer_art(void) {
    const int ids[] = {PV_ART_LEVEL_BLOCK, PV_ART_LEVEL_PLATFORM,
                       PV_ART_LEVEL_TRIGGER, PV_ART_LEVEL_TRIGGER_ROTATE,
                       PV_ART_LEVEL_TRIGGER_FOREVER,
                       PV_ART_LEVEL_TRIGGER_INVISIBILITY,
                       PV_ART_LEVEL_TRIGGER_NO_COLLISION,
                       PV_ART_LEVEL_TRIGGER_GRAVITY, PV_ART_LEVEL_FLAG,
                       PV_ART_LEVEL_SPIKE, PV_ART_LEVEL_SLOPE,
                       PV_ART_LEVEL_ORB_ORANGE, PV_ART_LEVEL_ORB_YELLOW,
                       PV_ART_LEVEL_CHECKPOINT_INACTIVE,
                       PV_ART_LEVEL_CHECKPOINT_ACTIVE,
                       PV_ART_LEVEL_PORTAL_NORMAL,
                       PV_ART_LEVEL_PORTAL_JETPACK,
                       PV_ART_JETPACK_ACTIVE, PV_ART_JETPACK_INACTIVE,
                       PV_ART_LEVEL_TRIGGER_COLOR};
    const int widths[] = {100, 100, 100, 100, 100, 100, 100, 100, 50,
                          100, 100, 100, 100, 100, 100,
                          100, 100, 100, 100, 256};
    const int heights[] = {100, 50, 100, 100, 100, 100, 100, 100, 100,
                           100, 100, 100, 100, 100, 100,
                           100, 100, 100, 100, 256};
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        int width = 0, height = 0, visible = 0;
        const uint32_t *pixels = game_art_rgba(ids[i], &width, &height);
        assert(pixels && width == widths[i] && height == heights[i]);
        for (int p = 0; p < width * height; ++p) visible |= pixels[p] >> 24;
        assert(visible);
    }
}
static void ui_snapshot(const char *name) {
    game_tick(0, lvgl_ui_fullscreen(game_phase()) ? NULL : ui_pixels);
    lvgl_ui_frame(.050f, ui_pixels);
    const char *filter = getenv("PVG3_LVGL_SHOTS");
    if (!filter || !*filter || (strcmp(filter, "1") && strcmp(filter, name))) return;
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
    int down = lvgl_ui_pointer(x, y, 1);
    if (!down) fprintf(stderr, "LVGL tap DOWN missed at %d,%d in phase %d\n", x, y, game_phase());
    assert(down);ui_snapshot("tap_down");
    int up = lvgl_ui_pointer(x, y, 0);
    if (!up) fprintf(stderr, "LVGL tap UP missed at %d,%d in phase %d\n", x, y, game_phase());
    assert(up);ui_snapshot("tap_up");
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
static void native_trigger_runtime_regression(void) {
    static OnPublishedLevel level;
    memset(&level, 0, sizeof level);
    snprintf(level.id, sizeof level.id, "%s", "1");
    snprintf(level.title, sizeof level.title, "%s", "Trigger runtime test");
    level.width = 16;level.height = 10;level.object_count = 8;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_BLOCK,
        .x=6,.y=6,.w=1,.h=1,.visible=1,.number=42};
    level.objects[4] = (OnLevelObject){.id=5,.type=ON_LEVEL_BLOCK,
        .x=8,.y=6,.w=1,.h=1,.visible=1,.number=42};
    /* Action-button events must fire even when the player is nowhere near them. */
    for (int i = 5; i < 8; ++i) {
        level.objects[i] = (OnLevelObject){.id=i + 1,.type=ON_LEVEL_TRIGGER,
            .x=100,.y=100,.w=1,.h=1,.visible=1,
            .trigger_event=ON_TRIGGER_MANUAL,.target_id=0,
            .trigger_group_id=42,.trigger_has_group=1};
    }
    level.objects[5].trigger_kind = ON_TRIGGER_KIND_MOVE;
    level.objects[5].trigger_action = ON_TRIGGER_MOVE;
    level.objects[5].trigger_value = 12;
    level.objects[5].trigger_value_y = -7;
    level.objects[6].trigger_kind = ON_TRIGGER_KIND_ROTATE;
    level.objects[6].trigger_action = ON_TRIGGER_ROTATE;
    level.objects[6].trigger_duration = 1;
    level.objects[6].trigger_has_duration = 1;
    level.objects[7].trigger_kind = ON_TRIGGER_KIND_FOREVER;
    level.objects[7].trigger_action = ON_TRIGGER_UNACTIVATE;

    game_workshop_open();game_workshop_open_details();game_workshop_open_editor();
    assert(game_workshop_preview(&level));
    game_custom_control(0, 0, 1);game_tick(.05f, NULL);game_custom_control(0, 0, 0);
    OnLevelObject first, second;
    assert(game_debug_custom_object(4, &first) && game_debug_custom_object(5, &second));
    assert(first.x == 18 && first.y == -1 && first.angle > 17.9f &&
           first.angle < 18.1f && !first.visible);
    assert(second.x == 20 && second.y == -1 && second.angle > 17.9f &&
           second.angle < 18.1f && !second.visible);
    game_tick(.05f, NULL);
    assert(game_debug_custom_object(4, &first) && !first.visible &&
           first.angle > 35.9f && first.angle < 36.1f);
    for (int i = 0; i < 18; ++i) game_tick(.05f, NULL);
    assert(game_debug_custom_object(4, &first) && !first.visible &&
           (first.angle < .01f || first.angle > 359.99f));
    game_tick(.10f, NULL);
    assert(game_debug_custom_object(4, &first) &&
           (first.angle < .01f || first.angle > 359.99f));

    game_custom_level_exit();
    level.objects[7].trigger_action = ON_TRIGGER_ACTIVATE;
    assert(game_workshop_preview(&level));
    game_custom_control(0, 0, 1);game_tick(.05f, NULL);game_custom_control(0, 0, 0);
    assert(game_debug_custom_object(4, &first) && first.visible &&
           first.x == 18 && first.y == -1 && first.angle > 17.9f &&
           first.angle < 18.1f);
    game_tick(.05f, NULL);
    assert(game_debug_custom_object(4, &first) && first.visible &&
           first.angle > 35.9f && first.angle < 36.1f);
    game_custom_level_exit();

    /* The invisibility trigger hides its target group visually, not physically. */
    level.object_count = 5;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=20,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_BLOCK,
        .x=1,.y=8,.w=1,.h=1,.visible=1,.number=42};
    level.objects[4] = (OnLevelObject){.id=5,.type=ON_LEVEL_TRIGGER,
        .x=1,.y=7,.w=1,.h=1,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_INVISIBILITY,
        .trigger_event=ON_TRIGGER_START,.trigger_action=ON_TRIGGER_INVISIBLE,
        .trigger_group_id=42,.trigger_has_group=1};
    assert(game_workshop_preview(&level));
    assert(game_debug_custom_object(4, &first) && first.visible &&
           game_debug_custom_object_invisible(4));
    for (int i = 0; i < 20; ++i) game_tick(.05f, NULL);
    assert(game_debug_custom_player_y() > 7 * 72 &&
           game_debug_custom_player_y() < 8 * 72);
    game_custom_level_exit();

    /* Touch events need actual contact; merely pressing action never substitutes for touch. */
    level.objects[4].trigger_event = ON_TRIGGER_TOUCH;
    level.objects[4].x = 100;level.objects[4].y = 100;
    assert(game_workshop_preview(&level));
    for (int i = 0; i < 20; ++i) game_tick(.05f, NULL);
    assert(!game_debug_custom_object_invisible(4));
    game_custom_level_exit();
    level.objects[4].x = 1;level.objects[4].y = 7;
    assert(game_workshop_preview(&level));
    game_tick(.01f, NULL);
    assert(game_debug_custom_object_invisible(4));
    game_custom_level_exit();

    /* The collision trigger leaves its block visible but lets the player fall through. */
    level.objects[3].visible = 1;
    level.objects[4].trigger_kind = ON_TRIGGER_KIND_NO_COLLISION;
    level.objects[4].trigger_event = ON_TRIGGER_MANUAL;
    level.objects[4].trigger_action = ON_TRIGGER_NO_COLLISION;
    assert(game_workshop_preview(&level));
    game_custom_control(0, 0, 1);game_tick(.05f, NULL);game_custom_control(0, 0, 0);
    for (int i = 0; i < 20; ++i) game_tick(.05f, NULL);
    assert(game_debug_custom_object(4, &first) && first.visible);
    assert(game_debug_custom_player_y() > 9 * 72);
    game_custom_level_exit();

    /* Rotated blocks use the same oriented geometry as their artwork. */
    level.object_count = 4;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=12,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=4.5f,.y=4.5f,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_BLOCK,
        .x=4,.y=6,.w=2,.h=1,.angle=90,.visible=1,.number=42};
    assert(game_workshop_preview(&level));
    for (int i = 0; i < 120; ++i) game_tick(1.0f / 60.0f, NULL);
    assert(game_debug_custom_player_y() > 315 && game_debug_custom_player_y() < 345);
    game_custom_level_exit();

    /* The existing triangular art is solid, safe, and climbable in native play. */
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_SLOPE,
        .x=3,.y=7,.w=1,.h=1,.visible=1,.number=42};
    assert(game_workshop_preview(&level));
    float spawn_y = game_debug_custom_player_y();
    float highest_y = spawn_y;
    game_custom_control(1, 0, 0);
    for (int i = 0; i < 24; ++i) {
        game_tick(.05f, NULL);
        if (game_debug_custom_player_y() < highest_y)
            highest_y = game_debug_custom_player_y();
    }
    game_custom_control(0, 0, 0);
    assert(game_debug_custom_player_x() > 240 && highest_y < spawn_y - 35);
    game_custom_level_exit();

    /* Horizontal mirroring reverses the slope, its climb direction and collision. */
    level.objects[1].x = 5;
    level.objects[3].flip_x = 1;
    assert(game_workshop_preview(&level));
    screenshot("slope_mirrored_start");
    spawn_y = game_debug_custom_player_y();highest_y = spawn_y;
    game_custom_control(-1, 0, 0);
    for (int i = 0; i < 24; ++i) {
        game_tick(.05f, NULL);
        if (game_debug_custom_player_y() < highest_y)
            highest_y = game_debug_custom_player_y();
        if (i == 13) screenshot("slope_mirrored_climb");
    }
    game_custom_control(0, 0, 0);
    assert(game_debug_custom_player_x() < 3 * 72 && highest_y < spawn_y - 35);
    game_custom_level_exit();

    /* The same slope remains climbable after a quarter-turn rotation. */
    level.objects[3].flip_x = 0;level.objects[3].angle = 90;
    assert(game_workshop_preview(&level));
    spawn_y = game_debug_custom_player_y();highest_y = spawn_y;
    game_custom_control(-1, 0, 0);
    for (int i = 0; i < 24; ++i) {
        game_tick(.05f, NULL);
        if (game_debug_custom_player_y() < highest_y)
            highest_y = game_debug_custom_player_y();
    }
    game_custom_control(0, 0, 0);
    assert(game_debug_custom_player_x() < 3 * 72 && highest_y < spawn_y - 35);
    game_custom_level_exit();

    /* The signed gravity trigger changes the whole level's acceleration. */
    level.object_count = 4;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=30,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=1,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_TRIGGER,
        .x=100,.y=100,.w=1,.h=1,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_GRAVITY,.trigger_event=ON_TRIGGER_START,
        .trigger_action=ON_TRIGGER_SET_GRAVITY,.trigger_value=-100,
        .trigger_color=0xffc54eu};
    assert(game_workshop_preview(&level));
    assert(game_debug_custom_gravity() == 450.0f);
    for (int i = 0; i < 10; ++i) game_tick(.05f, NULL);
    float weak_gravity_y = game_debug_custom_player_y();
    game_custom_level_exit();
    level.objects[3].trigger_value = 100;
    assert(game_workshop_preview(&level));
    assert(game_debug_custom_gravity() == 2450.0f);
    for (int i = 0; i < 10; ++i) game_tick(.05f, NULL);
    float strong_gravity_y = game_debug_custom_player_y();
    assert(strong_gravity_y > weak_gravity_y + 100);
    game_custom_level_exit();

    /* Orbs have no solid body; one jump press while inside the circle activates. */
    level.object_count = 4;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=30,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=3,.y=4,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_ORB_YELLOW,
        .x=3,.y=4,.w=.7f,.h=.7f,.visible=1,.number=4};
    assert(game_workshop_preview(&level));
    float orb_spawn_y = game_debug_custom_player_y();
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_y() > orb_spawn_y &&
           game_debug_custom_player_vy() > 0.0f &&
           game_debug_custom_gravity() == 1450.0f);
    assert(lvgl_ui_touch_pointer(17, 700, 400, 1)); /* click anywhere, not the orb art */
    game_tick(0, NULL);
    assert(game_debug_custom_player_vy() == -650.0f &&
           game_debug_custom_gravity() == 1450.0f);
    assert(lvgl_ui_touch_pointer(17, 700, 400, 0));
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_vy() > -650.0f &&
           game_debug_custom_player_vy() < 0 &&
           game_debug_custom_gravity() == 1450.0f);
    float yellow_bounce_y = game_debug_custom_player_y();
    game_custom_level_exit();
    level.objects[3].type = ON_LEVEL_ORB_ORANGE;
    assert(game_workshop_preview(&level));
    orb_spawn_y = game_debug_custom_player_y();
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_y() > orb_spawn_y &&
           game_debug_custom_player_vy() > 0.0f &&
           game_debug_custom_gravity() == 1450.0f);
    assert(lvgl_ui_touch_pointer(17, 700, 400, 1));
    game_tick(0, NULL);
    assert(game_debug_custom_player_vy() == -1050.0f &&
           game_debug_custom_gravity() == 1450.0f);
    assert(lvgl_ui_touch_pointer(17, 700, 400, 0));
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_vy() < -900.0f &&
           game_debug_custom_gravity() == 1450.0f &&
           game_debug_custom_player_y() < yellow_bounce_y);
    game_custom_level_exit();

    /* Checkpoints activate once touched and own the next death respawn point. */
    level.object_count = 6;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_CHECKPOINT,
        .x=2,.y=7,.w=1,.h=1,.visible=1,.number=4};
    level.objects[4] = (OnLevelObject){.id=5,.type=ON_LEVEL_CHECKPOINT,
        .x=4,.y=7,.w=1,.h=1,.visible=1,.number=5};
    level.objects[5] = (OnLevelObject){.id=6,.type=ON_LEVEL_HAZARD,
        .x=7,.y=7,.w=1,.h=1,.visible=1,.number=6};
    assert(game_workshop_preview(&level));
    game_custom_control(1, 0, 0);
    int saw_first_checkpoint = 0, saw_latest_checkpoint = 0, died_at_latest = 0;
    float latest_spawn_x = (4.0f + .5f - .65f * .5f) * 80.0f;
    float latest_spawn_y = (7.0f + 1.0f - .85f) * 72.0f;
    for (int i = 0; i < 80; ++i) {
        float previous_x = game_debug_custom_player_x();
        game_tick(.05f, NULL);
        int active_checkpoint = game_debug_custom_checkpoint_id();
        if (active_checkpoint == 4) saw_first_checkpoint = 1;
        if (active_checkpoint == 5) saw_latest_checkpoint = 1;
        if (active_checkpoint == 5 && previous_x > latest_spawn_x + 100.0f &&
            fabsf(game_debug_custom_player_x() - latest_spawn_x) < .001f) {
            died_at_latest = 1;break;
        }
    }
    game_custom_control(0, 0, 0);
    assert(saw_first_checkpoint && saw_latest_checkpoint && died_at_latest);
    assert(fabsf(game_debug_custom_player_x() - latest_spawn_x) < .001f &&
           fabsf(game_debug_custom_player_y() - latest_spawn_y) < .001f &&
           game_debug_custom_player_vx() == 0.0f &&
           game_debug_custom_player_vy() == 0.0f &&
           !game_debug_custom_player_grounded() &&
           game_debug_custom_checkpoint_id() == 5);
    game_custom_level_exit();

    /* Falling beyond the world boundary also returns to the saved checkpoint. */
    level.object_count = 3;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_PLAYER,
        .x=1,.y=99999.0f,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_CHECKPOINT,
        .x=1,.y=99999.0f,.w=1,.h=1,.visible=1,.number=3};
    assert(game_workshop_preview(&level));
    game_tick(0, NULL);
    assert(game_debug_custom_checkpoint_id() == 3);
    float fall_spawn_y = (99999.0f + 1.0f - .85f) * 72.0f;
    int fell_to_checkpoint = 0;
    for (int i = 0; i < 60; ++i) {
        float previous_y = game_debug_custom_player_y();
        game_tick(.05f, NULL);
        if (previous_y > fall_spawn_y + 1.0f &&
            fabsf(game_debug_custom_player_y() - fall_spawn_y) < .001f) {
            fell_to_checkpoint = 1;break;
        }
    }
    assert(fell_to_checkpoint && game_debug_custom_checkpoint_id() == 3 &&
           fabsf(game_debug_custom_player_vy()) < .001f &&
           fabsf(game_debug_custom_player_vx()) < .001f &&
           !game_debug_custom_player_grounded());
    game_custom_level_exit();

    /* The art-free particle emitter draws its own trail in the native runtime. */
    static uint32_t with_particles[GAME_W * GAME_H];
    static uint32_t without_particles[GAME_W * GAME_H];
    static uint32_t disabled_particles[GAME_W * GAME_H];
    static uint32_t changed_particles[GAME_W * GAME_H];
    level.object_count = 3;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=30,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    assert(game_workshop_preview(&level));
    game_tick(0, without_particles);
    game_custom_level_exit();
    level.object_count = 4;
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_PARTICLE,
        .x=6,.y=4,.w=1,.h=1,.color=0x68f0d8u,.visible=1,
        .emitter={.enabled=1,.continuous=1,.gravity_enabled=0,.glow=1,
            .rate=8,.lifetime=1.2f,.speed=90,.spread=40,.size=4,
            .direction=-90,.gravity=90}};
    assert(game_workshop_preview(&level));
    game_tick(0, with_particles);
    assert(memcmp(with_particles, without_particles, sizeof with_particles));
    game_custom_level_exit();
    level.objects[3].emitter.enabled = 0;
    assert(game_workshop_preview(&level));
    game_tick(0, disabled_particles);
    assert(!memcmp(disabled_particles, without_particles, sizeof disabled_particles));
    game_custom_level_exit();
    level.objects[3].emitter.enabled = 1;
    level.objects[3].emitter.speed = 0;
    assert(game_workshop_preview(&level));
    game_tick(0, changed_particles);
    assert(memcmp(changed_particles, with_particles, sizeof changed_particles));
    game_custom_level_exit();

    /* The bread's collider starts/ends at its visible alpha bounds, not its PNG canvas. */
    level.object_count = 4;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_BLOCK,
        .x=2,.y=7,.w=1,.h=1,.visible=1,.number=4};
    assert(game_workshop_preview(&level));
    game_custom_control(1, 0, 0);
    for (int i = 0; i < 3; ++i) game_tick(.05f, NULL);
    game_custom_control(0, 0, 0);
    assert(game_debug_custom_player_x() > 112);
    screenshot("alpha_player_wall");
    game_custom_level_exit();

    /* A transparent upper corner of the spike art is not a lethal contact. */
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=20,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=2.53f,.y=4.033f,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_HAZARD,
        .x=3,.y=4,.w=1,.h=1,.visible=1,.number=4};
    assert(game_workshop_preview(&level));
    float spike_graze_spawn = game_debug_custom_player_x();
    game_custom_control(1, 0, 0);game_tick(.001f, NULL);game_custom_control(0, 0, 0);
    assert(game_debug_custom_player_x() > spike_graze_spawn + .1f);
    screenshot("alpha_spike_graze");
    game_custom_level_exit();

    /* Trigger PNGs are for the editor only and never appear in a play preview. */
    static uint32_t with_trigger[GAME_W * GAME_H];
    static uint32_t without_trigger[GAME_W * GAME_H];
    level.object_count = 4;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=12,.w=16,.h=1,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_TRIGGER,
        .x=10,.y=1,.w=1,.h=1,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_INVISIBILITY,
        .trigger_event=ON_TRIGGER_MANUAL,.trigger_action=ON_TRIGGER_INVISIBLE,
        .target_id=3};
    assert(game_workshop_preview(&level));
    game_tick(0, with_trigger);
    game_custom_level_exit();
    level.object_count = 3;
    assert(game_workshop_preview(&level));
    game_tick(0, without_trigger);
    assert(!memcmp(with_trigger, without_trigger, sizeof with_trigger));
    game_custom_level_exit();
}

static void native_recolor_background_regression(void) {
    static OnPublishedLevel level;
    static uint32_t base_frame[GAME_W * GAME_H];
    static uint32_t changed_frame[GAME_W * GAME_H];
    memset(&level, 0, sizeof level);
    snprintf(level.id, sizeof level.id, "%s", "3");
    snprintf(level.title, sizeof level.title, "%s", "Recolor and background test");
    level.width = 16;level.height = 10;level.object_count = 5;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.color=0x65a845u,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.color=0x5ab7e8u,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.color=0x69d16cu,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_BLOCK,
        .x=6,.y=6,.w=1,.h=1,.color=0x55c8eau,.visible=1,.number=42};
    level.objects[4] = (OnLevelObject){.id=5,.type=ON_LEVEL_BLOCK,
        .x=8,.y=6,.w=1,.h=1,.color=0x55c8eau,.visible=1,.number=43};
    assert(game_workshop_preview(&level));
    game_tick(0, base_frame);
    assert(game_debug_custom_background_color() == 0x8bcce6u);
    game_custom_level_exit();

    level.object_count = 7;
    level.objects[5] = (OnLevelObject){.id=6,.type=ON_LEVEL_TRIGGER,
        .x=100,.y=100,.w=1,.h=1,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_RECOLOR,.trigger_event=ON_TRIGGER_START,
        .trigger_action=ON_TRIGGER_RECOLOR,.target_id=0,
        .trigger_group_id=42,.trigger_has_group=1,.trigger_color=0xd02da6u};
    level.objects[6] = (OnLevelObject){.id=7,.type=ON_LEVEL_TRIGGER,
        .x=102,.y=100,.w=1,.h=1,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_BACKGROUND,.trigger_event=ON_TRIGGER_START,
        .trigger_action=ON_TRIGGER_SET_BACKGROUND,.target_id=0,
        .trigger_color=0x4c82d0u};
    assert(game_workshop_preview(&level));
    OnLevelObject recolored, untouched;
    assert(game_debug_custom_object(4, &recolored) &&
           recolored.color == 0xd02da6u);
    assert(game_debug_custom_object(5, &untouched) &&
           untouched.color == 0x55c8eau);
    assert(game_debug_custom_background_color() == 0x4c82d0u);
    game_tick(0, changed_frame);
    assert(changed_frame[160 * GAME_W + 20] ==
           (0xff000000u | (0xd0u << 16) | (0x82u << 8) | 0x4cu));
    int changed_pixels = 0;
    for (int y = 285; y < 390; ++y)
        for (int x = 875; x < 980; ++x)
            changed_pixels += base_frame[y * GAME_W + x] !=
                              changed_frame[y * GAME_W + x];
    assert(changed_pixels > 100); /* recolor must visibly tint block artwork */
    game_custom_level_exit();
}

static void native_jetpack_portal_regression(void) {
    static OnPublishedLevel level;
    memset(&level, 0, sizeof level);
    snprintf(level.id, sizeof level.id, "%s", "2");
    snprintf(level.title, sizeof level.title, "%s", "Jetpack portal test");
    level.width = 16;level.height = 10;level.object_count = 6;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.number=3};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.visible=1,.number=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.number=2};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_PORTAL_NORMAL,
        .x=1,.y=7,.w=1,.h=1,.visible=1,.number=4};
    level.objects[4] = (OnLevelObject){.id=5,.type=ON_LEVEL_PORTAL_JETPACK,
        .x=1,.y=7,.w=1,.h=1,.visible=1,.number=5};
    level.objects[5] = (OnLevelObject){.id=6,.type=ON_LEVEL_PORTAL_NORMAL,
        .x=4,.y=7,.w=1,.h=1,.visible=1,.number=6};

    game_workshop_open();game_workshop_open_details();game_workshop_open_editor();
    assert(game_workshop_preview(&level));
    assert(!game_custom_jetpack_mode());
    game_tick(.01f, NULL);
    assert(game_custom_jetpack_mode() && game_debug_custom_jetpack_mode());
    assert(!game_debug_custom_jetpack_active());
    game_set_lvgl_ui(1);
    lvgl_ui_frame(.016f, ui_pixels);
    assert(lvgl_ui_test_label_present("Уровень"));
    assert(!lvgl_ui_test_label_present("ID —  ·  Уровень"));
    assert(lvgl_ui_test_label_present("Jetpack · ↑/W, ↓/S"));
    float parked_y = game_debug_custom_player_y();
    game_tick(.05f, NULL);
    assert(!game_debug_custom_jetpack_active());
    assert(fabsf(game_debug_custom_player_y() - parked_y) < .001f);
    assert(fabsf(game_debug_custom_player_vy()) < .001f);

    game_custom_vertical_control(1);
    game_tick(.05f, NULL);
    float raised_y = game_debug_custom_player_y();
    assert(raised_y < parked_y - 10.0f && game_debug_custom_player_vy() < 0);
    assert(game_debug_custom_jetpack_active());
    game_custom_vertical_control(0);
    game_tick(.05f, NULL);
    assert(fabsf(game_debug_custom_player_y() - raised_y) < .001f);
    assert(!game_debug_custom_jetpack_active());
    game_custom_vertical_control(-1);
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_y() > raised_y + 10.0f);
    assert(game_debug_custom_player_vy() > 0 && game_debug_custom_jetpack_active());
    game_custom_vertical_control(0);
    game_tick(.05f, NULL);
    assert(!game_debug_custom_jetpack_active());
    assert(!game_debug_custom_player_facing_left());

    /* Vertical, jump and action controls never change horizontal facing. */
    game_custom_vertical_control(1);
    game_tick(.05f, NULL);
    assert(!game_debug_custom_player_facing_left());
    game_custom_vertical_control(0);
    game_custom_control(-1, 0, 0);
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_facing_left());
    game_custom_control(0, 0, 1);
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_facing_left());
    game_custom_control(0, 0, 0);
    game_custom_control(1, 0, 0);
    game_tick(.05f, NULL);
    assert(!game_debug_custom_player_facing_left());
    game_custom_control(0, 0, 0);

    /* Native screen controls continuously drive both vertical directions. */
    assert(lvgl_ui_touch_pointer(41, 1120, 591, 1));
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_vy() < 0 && game_debug_custom_jetpack_active());
    assert(lvgl_ui_touch_pointer(41, 1120, 591, 0));
    game_tick(.05f, NULL);
    assert(fabsf(game_debug_custom_player_vy()) < .001f &&
           !game_debug_custom_jetpack_active());
    assert(lvgl_ui_touch_pointer(42, 1200, 591, 1));
    game_tick(.05f, NULL);
    assert(game_debug_custom_player_vy() > 0 && game_debug_custom_jetpack_active());
    assert(lvgl_ui_touch_pointer(42, 1200, 591, 0));
    game_tick(.05f, NULL);

    /* A normal portal changes form once; overlapping it cannot switch back. */
    game_custom_control(1, 0, 0);
    for (int i = 0; i < 32; ++i) game_tick(.05f, NULL);
    game_custom_control(0, 0, 0);
    assert(!game_custom_jetpack_mode());
    lvgl_ui_frame(.016f, ui_pixels);
    assert(lvgl_ui_test_label_present("Прыжок · Space / ↑"));
    for (int i = 0; i < 3; ++i) game_tick(.05f, NULL);
    assert(!game_custom_jetpack_mode());

    /* Leaving and re-entering the Jetpack portal selects Jetpack again. */
    game_custom_control(-1, 0, 0);
    for (int i = 0; i < 32; ++i) game_tick(.05f, NULL);
    game_custom_control(0, 0, 0);
    assert(game_custom_jetpack_mode());
    game_tick(.05f, NULL);
    assert(game_custom_jetpack_mode() && !game_debug_custom_jetpack_active());
    game_custom_level_exit();
}

static int run_lvgl_test(void) {
    static uint8_t before[20000], after[20000];
    size_t bytes = game_save_size();assert(bytes < sizeof before);
    game_init();assert(game_save_export(before, bytes));
    assert_platformer_art();
    assert(lvgl_ui_init());
    native_trigger_runtime_regression();
    native_recolor_background_regression();
    native_jetpack_portal_regression();
    game_init();assert(game_save_export(after, bytes) && !memcmp(before, after, bytes));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER_ROTATE));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER_FOREVER));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER_INVISIBILITY));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER_NO_COLLISION));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER_GRAVITY));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_TRIGGER_COLOR));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_ORB_ORANGE));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_ORB_YELLOW));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_CHECKPOINT_INACTIVE));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_CHECKPOINT_ACTIVE));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_PORTAL_NORMAL));
    assert(lvgl_ui_test_art_loaded(PV_ART_LEVEL_PORTAL_JETPACK));
    assert(lvgl_ui_test_art_loaded(PV_ART_JETPACK_ACTIVE));
    assert(lvgl_ui_test_art_loaded(PV_ART_JETPACK_INACTIVE));
    game_set_lvgl_ui(1);
    ui_snapshot("menu");
    assert(ui_pixels[60 * GAME_W + 545] == 0xFFFFFFFFu); /* campaign */
    assert(ui_pixels[60 * GAME_W + 848] == 0xFFFFFFFFu); /* garden */
    assert(ui_pixels[70 * GAME_W + 970] == 0xFFFFFFFFu); /* player levels */
    assert(ui_pixels[580 * GAME_W + 100] == 0xFFFFFFFFu); /* book */
    assert(ui_pixels[560 * GAME_W + 420] == 0xFFFFFFFFu); /* start */
    assert(ui_pixels[580 * GAME_W + 905] == 0xFFFFFFFFu); /* online */

    ui_tap(1080, 80);assert(game_phase() == GAME_CUSTOM_LEVELS);tick_pump(2);
    ui_snapshot("menu_player_catalog");
    ui_tap(912, 210);assert(game_phase() == GAME_WORKSHOP);
    ui_snapshot("workshop_home");
    assert(!lvgl_ui_test_label_present("Создай уровень или открой опубликованный каталог."));
    assert(!lvgl_ui_test_label_present(
        "Строй сцену, настраивай объекты, группы, цвет и движение."));
    ui_tap(310, 599);assert(game_phase() == GAME_WORKSHOP_DETAILS);
    ui_snapshot("workshop_details");
    assert(!lvgl_ui_test_label_present("Нажми, чтобы добавить описание уровня."));
    ui_tap(638, 242); /* set the level title with the native virtual keyboard */
    ui_snapshot("workshop_keyboard_open");
    ui_tap(392, 620);ui_tap(392, 620); /* toggle case in both directions */
    ui_tap(268, 620);ui_snapshot("workshop_keyboard_special");
    ui_tap(167, 392); /* return to letters with the custom ASCII label */
    ui_tap(640, 505);ui_tap(538, 275);ui_tap(336, 505);ui_tap(640, 505);
    ui_tap(1132, 620); /* keyboard OK */
    ui_tap(638, 395); /* set the level description */
    ui_tap(437, 505);ui_tap(437, 390);ui_tap(639, 390);ui_tap(843, 275);
    ui_tap(639, 390);ui_tap(336, 275);ui_tap(640, 505);
    ui_tap(1132, 620); /* keyboard OK */
    ui_snapshot("workshop_details_named");
    ui_tap(964, 600);assert(game_phase() == GAME_WORKSHOP_EDIT);
    ui_snapshot("workshop_editor");
    assert(lvgl_ui_test_label_present("ЗЕРКАЛО"));
    assert(!lvgl_ui_test_label_present("Зеркало: горизонтально или вертикально."));
    assert(!lvgl_ui_test_label_present(
        "Зелёные стрелки двигают на 0,5 блока; бирюзовые — окно карты."));
    assert(!lvgl_ui_test_label_present("Выбери категорию и клетку карты"));
    ui_tap(191, 205);ui_snapshot("workshop_block_added");

    /* Multi-select, move as a group, copy/paste, and delete the temporary
     * copies without disturbing the earlier block used by trigger tests. */
    ui_tap(80, 596);ui_tap(275, 247);ui_tap(317, 247);
    ui_tap(929, 209); /* single-select mode */
    ui_tap(275, 247); /* first temporary block */
    ui_tap(1044, 209); /* multi-select mode */
    ui_tap(317, 247); /* add the adjacent block to the selection */
    ui_snapshot("workshop_multi_selected");
#ifdef PVG3_LVGL_TEST
    static OnPublishedLevel editor_probe;
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.object_count == 6 &&
           editor_probe.objects[4].x == 5 && editor_probe.objects[5].x == 6);
#endif
    ui_tap(214, 688); /* move both objects right by half a world cell */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.object_count == 6 &&
           editor_probe.objects[4].x == 5.5f && editor_probe.objects[5].x == 6.5f);
#endif
    ui_tap(377, 688);ui_tap(428, 688); /* mirror left/right and top/bottom */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[4].flip_x && editor_probe.objects[4].flip_y &&
           editor_probe.objects[5].flip_x && editor_probe.objects[5].flip_y);
#endif
    ui_tap(808, 690); /* copy selection */
    ui_tap(932, 690); /* paste offset copies */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.object_count == 8 &&
           editor_probe.objects[6].x == 6.5f && editor_probe.objects[6].y == 3 &&
           editor_probe.objects[7].x == 7.5f && editor_probe.objects[7].y == 3 &&
           editor_probe.objects[6].number == editor_probe.objects[4].number &&
           editor_probe.objects[6].flip_x && editor_probe.objects[6].flip_y &&
           editor_probe.objects[7].flip_x && editor_probe.objects[7].flip_y);
#endif
    ui_tap(1056, 690); /* delete only the pasted selection */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.object_count == 6);
#endif
    ui_tap(296, 247);ui_tap(338, 247); /* reselect the half-shifted original pair */
    ui_tap(1056, 690); /* remove the temporary pair */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.object_count == 4);
#endif

    ui_tap(929, 209); /* return to single-select mode */
    ui_tap(191, 205); /* select the retained block */
    assert(lvgl_ui_test_label_present("Блок · ID 4"));
    ui_tap(1180, 690); /* independent size/rotation dialog */
    ui_snapshot("workshop_transform_dialog");
    assert(!lvgl_ui_test_label_present(
        "Поворот здесь не меняет триггеры. Выбрано: 1"));
    ui_tap(809, 187); /* width +0.1 */
    ui_tap(722, 375); /* counterclockwise 45-degree rotation */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[3].angle == 315.0f);
#endif
    ui_tap(809, 375); /* clockwise 45-degree rotation, independent of triggers */
    ui_tap(809, 375); /* return from 0 to 45 degrees */
    ui_tap(640, 610); /* save direct transform */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.object_count == 4 &&
           editor_probe.objects[3].w > 1.09f && editor_probe.objects[3].w < 1.11f &&
           editor_probe.objects[3].angle == 45.0f);
#endif
    ui_snapshot("workshop_direct_transform");

    ui_tap(531, 596); /* trigger category; movement is the default */
    ui_tap(294, 247); /* place a movement trigger */
    ui_tap(835, 361); /* configure X and Y separately */
    assert(!lvgl_ui_test_label_present(
        "Оба смещения задаются\nотдельно, до ±9999."));
    ui_tap(640, 343); /* X: replace 1 with 9999 */
    ui_snapshot("workshop_numeric_keyboard");
    ui_tap(975, 505);ui_tap(640, 505);ui_tap(640, 505);
    ui_tap(640, 505);ui_tap(640, 505);ui_tap(975, 390);
    ui_tap(640, 440); /* Y: replace 0 with -9999 */
    ui_tap(975, 505);ui_tap(194, 620);
    ui_tap(640, 505);ui_tap(640, 505);ui_tap(640, 505);ui_tap(640, 505);
    ui_tap(975, 390);ui_tap(640, 248); /* choose another target group */
    ui_tap(975, 505);ui_tap(417, 275);ui_tap(194, 390);ui_tap(975, 390);
    ui_tap(640, 628); /* save movement settings */
    ui_tap(930, 439); /* rotation variant */
    ui_tap(338, 247); /* place the rotation trigger */
    ui_tap(835, 361); /* open rotation trigger settings */
    assert(!lvgl_ui_test_label_present(
        "Группа вращается\nсо скоростью 1 оборот/с."));
    ui_tap(640, 371); /* edit rotation angle */
    ui_tap(975, 505);ui_tap(975, 505);
    ui_tap(194, 275);ui_tap(640, 275);ui_tap(417, 390);ui_tap(975, 390);
    ui_tap(640, 628);
    ui_tap(1046, 439); /* forever variant */
    ui_tap(380, 289); /* place the persistent group action */
    ui_tap(835, 361); /* open forever-trigger settings */
    assert(!lvgl_ui_test_label_present(
        "Группа останется в выбранном состоянии до другого триггера."));
    ui_tap(640, 248); /* group input */
    ui_tap(975, 505);ui_tap(417, 275);ui_tap(194, 390);ui_tap(975, 390);
    ui_tap(720, 371); /* unactivate forever */
    ui_tap(640, 628); /* save trigger settings */
    ui_tap(1162, 439); /* invisibility variant */
    ui_tap(422, 330); /* place invisibility trigger */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_INVISIBILITY);
#endif
    ui_tap(814, 480); /* no-collision variant */
    ui_tap(472, 330); /* place no-collision trigger */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_NO_COLLISION);
#endif
    ui_tap(472, 330); /* select the newly placed no-collision trigger */
    ui_tap(835, 361); /* open trigger settings */
    assert(!lvgl_ui_test_label_present(
        "Объекты исчезнут с экрана, но сохранят столкновения."));
    assert(!lvgl_ui_test_label_present(
        "После события у объектов группы отключится столкновение."));
    ui_tap(1015, 528); /* switch the selected trigger to invisibility */
    ui_tap(268, 572); /* and back to no-collision */
    ui_tap(878, 173);ui_tap(878, 173);ui_tap(878, 173); /* event: start */
    ui_tap(640, 628); /* close trigger settings */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_NO_COLLISION &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_event ==
           ON_TRIGGER_START);
#endif
    ui_tap(531, 596); /* trigger category */
    ui_tap(930, 480); /* gravity trigger */
    ui_tap(520, 370); /* place it away from the finish */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_GRAVITY &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_action ==
           ON_TRIGGER_SET_GRAVITY &&
           !editor_probe.objects[editor_probe.object_count - 1].trigger_has_group &&
           editor_probe.objects[editor_probe.object_count - 1].target_id == 0);
#endif
    ui_tap(835, 361); /* open the gravity settings */
    assert(!lvgl_ui_test_label_present(
        "−100 · слабее       0 · обычная       +100 · сильнее"));
    assert(!lvgl_ui_test_label_present(
        "Ползунок задаёт гравитацию всего уровня; орбы действуют отдельно."));
    ui_tap(158, 319); /* weak gravity is the negative endpoint */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_GRAVITY &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_value <= -95.0f);
#endif
    ui_tap(1079, 319); /* strong gravity is the positive endpoint */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_GRAVITY &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_value == 100.0f);
#endif
    ui_tap(640, 628); /* save the slider value */
    ui_tap(1046, 480); /* recolor trigger */
    ui_tap(520, 370); /* place a recolor trigger */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_RECOLOR &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_action ==
           ON_TRIGGER_RECOLOR &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_has_group &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_group_id == 2);
    uint32_t recolor_original =
        editor_probe.objects[editor_probe.object_count - 1].trigger_color;
#endif
    ui_tap(835, 361); /* open recolor settings */
    assert(lvgl_ui_test_label_present("Цвет объектов"));
    ui_tap(640, 343); /* open the color-wheel palette */
    ui_tap(705, 322); /* select a color swatch */
    ui_tap(640, 598); /* return to the editor */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_RECOLOR &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_color !=
           recolor_original);
#endif
    ui_tap(1162, 480); /* background trigger */
    ui_tap(565, 370); /* place a background trigger */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_BACKGROUND &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_action ==
           ON_TRIGGER_SET_BACKGROUND &&
           !editor_probe.objects[editor_probe.object_count - 1].trigger_has_group &&
           editor_probe.objects[editor_probe.object_count - 1].target_id == 0);
#endif
    ui_tap(835, 361); /* open background settings */
    assert(lvgl_ui_test_label_present("Цвет фона"));
    assert(!lvgl_ui_test_label_present("Целевая группа"));
    ui_tap(640, 248);ui_tap(705, 322);ui_tap(640, 598);
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].trigger_kind ==
           ON_TRIGGER_KIND_BACKGROUND &&
           editor_probe.objects[editor_probe.object_count - 1].trigger_color ==
           editor_probe.objects[editor_probe.object_count - 1].color);
#endif
    ui_tap(607, 596); /* orb category (yellow is the default) */
    ui_tap(565, 414); /* place a yellow orb */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type == ON_LEVEL_ORB_YELLOW);
#endif
    ui_tap(1108, 439); /* orange orb variant */
    ui_tap(607, 414); /* place a stronger orange orb */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type == ON_LEVEL_ORB_ORANGE);
#endif
    ui_tap(1178, 526); /* scroll the infinite workshop map */
    ui_tap(80, 596); /* select the shared blocks category */
    ui_tap(990, 439); /* choose its slope variant */
    ui_tap(506, 331); /* place a solid triangular slope */
    ui_snapshot("workshop_slope_added");
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type == ON_LEVEL_SLOPE);
#endif
    ui_tap(377, 688);ui_snapshot("workshop_slope_mirror_x");
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].flip_x);
#endif
    ui_tap(428, 688);ui_snapshot("workshop_slope_mirror_xy");
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].flip_x &&
           editor_probe.objects[editor_probe.object_count - 1].flip_y);
#endif
    ui_tap(742, 596); /* the art-free P palette button */
    ui_tap(590, 370);ui_snapshot("workshop_particle_added");
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type == ON_LEVEL_PARTICLE &&
           editor_probe.objects[editor_probe.object_count - 1].emitter.rate == 8 &&
           editor_probe.objects[editor_probe.object_count - 1].emitter.enabled);
#endif
    ui_tap(830, 360);ui_snapshot("workshop_particle_settings");
    assert(!lvgl_ui_test_label_present("P — точка рождения частиц"));
    assert(!lvgl_ui_test_label_present("0° вправо · −90° вверх · +90° вниз"));
    assert(!lvgl_ui_test_label_present(
        "Скорость, угол, размер и время жизни задают реальную траекторию частиц."));
    ui_tap(490, 220); /* adjust the particles-per-second slider */
    ui_tap(316, 142); /* switch to finite bursts */
    ui_tap(485, 142); /* enable the gravity setting */
    ui_snapshot("workshop_particle_settings_changed");
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type == ON_LEVEL_PARTICLE &&
           editor_probe.objects[editor_probe.object_count - 1].emitter.rate != 8 &&
           !editor_probe.objects[editor_probe.object_count - 1].emitter.continuous &&
           editor_probe.objects[editor_probe.object_count - 1].emitter.gravity_enabled &&
           on_level_particle_valid(&editor_probe.objects[editor_probe.object_count - 1].emitter));
#endif
    ui_tap(640, 659);ui_snapshot("workshop_particle_settings_closed");
    ui_tap(455, 596); /* goals group contains both the finish and checkpoints */
    ui_tap(1108, 439); /* checkpoint variant */
    ui_tap(675, 220); /* place a checkpoint on an empty map cell */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type ==
           ON_LEVEL_CHECKPOINT);
#endif
    ui_tap(683, 596); /* portals category */
    ui_tap(1108, 439); /* select the Jetpack portal */
    ui_tap(675, 270); /* place it on the next map row */
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type ==
           ON_LEVEL_PORTAL_JETPACK);
#endif
    ui_tap(870, 439); /* the normal portal leaves Jetpack form */
    ui_tap(675, 320);
#ifdef PVG3_LVGL_TEST
    assert(lvgl_ui_test_workshop_level(&editor_probe));
    assert(editor_probe.objects[editor_probe.object_count - 1].type ==
           ON_LEVEL_PORTAL_NORMAL);
#endif
    ui_tap(80, 596); /* reselect blocks category and choose its square variant */
    ui_tap(834, 439);
    ui_tap(422, 205); /* now maps to world X=10 */
    ui_snapshot("workshop_trigger_variants_and_pan");
#ifdef PVG3_LVGL_TEST
    assert(ui_pixels[480 * GAME_W + 800] == ui_pixels[480 * GAME_W + 1100]);
#endif
    ui_tap(929, 209); /* select the earlier block for the edit dialogs */
    ui_tap(149, 195);
    ui_tap(836, 377);ui_snapshot("workshop_move_dialog");
    ui_tap(537, 152);ui_tap(1100, 152);
    ui_snapshot("workshop_move_adjusted");
    ui_tap(171, 253);ui_tap(687, 253);ui_snapshot("workshop_move_options");
    ui_tap(171, 253);ui_tap(687, 253);
    ui_tap(640, 638);assert(game_phase() == GAME_WORKSHOP_EDIT);
    ui_tap(987, 377);ui_snapshot("workshop_group_dialog");
    ui_tap(721, 159);ui_tap(640, 650);
    ui_tap(1145, 377);ui_snapshot("workshop_color_dialog");
    assert(!lvgl_ui_test_label_present("Палитра 1 · выбери цвет"));
    ui_tap(535, 208);ui_tap(351, 322);
    ui_snapshot("workshop_color_selected");
    ui_tap(640, 598); /* close the color dialog before using the toolbar */
    ui_tap(820, 50);tick_pump(2);
    OnNetView published = view();
    assert(!published.level_publish_busy && published.level_publish_id[0] &&
           strstr(published.level_publish_notice, "ОПУБЛИКОВАН") &&
           uploaded_level_body[0] && uploaded_index_body[0]);
    assert(strstr(uploaded_level_body, "\"title\":\"Новый уровеньтест\"") &&
           strstr(uploaded_level_body, "\"description\":\"маршрут\"") &&
           strstr(uploaded_level_body, "\"kind\":\"move\"") &&
           strstr(uploaded_level_body, "\"kind\":\"rotate\"") &&
           strstr(uploaded_level_body, "\"duration\":135") &&
           strstr(uploaded_level_body, "\"valueX\":9999.0000") &&
           strstr(uploaded_level_body, "\"valueY\":-9999.0000") &&
           strstr(uploaded_level_body, "\"groupId\":24") &&
           strstr(uploaded_level_body, "\"action\":\"unactivate\"") &&
           strstr(uploaded_level_body, "\"kind\":\"forever\"") &&
           strstr(uploaded_level_body, "\"kind\":\"invisibility\"") &&
           strstr(uploaded_level_body, "\"action\":\"invisible\"") &&
           strstr(uploaded_level_body, "\"kind\":\"no-collision\"") &&
           strstr(uploaded_level_body, "\"action\":\"no-collision\"") &&
           strstr(uploaded_level_body, "\"kind\":\"gravity\"") &&
           strstr(uploaded_level_body, "\"action\":\"set-gravity\"") &&
           strstr(uploaded_level_body, "\"value\":100.0000") &&
           strstr(uploaded_level_body, "\"type\":\"orb-yellow\"") &&
           strstr(uploaded_level_body, "\"type\":\"orb-orange\"") &&
           strstr(uploaded_level_body, "\"type\":\"portal-normal\"") &&
           strstr(uploaded_level_body, "\"type\":\"portal-jetpack\"") &&
           strstr(uploaded_level_body, "\"type\":\"particle\"") &&
           strstr(uploaded_level_body, "\"emitter\":{\"enabled\":true") &&
           strstr(uploaded_level_body, "\"continuous\":false") &&
           strstr(uploaded_level_body, "\"gravityEnabled\":true") &&
           strstr(uploaded_level_body, "\"event\":\"start\"") &&
           strstr(uploaded_level_body, "\"type\":\"slope\"") &&
           strstr(uploaded_level_body, "\"flipX\":true") &&
           strstr(uploaded_level_body, "\"flipY\":true") &&
           strstr(uploaded_level_body, "\"x\":10.0000") &&
           strstr(uploaded_level_body, "\"w\":1.1000") &&
           strstr(uploaded_level_body, "\"angle\":45.000"));
    ui_snapshot("workshop_published");
    char editor_id_label[48];
    snprintf(editor_id_label, sizeof editor_id_label, "ID %s",
             published.level_publish_id);
    assert(lvgl_ui_test_label_present(editor_id_label));
    ui_tap(1158, 50);assert(game_phase() == GAME_CUSTOM_PLAY);
    ui_snapshot("workshop_preview");
    ui_tap(1140, 55);assert(game_phase() == GAME_WORKSHOP_EDIT);
    ui_tap(977, 50);assert(game_phase() == GAME_WORKSHOP_DETAILS);
    ui_tap(1130, 74);assert(game_phase() == GAME_WORKSHOP);
    ui_snapshot("workshop_saved");
    ui_tap(984, 599);assert(game_phase() == GAME_CUSTOM_LEVELS);
    ui_tap(1140, 55);assert(game_phase() == GAME_WORKSHOP);
    ui_tap(1130, 74);assert(game_phase() == GAME_CUSTOM_LEVELS);
    ui_tap(1140, 77);assert(game_phase() == GAME_MENU);
    ui_snapshot("menu_after_workshop");

    ui_tap(680, 82);assert(game_phase() == GAME_SELECT);
    ui_tap(1040, 620);assert(game_phase() == GAME_CUSTOM_LEVELS);
    tick_pump(2);ui_snapshot("custom_levels");
    OnNetView catalog = view();
    assert(catalog.level_count == 3 && !strcmp(catalog.levels[0].id, "104") &&
           !strcmp(catalog.levels[0].title,
                   "Невероятное приключение через тайный мост к финишу") &&
           !strcmp(catalog.levels[0].description,
                   "Найди скрытый мост и монеты, затем доберись до финиша по платформам.") &&
           !strcmp(catalog.levels[1].id, ON_LEVEL_OFFICIAL_ID));
    assert(lvgl_ui_test_label_present("ОФИЦИАЛЬНЫЙ"));
    assert(lvgl_ui_test_label_does_not_wrap("ОФИЦИАЛЬНЫЙ") &&
           lvgl_ui_test_label_does_not_wrap(ON_LEVEL_OFFICIAL_ID));
    ui_tap(185, 318);tick_pump(3);
    assert(game_phase() == GAME_CUSTOM_PLAY);
    game_tick(.05f, NULL); /* settle on the ground before jumping */
    float custom_x = game_debug_custom_player_x();
    float custom_y = game_debug_custom_player_y();
    assert(lvgl_ui_touch_pointer(17, 230, 591, 1)); /* forward */
    assert(lvgl_ui_touch_pointer(23, 1150, 591, 1)); /* second finger: jump */
    for (int i = 0; i < 5; ++i) game_tick(.05f, NULL);
    assert(game_debug_custom_player_x() > custom_x + 50);
    assert(game_debug_custom_player_y() < custom_y - 20);
    assert(lvgl_ui_touch_pointer(23, 1150, 591, 0));
    custom_x = game_debug_custom_player_x();
    for (int i = 0; i < 4; ++i) game_tick(.05f, NULL);
    assert(game_debug_custom_player_x() > custom_x + 40); /* forward remains held */
    assert(lvgl_ui_touch_pointer(17, 230, 591, 0));
    custom_x = game_debug_custom_player_x();
    assert(lvgl_ui_touch_pointer(31, 110, 591, 1)); /* back */
    for (int i = 0; i < 5; ++i) game_tick(.05f, NULL);
    assert(lvgl_ui_touch_pointer(31, 110, 591, 0));
    assert(game_debug_custom_player_x() < custom_x);
    ui_tap(1140, 55);assert(game_phase() == GAME_CUSTOM_LEVELS);
    ui_snapshot("custom_level_return");
    ui_tap(1150, 76);assert(game_phase() == GAME_SELECT);
    ui_tap(1150, 76);assert(game_phase() == GAME_MENU);
    ui_snapshot("menu_after_custom");
    ui_tap(830, 79);assert(game_phase() == GAME_GARDEN);
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
    ui_tap(180, 57); /* switch to the goose palette */
    GameOfflineUIState garden_state;
    game_offline_ui_snapshot(&garden_state);
    assert(garden_state.garden_mode == 1);
    ui_drag(450, 55, 649, 176, "garden_goose_hover");
    game_garden_export(garden);assert(garden[3] == 6);
    ui_tap(180, 93); /* water map is selectable without losing placements */
    game_offline_ui_snapshot(&garden_state);
    assert(garden_state.garden_map == 5 && garden[3] == 6);
    ui_snapshot("garden_water");
    const uint32_t *garden_water = game_art_rgba(PV_ART_WATER, &w, &h);
    assert(garden_water && w == 500 && h == 500);
    int garden_canal_x = 980, garden_canal_y = 300;
    int garden_src_y = h / 5 + (garden_canal_y - 232) *
        (h * 27 / 50 - h / 5) / 224;
    int garden_src_x = w / 2 + (garden_canal_x - 250) *
        (w - w / 2) / (GAME_W - 250);
    assert(ui_pixels[garden_canal_y * GAME_W + garden_canal_x] ==
           garden_water[garden_src_y * w + garden_src_x]);
    ui_tap(60, 57); /* return to plants while keeping the water map */
    ui_drag(840, 55, 535, 288, "garden_water_lily");
    game_garden_export(garden);
    assert(garden[11] == 5 && game_debug_garden_plant_type(1, 2) == 4);
    ui_tap(60, 93); /* return to the lawn without losing either placement */
    game_offline_ui_snapshot(&garden_state);
    assert(garden_state.garden_mode == 0 && garden_state.garden_map == 1);
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
    assert(lvgl_ui_pointer(680, 78, 1));
    assert(lvgl_ui_cancel());ui_snapshot("cancel_menu_button");
    assert(game_phase() == GAME_MENU);
    ui_quick_tap(680, 78);assert(game_phase() == GAME_SELECT);
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
    assert(ui_pixels[50 * GAME_W + 935] == 0xFFFFFFFFu); /* online book */
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
    ui_tap(1030, 611);assert(game_phase() == GAME_ONLINE_ROOMS);tick_pump(1);
    ui_tap(1144, 210);assert(game_phase() == GAME_WORKSHOP);
    ui_snapshot("workshop_from_online");
    ui_tap(1130, 74);assert(game_phase() == GAME_ONLINE_ROOMS);
    ui_tap(1140, 77);assert(game_phase() == GAME_MENU);
    isolate_saves(before, after, bytes);
    game_garden_export(garden_after);
    assert(!memcmp(garden, garden_after, sizeof garden));
    lvgl_ui_shutdown();
    game_set_lvgl_ui(0);
    on_net_shutdown();
    puts("Legacy and LVGL UI, native published-level list/play and multitouch controls, drag planting, both online roles and saves passed");
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
    static OnPublishedLevel draft, decoded;
    sample_level(&draft);
    assert(on_net_level_publish(&draft));tick_pump(1);
    OnNetView published = view();
    assert(!published.level_publish_busy && published.level_publish_id[0] &&
           strstr(published.level_publish_notice, "ОПУБЛИКОВАН") &&
           uploaded_level_body[0] && uploaded_index_body[0]);
    assert(on_protocol_published_level(uploaded_level_body,
                                       uploaded_level_id, &decoded));
    assert(!strcmp(decoded.title, draft.title));
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
    large_level_transport_round_trip();
    on_net_shutdown();
    puts("Native Firebase REST host/guest, both roles, coins, ACK and offline saves passed");
    return 0;
}
