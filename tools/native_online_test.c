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
    level.width = 16;level.height = 10;level.object_count = 19;
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
    level.objects[10] = (OnLevelObject){.id=11,.type=ON_LEVEL_TRIGGER,
        .x=30,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_GRAVITY,.trigger_event=ON_TRIGGER_MANUAL,
        .trigger_action=ON_TRIGGER_SET_GRAVITY,.target_id=0,
        .trigger_value=-65,.trigger_color=0xffc54eu};
    level.objects[11] = (OnLevelObject){.id=12,.type=ON_LEVEL_ORB_YELLOW,
        .x=10,.y=7,.w=.7f,.h=.7f,.color=0xfff400u,.visible=1};
    level.objects[12] = (OnLevelObject){.id=13,.type=ON_LEVEL_ORB_ORANGE,
        .x=11,.y=7,.w=.7f,.h=.7f,.color=0xff8a16u,.visible=1};
    level.objects[13] = (OnLevelObject){.id=14,.type=ON_LEVEL_PARTICLE,
        .x=12,.y=7,.w=1,.h=1,.color=0x68f0d8u,.visible=1,
        .emitter={.enabled=1,.continuous=0,.gravity_enabled=1,.glow=0,
            .rate=13,.lifetime=2.4f,.speed=177,.spread=72,.size=7,
            .direction=-45,.gravity=140}};
    level.objects[14] = (OnLevelObject){.id=15,.type=ON_LEVEL_CHECKPOINT,
        .x=8,.y=6,.w=1,.h=1,.color=0xb6d8ffu,.visible=1};
    level.objects[15] = (OnLevelObject){.id=16,.type=ON_LEVEL_PORTAL_NORMAL,
        .x=6,.y=6,.w=1,.h=1,.color=0xccccccu,.visible=1};
    level.objects[16] = (OnLevelObject){.id=17,.type=ON_LEVEL_PORTAL_JETPACK,
        .x=7,.y=6,.w=1,.h=1,.color=0x2f2f2fu,.visible=1};
    level.objects[17] = (OnLevelObject){.id=18,.type=ON_LEVEL_TRIGGER,
        .x=32,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_RECOLOR,.trigger_event=ON_TRIGGER_START,
        .trigger_action=ON_TRIGGER_RECOLOR,.trigger_group_id=42,
        .trigger_has_group=1,.trigger_color=0x8f31d4u};
    level.objects[18] = (OnLevelObject){.id=19,.type=ON_LEVEL_TRIGGER,
        .x=34,.y=-20,.w=1,.h=1,.color=0xf27652u,.visible=1,
        .trigger_kind=ON_TRIGGER_KIND_BACKGROUND,.trigger_event=ON_TRIGGER_START,
        .trigger_action=ON_TRIGGER_SET_BACKGROUND,.trigger_color=0x3456abu};
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
           strstr(body, "\"kind\":\"gravity\"") &&
           strstr(body, "\"action\":\"set-gravity\"") &&
           strstr(body, "\"kind\":\"recolor\"") &&
           strstr(body, "\"kind\":\"background\"") &&
           strstr(body, "\"action\":\"set-background\"") &&
           strstr(body, "\"value\":-65.0000") &&
           strstr(body, "\"type\":\"orb-yellow\"") &&
           strstr(body, "\"type\":\"orb-orange\"") &&
           strstr(body, "\"type\":\"particle\"") &&
           strstr(body, "\"type\":\"checkpoint\"") &&
           strstr(body, "\"type\":\"portal-normal\"") &&
           strstr(body, "\"type\":\"portal-jetpack\"") &&
           strstr(body, "\"gravityEnabled\":true") &&
           strstr(body, "\"continuous\":false") &&
           strstr(body, "\"rate\":13") &&
           strstr(body, "\"lifetime\":2.4") &&
           strstr(body, "\"speed\":177") &&
           strstr(body, "\"flipX\":true") && strstr(body, "\"flipY\":true") &&
           on_protocol_published_level(body, "23817", &decoded));
    assert(!strcmp(decoded.title, level.title) && decoded.object_count == 19);
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
    assert(decoded.objects[10].trigger_kind == ON_TRIGGER_KIND_GRAVITY &&
           decoded.objects[10].trigger_action == ON_TRIGGER_SET_GRAVITY &&
           decoded.objects[10].trigger_event == ON_TRIGGER_MANUAL &&
           decoded.objects[10].trigger_value == -65 &&
           !decoded.objects[10].trigger_has_group && decoded.objects[10].target_id == 0);
    assert(decoded.objects[17].trigger_kind == ON_TRIGGER_KIND_RECOLOR &&
           decoded.objects[17].trigger_action == ON_TRIGGER_RECOLOR &&
           decoded.objects[17].trigger_event == ON_TRIGGER_START &&
           decoded.objects[17].trigger_group_id == 42 &&
           decoded.objects[17].trigger_has_group &&
           decoded.objects[17].target_id == 0 &&
           decoded.objects[17].trigger_color == 0x8f31d4u);
    assert(decoded.objects[18].trigger_kind == ON_TRIGGER_KIND_BACKGROUND &&
           decoded.objects[18].trigger_action == ON_TRIGGER_SET_BACKGROUND &&
           decoded.objects[18].trigger_event == ON_TRIGGER_START &&
           !decoded.objects[18].trigger_has_group &&
           decoded.objects[18].target_id == 0 &&
           decoded.objects[18].trigger_color == 0x3456abu);
    assert(decoded.objects[11].type == ON_LEVEL_ORB_YELLOW &&
           !strcmp(decoded.objects[11].name, "Жёлтый орб") &&
           decoded.objects[12].type == ON_LEVEL_ORB_ORANGE &&
           !strcmp(decoded.objects[12].name, "Оранжевый орб") &&
           decoded.objects[13].type == ON_LEVEL_PARTICLE &&
           !strcmp(decoded.objects[13].name, "Эмиттер частиц") &&
           decoded.objects[14].type == ON_LEVEL_CHECKPOINT &&
           !strcmp(decoded.objects[14].name, "Чекпоинт") &&
           decoded.objects[15].type == ON_LEVEL_PORTAL_NORMAL &&
           !strcmp(decoded.objects[15].name, "Обычный портал") &&
           decoded.objects[16].type == ON_LEVEL_PORTAL_JETPACK &&
           !strcmp(decoded.objects[16].name, "Портал Jetpack"));
    assert(decoded.objects[13].emitter.enabled &&
           !decoded.objects[13].emitter.continuous &&
           decoded.objects[13].emitter.gravity_enabled &&
           !decoded.objects[13].emitter.glow &&
           decoded.objects[13].emitter.rate == 13 &&
           decoded.objects[13].emitter.lifetime == 2.4f &&
           decoded.objects[13].emitter.speed == 177 &&
           decoded.objects[13].emitter.direction == -45);
    char legacy_without_emitter[8192];
    const char *emitter_start = strstr(body, ",\"emitter\":{");
    const char *emitter_end = emitter_start ? strchr(emitter_start, '}') : NULL;
    assert(emitter_start && emitter_end);
    int no_emitter_size = snprintf(legacy_without_emitter,
        sizeof legacy_without_emitter, "%.*s%s",
        (int)(emitter_start - body), body, emitter_end + 1);
    assert(no_emitter_size > 0 && (size_t)no_emitter_size < sizeof legacy_without_emitter);
    static OnPublishedLevel legacy_particle_level;
    assert(on_protocol_published_level(legacy_without_emitter, "23817",
                                       &legacy_particle_level));
    OnLevelParticle default_emitter = on_level_particle_default();
    assert(legacy_particle_level.objects[13].emitter.rate == default_emitter.rate &&
           legacy_particle_level.objects[13].emitter.lifetime == default_emitter.lifetime &&
           legacy_particle_level.objects[13].emitter.direction == default_emitter.direction);
    level.objects[13].emitter.rate = 31;
    assert(!on_protocol_published_level_json(&level, body, sizeof body));
    level.objects[13].emitter.rate = 13;
    level.objects[10].trigger_value = 101;
    assert(!on_protocol_published_level_json(&level, body, sizeof body));
    level.objects[10].trigger_value = -65;
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
                   i == 6 ? ON_LEVEL_ENEMY : i == 7 ? ON_LEVEL_SLOPE :
                   i == 8 ? ON_LEVEL_ORB_YELLOW : i == 9 ? ON_LEVEL_ORB_ORANGE :
                   i == 10 ? ON_LEVEL_PARTICLE : i == 11 ? ON_LEVEL_CHECKPOINT :
                   i == 12 ? ON_LEVEL_PORTAL_NORMAL :
                   i == 13 ? ON_LEVEL_PORTAL_JETPACK : ON_LEVEL_TRIGGER;
        OnLevelObject *object = &level.objects[i];
        *object = (OnLevelObject){.id=i + 1,.type=type,
            .x=(float)(i % 16),.y=(float)((i / 16) % 10),.w=1,.h=1,
            .color=0x55c8eau,.number=i % 10000,.visible=1};
        if (type == ON_LEVEL_PARTICLE)
            object->emitter = on_level_particle_default();
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
           decoded.objects[8].type == ON_LEVEL_ORB_YELLOW &&
           decoded.objects[9].type == ON_LEVEL_ORB_ORANGE &&
           decoded.objects[10].type == ON_LEVEL_PARTICLE &&
           decoded.objects[11].type == ON_LEVEL_CHECKPOINT &&
           decoded.objects[12].type == ON_LEVEL_PORTAL_NORMAL &&
           decoded.objects[13].type == ON_LEVEL_PORTAL_JETPACK &&
           decoded.objects[14].type == ON_LEVEL_TRIGGER &&
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
    assert(on_level_id_is_official(ON_LEVEL_OFFICIAL_ID));
    assert(on_level_id_is_official("338069"));
    assert(!on_level_id_is_official("338068"));
    assert(!on_level_id_is_official("1338069"));
    match_codec();
    rooms_and_commands();
    published_level_writer();
    published_level_object_limit();
    if (argc == 2) web_fixture(argv[1]);
    puts("Native online match, JSON protocol and optional browser fixture passed");
    return 0;
}
