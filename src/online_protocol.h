#ifndef PVG3_ONLINE_PROTOCOL_H
#define PVG3_ONLINE_PROTOCOL_H

#include "online_rules.h"
#include "online_level.h"

#include <stddef.h>
#include <stdint.h>

#define ON_ROOM_ID_SIZE 7
#define ON_PLAYER_ID_SIZE 33
#define ON_ROOM_LIST_CAP 24
#define ON_STATE_JSON_CAP 65536
/* Enough for 20,000 serialized objects plus the enclosing published-level
 * record and its strings. Includes one byte for the terminating NUL. */
#define ON_LEVEL_JSON_CAP (16u * 1024u * 1024u + 1u)

typedef struct {
    char id[ON_ROOM_ID_SIZE];
    int map;
    int64_t ping;
} OnRoomSummary;

typedef struct {
    int present, version, map;
    char host_id[ON_PLAYER_ID_SIZE], guest_id[ON_PLAYER_ID_SIZE];
    int host_role, guest_role;
    int64_t host_ping, guest_ping;
    int has_state, has_command;
    OnMatch state;
    OnCommand command;
} OnRoomData;

int on_protocol_valid_room_id(const char *id);
int on_protocol_valid_player_id(const char *id);
/* Return -1 on malformed/overlong JSON; otherwise the number of available
 * rooms (0..cap), sorted by most recent heartbeat. Occupied/stale rooms are
 * hidden, as in the web client; no rooms are modified by listing/search. */
int on_protocol_rooms(const char *json, OnRoomSummary *out, int cap, int64_t now_ms);
/* Parse a single room, including state and command. Returns 1 for a valid
 * occupied/empty room, 0 for null or structurally invalid data. */
int on_protocol_room(const char *json, OnRoomData *out);
/* Exposed separately for cross-platform fixtures and malformed-data tests. */
int on_protocol_match(const char *json, OnMatch *out);
/* Serialize with false sentinels for empty lists/null plants: Firebase drops
 * empty arrays and nulls. On the wire this matches online/firebase.js. */
size_t on_protocol_match_json(const OnMatch *s, char *out, size_t cap);
size_t on_protocol_command_json(const OnCommand *c, const char *player_id,
                                char *out, size_t cap);
/* Serialize a bounded PVG3-PUBLISHED-LEVEL record for native workshop upload.
 * Passing out == NULL and cap == 0 measures the required JSON byte count. */
size_t on_protocol_published_level_json(const OnPublishedLevel *level,
                                        char *out, size_t cap);
size_t on_protocol_level_summary_json(const OnPublishedLevel *level,
                                      char *out, size_t cap, int64_t updated_at);

#endif
