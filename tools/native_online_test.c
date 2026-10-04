/* Native Firebase protocol and authoritative match regressions.
 * No Android SDK and no live database are needed for these tests. */
#include "online_protocol.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void match_codec(void) {
    OnMatch m, decoded;
    on_match_new(&m, 5);
    assert(on_match_valid(&m));
    assert(m.plant_cash == 250 && m.zombie_cash == 175 && m.left == 18);
    char *body = malloc(ON_STATE_JSON_CAP);
    assert(body);
    size_t size = on_protocol_match_json(&m, body, ON_STATE_JSON_CAP);
    assert(size > 800 && strstr(body, "\"plants\":[false,false"));
    assert(strstr(body, "\"ducks\":[false]"));
    assert(on_protocol_match(body, &decoded) && on_match_valid(&decoded));
    assert(decoded.plants[0].type == -1 && decoded.duck_count == 0);
    assert(!on_protocol_match_json(&m, body, 30));

    OnCommand c = {.kind=ON_CMD_PLANT,.row=1,.col=2,.type=ON_PEA_PLANT};
    assert(!on_match_apply(&m, ON_ROLE_PLANTS, &c)); /* no planting directly in water */
    c.type = ON_LILY;
    assert(on_match_apply(&m, ON_ROLE_PLANTS, &c));
    c.type = ON_PEA_PLANT;
    assert(on_match_apply(&m, ON_ROLE_PLANTS, &c));
    assert(m.plants[11].type == ON_PEA_PLANT && m.lilies[11] == 1);

    OnCommand enemy = {.kind=ON_CMD_SPAWN,.row=1,.type=ON_CONE};
    assert(on_match_apply(&m, ON_ROLE_ZOMBIES, &enemy));
    assert(m.ducks[0].hp == 420 && m.left == 17);
    for (int i = 0; i < 120; i++) on_match_step(&m, .05f);
    assert(m.zombie_cash == 150);  /* passive coins after 5 seconds */
    assert(m.duck_count == 1);
    assert(on_protocol_match_json(&m, body, ON_STATE_JSON_CAP));
    assert(on_protocol_match(body, &decoded));
    assert(decoded.ducks[0].hp == m.ducks[0].hp &&
           decoded.plants[11].type == ON_PEA_PLANT);
    assert(fabsf(decoded.time - m.time) < 0.001f);
    assert(!on_protocol_match("{\"version\":1,\"plants\":[]}", &decoded));
    assert(!on_protocol_match("not json", &decoded));
    assert(!on_protocol_match("{\"a\": [null,]}", &decoded));
    assert(!on_protocol_match("{\"a\":NaN}", &decoded));
    free(body);
}
static void rooms_and_commands(void) {
    const char *host = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char *guest = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(on_protocol_valid_room_id("NJYK64"));
    assert(!on_protocol_valid_room_id("bad"));
    assert(!on_protocol_valid_room_id("ABC012"));
    assert(on_protocol_valid_player_id(host));
    char room[2048];
    snprintf(room, sizeof room,
             "{\"version\":1,\"map\":1,\"host\":{\"id\":\"%s\",\"ping\":1780000000000},"
             "\"guest\":{\"id\":\"%s\",\"role\":\"zombies\",\"ping\":1780000000000},"
             "\"command\":{\"id\":\"%s\",\"kind\":\"spawn\",\"row\":2,\"type\":3,\"seq\":1}}",
             host, guest, guest);
    OnRoomData data;
    assert(on_protocol_room(room, &data));
    assert(data.present && data.guest_role == ON_ROLE_ZOMBIES);
    assert(data.has_command && data.command.type == ON_HELMET && data.command.seq == 1);
    assert(!on_protocol_room("{\"version\":7}", &data));
    assert(on_protocol_room("null", &data) && !data.present);
    OnCommand cmd = {.kind=ON_CMD_COIN,.id=45,.seq=4};
    char wire[256];
    assert(on_protocol_command_json(&cmd, guest, wire, sizeof wire));
    assert(strstr(wire, "\"coinId\":45"));
    assert(strstr(wire, guest));
    assert(!on_protocol_command_json(&cmd, "not-an-id", wire, sizeof wire));

    const char *list =
        "{\"ZZZZZZ\":{\"version\":1,\"map\":1,\"host\":{"
        "\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"ping\":1779999999999}},"
        "\"AABBCC\":{\"version\":1,\"map\":5,\"host\":{"
        "\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"ping\":1780000000100}},"
        "\"BUSY34\":{\"version\":1,\"map\":1,\"host\":{"
        "\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"ping\":1780000000100},"
        "\"guest\":{\"id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}},"
        "\"EXPIRED\":{\"version\":1,\"map\":1,\"host\":{"
        "\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"ping\":1779999000000}}}";
    OnRoomSummary items[ON_ROOM_LIST_CAP];
    memset(items, 0, sizeof(items));
    int n = on_protocol_rooms(list, items, ON_ROOM_LIST_CAP, 1780000000100LL);
    assert(n == 2);
    assert(!strcmp(items[0].id, "AABBCC") && items[0].map == 5);
    assert(!strcmp(items[1].id, "ZZZZZZ"));
    assert(on_protocol_rooms("null", items, ON_ROOM_LIST_CAP, 1780000000100LL) == 0);
}
static void published_level_writer(void) {
    static OnPublishedLevel level, decoded;
    memset(&level, 0, sizeof level);memset(&decoded, 0, sizeof decoded);
    snprintf(level.id, sizeof level.id, "%s", "23817");
    snprintf(level.title, sizeof level.title, "%s", "Проверка \"уровня\"");
    snprintf(level.description, sizeof level.description, "%s", "Маршрут и триггер.");
    level.width = 16;level.height = 10;level.object_count = 10;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.color=0x65a845u,.visible=1};
    snprintf(level.objects[0].name, sizeof level.objects[0].name, "%s", "Платформа");
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=1,.y=7,.w=.65f,.h=.85f,.color=0x5ab7e8u,.visible=1};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.color=0x69d16cu,.visible=1};
    level.objects[3] = (OnLevelObject){.id=4,.type=ON_LEVEL_TRIGGER,
        .x=-ON_LEVEL_WORLD_LIMIT,.y=ON_LEVEL_WORLD_LIMIT - 1,.w=1,.h=1,
        .color=0xf27652u,.visible=1,.trigger_kind=ON_TRIGGER_KIND_MOVE,
        .trigger_event=ON_TRIGGER_MANUAL,.trigger_action=ON_TRIGGER_MOVE,
        .target_id=3,.trigger_value=9999,.trigger_value_y=-9999,
        .trigger_color=0xffc54eu,.trigger_group_id=42,.trigger_has_group=1};
    level.objects[4] = (OnLevelObject){.id=5,.type=ON_LEVEL_TRIGGER,
        .x=20,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_ROTATE,.trigger_event=ON_TRIGGER_MANUAL,
        .trigger_action=ON_TRIGGER_ROTATE,.target_id=3,.trigger_duration=4,
        .trigger_has_duration=1,.trigger_color=0xffc54eu,
        .trigger_group_id=42,.trigger_has_group=1};
    level.objects[5] = (OnLevelObject){.id=6,.type=ON_LEVEL_TRIGGER,
        .x=22,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_FOREVER,.trigger_event=ON_TRIGGER_TOUCH,
        .trigger_action=ON_TRIGGER_UNACTIVATE,.target_id=0,
        .trigger_color=0xffc54eu,.trigger_group_id=42,.trigger_has_group=1};
    level.objects[6] = (OnLevelObject){.id=7,.type=ON_LEVEL_TRIGGER,
        .x=24,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_INVISIBILITY,.trigger_event=ON_TRIGGER_TOUCH,
        .trigger_action=ON_TRIGGER_INVISIBLE,.target_id=3,
        .trigger_color=0xffc54eu,.trigger_group_id=42,.trigger_has_group=1};
    level.objects[7] = (OnLevelObject){.id=8,.type=ON_LEVEL_TRIGGER,
        .x=26,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_NO_COLLISION,.trigger_event=ON_TRIGGER_MANUAL,
        .trigger_action=ON_TRIGGER_NO_COLLISION,.target_id=0,
        .trigger_color=0xffc54eu,.trigger_group_id=42,.trigger_has_group=1};
    level.objects[8] = (OnLevelObject){.id=9,.type=ON_LEVEL_TRIGGER,
        .x=28,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_INVISIBILITY,.trigger_event=ON_TRIGGER_START,
        .trigger_action=ON_TRIGGER_INVISIBLE,.target_id=3,
        .trigger_color=0xffc54eu,.trigger_group_id=42,.trigger_has_group=1};
    level.objects[9] = (OnLevelObject){.id=10,.type=ON_LEVEL_SLOPE,
        .x=9,.y=7,.w=1,.h=1,.color=0xe56c5bu,.visible=1,
        .flip_x=1,.flip_y=1};
    char body[8192], index[1024];
    size_t size = on_protocol_published_level_json(&level, body, sizeof body);
    assert(size && strstr(body, "PVG3-PUBLISHED-LEVEL") &&
           strstr(body, "\\\"уровня\\\"") &&
           strstr(body, "\"valueX\":9999.0000") &&
           strstr(body, "\"valueY\":-9999.0000") &&
           strstr(body, "\"duration\":4") &&
           strstr(body, "\"groupId\":42") &&
           strstr(body, "\"action\":\"unactivate\"") &&
           strstr(body, "\"kind\":\"invisibility\"") &&
           strstr(body, "\"action\":\"invisible\"") &&
           strstr(body, "\"kind\":\"no-collision\"") &&
           strstr(body, "\"action\":\"no-collision\"") &&
           strstr(body, "\"event\":\"start\"") &&
           strstr(body, "\"type\":\"slope\"") &&
           strstr(body, "\"flipX\":true") && strstr(body, "\"flipY\":true") &&
           on_protocol_published_level(body, "23817", &decoded));
    assert(!strcmp(decoded.title, level.title) && decoded.object_count == 10);
    assert(decoded.objects[3].trigger_kind == ON_TRIGGER_KIND_MOVE &&
           decoded.objects[3].trigger_value == 9999 &&
           decoded.objects[3].trigger_value_y == -9999 &&
           decoded.objects[3].trigger_group_id == 42 &&
           decoded.objects[3].trigger_has_group);
    assert(decoded.objects[4].trigger_kind == ON_TRIGGER_KIND_ROTATE &&
           decoded.objects[4].trigger_action == ON_TRIGGER_ROTATE &&
           decoded.objects[4].trigger_duration == 4 &&
           decoded.objects[4].trigger_has_duration &&
           decoded.objects[4].trigger_group_id == 42 &&
           decoded.objects[4].trigger_color == 0xffc54eu);
    char legacy_body[8192];
    const char *duration_field = strstr(body, "\"duration\":4");
    assert(duration_field);
    size_t duration_len = strlen("\"duration\":4");
    int legacy_size = snprintf(legacy_body, sizeof legacy_body,
        "%.*s\"degrees\":270.0000,\"value\":270.0000%s",
        (int)(duration_field - body), body, duration_field + duration_len);
    assert(legacy_size > 0 && (size_t)legacy_size < sizeof legacy_body);
    static OnPublishedLevel legacy_level;
    assert(on_protocol_published_level(legacy_body, "23817", &legacy_level));
    assert(legacy_level.objects[4].trigger_kind == ON_TRIGGER_KIND_ROTATE &&
           !legacy_level.objects[4].trigger_has_duration &&
           legacy_level.objects[4].trigger_value == 270);
    assert(decoded.objects[5].trigger_kind == ON_TRIGGER_KIND_FOREVER &&
           decoded.objects[5].trigger_action == ON_TRIGGER_UNACTIVATE &&
           decoded.objects[5].trigger_group_id == 42 &&
           decoded.objects[5].trigger_has_group);
    assert(decoded.objects[6].trigger_kind == ON_TRIGGER_KIND_INVISIBILITY &&
           decoded.objects[6].trigger_action == ON_TRIGGER_INVISIBLE &&
           decoded.objects[6].trigger_group_id == 42 &&
           decoded.objects[6].trigger_has_group);
    assert(decoded.objects[7].trigger_kind == ON_TRIGGER_KIND_NO_COLLISION &&
           decoded.objects[7].trigger_action == ON_TRIGGER_NO_COLLISION &&
           decoded.objects[7].trigger_event == ON_TRIGGER_MANUAL &&
           decoded.objects[7].trigger_group_id == 42 &&
           decoded.objects[7].trigger_has_group);
    assert(decoded.objects[8].trigger_kind == ON_TRIGGER_KIND_INVISIBILITY &&
           decoded.objects[8].trigger_event == ON_TRIGGER_START &&
           decoded.objects[8].trigger_action == ON_TRIGGER_INVISIBLE &&
           decoded.objects[8].trigger_group_id == 42 &&
           decoded.objects[8].trigger_has_group);
    assert(decoded.objects[9].type == ON_LEVEL_SLOPE &&
           !strcmp(decoded.objects[9].name, "Склон") &&
           decoded.objects[9].flip_x && decoded.objects[9].flip_y);
    static const char legacy_without_flips[] =
        "{\"format\":\"PVG3-PUBLISHED-LEVEL\",\"version\":1,\"id\":\"23817\","
        "\"title\":\"Legacy\",\"description\":\"\",\"project\":{"
        "\"format\":\"PVG3-MAKER\",\"version\":1,\"width\":16,\"height\":10,"
        "\"objects\":["
        "{\"id\":1,\"type\":\"player\",\"name\":\"Игрок\",\"x\":1,\"y\":7,"
        "\"w\":0.65,\"h\":0.85,\"angle\":0,\"color\":\"#5ab7e8\","
        "\"number\":1,\"visible\":true},"
        "{\"id\":2,\"type\":\"goal\",\"name\":\"Финиш\",\"x\":14,\"y\":6,"
        "\"w\":1,\"h\":2,\"angle\":0,\"color\":\"#69d16c\","
        "\"number\":2,\"visible\":true}]}}";
    static OnPublishedLevel old_record;
    assert(on_protocol_published_level(legacy_without_flips, "23817", &old_record));
    assert(old_record.object_count == 2 && !old_record.objects[0].flip_x &&
           !old_record.objects[0].flip_y && !old_record.objects[1].flip_x &&
           !old_record.objects[1].flip_y);
    assert(on_protocol_level_summary_json(&level, index, sizeof index, 1234));
    assert(strstr(index, "\"updatedAt\":1234") && strstr(index, "23817"));
    static OnPublishedLevel scaled;
    scaled = level;
    scaled.objects[0].y = 0;scaled.objects[0].w = 64;scaled.objects[0].h = 40;
    assert(on_protocol_published_level_json(&scaled, body, sizeof body));
    assert(on_protocol_published_level(body, "23817", &decoded));
    assert(decoded.objects[0].w == 64 && decoded.objects[0].h == 40);
    level.objects[2].id = level.objects[1].id;
    assert(!on_protocol_published_level_json(&level, body, sizeof body));
    level.objects[2].id = 3;
    level.objects[1].type = ON_LEVEL_BLOCK;
    assert(!on_protocol_published_level_json(&level, body, sizeof body));
    level.objects[1].type = ON_LEVEL_PLAYER;
    memset(level.title, 'x', 81);level.title[81] = 0;
    assert(!on_protocol_published_level_json(&level, body, sizeof body));
    level.title[80] = 0;
    assert(on_protocol_published_level_json(&level, body, sizeof body));
    level.title[0] = (char)0xff;level.title[1] = 0;
    assert(!on_protocol_published_level_json(&level, body, sizeof body));
}
static void published_level_object_limit(void) {
    static OnPublishedLevel level, decoded;
    memset(&level, 0, sizeof level);
    snprintf(level.id, sizeof level.id, "%s", "654321");
    snprintf(level.title, sizeof level.title, "%s", "20 000 объектов");
    snprintf(level.description, sizeof level.description, "%s", "Общий лимит всех типов.");
    level.width = 16;level.height = 10;level.object_count = ON_LEVEL_OBJECT_CAP;
    for (int i = 0; i < ON_LEVEL_OBJECT_CAP; ++i) {
        int type = i == 0 ? ON_LEVEL_PLAYER : i == 1 ? ON_LEVEL_GOAL :
                   i == 2 ? ON_LEVEL_GROUND : i == 3 ? ON_LEVEL_BLOCK :
                   i == 4 ? ON_LEVEL_HAZARD : i == 5 ? ON_LEVEL_COIN :
                   i == 6 ? ON_LEVEL_ENEMY : i == 7 ? ON_LEVEL_SLOPE : ON_LEVEL_TRIGGER;
        OnLevelObject *object = &level.objects[i];
        *object = (OnLevelObject){.id=i + 1,.type=type,
            .x=(float)(i % 16),.y=(float)((i / 16) % 10),.w=1,.h=1,
            .color=0x55c8eau,.number=i % 10000,.visible=1};
        if (type == ON_LEVEL_TRIGGER) {
            object->trigger_kind = ON_TRIGGER_KIND_MOVE;
            object->trigger_event = ON_TRIGGER_TOUCH;
            object->trigger_action = ON_TRIGGER_MOVE;
            object->target_id = 2;
            object->trigger_value = 1;
            object->trigger_color = 0xffc54eu;
        }
    }
    char *body = (char *)malloc(ON_LEVEL_JSON_CAP);
    assert(body);
    size_t measured = on_protocol_published_level_json(&level, NULL, 0);
    size_t size = on_protocol_published_level_json(&level, body, ON_LEVEL_JSON_CAP);
    assert(measured == size && size > 1024u * 1024u && size < ON_LEVEL_JSON_CAP);
    assert(on_protocol_published_level(body, level.id, &decoded));
    assert(decoded.object_count == ON_LEVEL_OBJECT_CAP);
    assert(decoded.objects[0].type == ON_LEVEL_PLAYER &&
           decoded.objects[1].type == ON_LEVEL_GOAL &&
           decoded.objects[7].type == ON_LEVEL_SLOPE &&
           decoded.objects[8].type == ON_LEVEL_TRIGGER &&
           decoded.objects[ON_LEVEL_OBJECT_CAP - 1].id == ON_LEVEL_OBJECT_CAP);
    free(body);

    const char header[] =
        "{\"format\":\"PVG3-PUBLISHED-LEVEL\",\"version\":1,\"id\":\"1\","
        "\"title\":\"x\",\"project\":{\"format\":\"PVG3-MAKER\",\"version\":1,"
        "\"width\":16,\"height\":10,\"objects\":[";
    const char footer[] = "]}}";
    size_t over_cap = sizeof header + (size_t)(ON_LEVEL_OBJECT_CAP + 1) * 5 + sizeof footer;
    char *too_many = (char *)malloc(over_cap);
    assert(too_many);
    size_t at = sizeof header - 1;
    memcpy(too_many, header, at);
    for (int i = 0; i <= ON_LEVEL_OBJECT_CAP; ++i) {
        if (i) too_many[at++] = ',';
        memcpy(too_many + at, "null", 4);at += 4;
    }
    memcpy(too_many + at, footer, sizeof footer);
    assert(!on_protocol_published_level(too_many, "1", &decoded));
    free(too_many);
}
static void web_fixture(const char *path) {
    FILE *file = fopen(path, "rb");assert(file);
    char wire[ON_STATE_JSON_CAP];
    size_t n = fread(wire, 1, sizeof wire - 1, file);
    assert(!ferror(file) && feof(file));fclose(file);
    wire[n] = 0;
    OnMatch m;
    assert(on_protocol_match(wire, &m));
    assert(m.map == 5 && m.plant_cash == 125 && m.zombie_cash == 75 &&
           m.plants[11].type == ON_PEA_PLANT && m.lilies[11] &&
           m.duck_count == 1 && m.ducks[0].type == ON_CONE);
}
int main(int argc, char **argv) {
    match_codec();
    rooms_and_commands();
    published_level_writer();
    published_level_object_limit();
    if (argc == 2) web_fixture(argv[1]);
    puts("Native online match, JSON protocol and optional browser fixture passed");
    return 0;
}
