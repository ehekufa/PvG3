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
    if (argc == 2) web_fixture(argv[1]);
    puts("Native online match, JSON protocol and optional browser fixture passed");
    return 0;
}
