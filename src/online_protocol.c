/* Bounded JSON codec for the existing Firebase browser protocol. There is no
 * JavaScript runtime, embedded browser, API key or service account in the APK.
 * Everything received from the public database is length- and type-checked. */
#include "online_protocol.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_BYTES (1024u * 1024u)
#define JSON_MAX_TOKENS 32768
#define JSON_MAX_DEPTH 32

typedef struct { char type; int start, end, after, count; } JT;
typedef struct { const char *s; size_t n, at; JT *t; int used, cap, bad; } JD;

static void ws(JD *d) {
    while (d->at < d->n && (d->s[d->at] == ' ' || d->s[d->at] == '\t' ||
                             d->s[d->at] == '\n' || d->s[d->at] == '\r')) d->at++;
}
static int token(JD *d, char type, int start) {
    if (d->used >= d->cap) { d->bad = 1; return -1; }
    int at = d->used++;
    d->t[at] = (JT){type, start, 0, 0, 0};
    return at;
}
static int parse_value(JD *d, int depth);
static int parse_string(JD *d) {
    if (d->at >= d->n || d->s[d->at++] != '"') { d->bad = 1; return -1; }
    int t = token(d, 's', (int)d->at);
    if (t < 0) return -1;
    while (d->at < d->n) {
        unsigned char ch = (unsigned char)d->s[d->at++];
        if (ch == '"') {
            d->t[t].end = (int)d->at - 1;
            d->t[t].after = d->used;
            return t;
        }
        if (ch < 32) break;
        if (ch == '\\') {
            if (d->at >= d->n) break;
            char esc = d->s[d->at++];
            if (strchr("\"\\/bfnrt", esc)) continue;
            if (esc != 'u' || d->at + 4 > d->n) break;
            for (int i = 0; i < 4; i++)
                if (!isxdigit((unsigned char)d->s[d->at + i])) { d->bad = 1; return -1; }
            d->at += 4;
        }
    }
    d->bad = 1;
    return -1;
}
static int literal(JD *d, const char *s, char type) {
    size_t length = strlen(s);
    if (d->at + length > d->n || memcmp(d->s + d->at, s, length)) {
        d->bad = 1; return -1;
    }
    int t = token(d, type, (int)d->at);
    if (t < 0) return -1;
    d->at += length;
    d->t[t].end = (int)d->at;
    d->t[t].after = d->used;
    return t;
}
static int number(JD *d) {
    int start = (int)d->at;
    if (d->at < d->n && d->s[d->at] == '-') d->at++;
    if (d->at == d->n) { d->bad = 1; return -1; }
    if (d->s[d->at] == '0') d->at++;
    else if (d->s[d->at] >= '1' && d->s[d->at] <= '9') {
        while (d->at < d->n && isdigit((unsigned char)d->s[d->at])) d->at++;
    } else { d->bad = 1; return -1; }
    if (d->at < d->n && d->s[d->at] == '.') {
        d->at++;
        if (d->at == d->n || !isdigit((unsigned char)d->s[d->at])) { d->bad = 1; return -1; }
        while (d->at < d->n && isdigit((unsigned char)d->s[d->at])) d->at++;
    }
    if (d->at < d->n && (d->s[d->at] == 'e' || d->s[d->at] == 'E')) {
        d->at++;
        if (d->at < d->n && (d->s[d->at] == '+' || d->s[d->at] == '-')) d->at++;
        if (d->at == d->n || !isdigit((unsigned char)d->s[d->at])) { d->bad = 1; return -1; }
        while (d->at < d->n && isdigit((unsigned char)d->s[d->at])) d->at++;
    }
    int t = token(d, 'n', start);
    if (t < 0) return -1;
    d->t[t].end = (int)d->at;
    d->t[t].after = d->used;
    return t;
}
static int parse_value(JD *d, int depth) {
    ws(d);
    if (d->bad || d->at >= d->n || depth > JSON_MAX_DEPTH) { d->bad = 1; return -1; }
    char ch = d->s[d->at];
    if (ch == '"') return parse_string(d);
    if (ch == 't') return literal(d, "true", 't');
    if (ch == 'f') return literal(d, "false", 'f');
    if (ch == 'n') return literal(d, "null", 'z');
    if (ch == '-' || isdigit((unsigned char)ch)) return number(d);
    if (ch != '{' && ch != '[') { d->bad = 1; return -1; }
    int t = token(d, ch == '{' ? 'o' : 'a', (int)d->at++);
    if (t < 0) return -1;
    ws(d);
    if (d->at < d->n && d->s[d->at] == (ch == '{' ? '}' : ']')) {
        d->at++;d->t[t].end = (int)d->at;d->t[t].after = d->used;return t;
    }
    for (;;) {
        if (ch == '{') {
            if (parse_string(d) < 0) return -1;
            ws(d);
            if (d->at == d->n || d->s[d->at++] != ':') {d->bad = 1;return -1;}
        }
        if (parse_value(d, depth + 1) < 0) return -1;
        d->t[t].count++;
        ws(d);
        if (d->at >= d->n) {d->bad = 1;return -1;}
        if (d->s[d->at] == (ch == '{' ? '}' : ']')) {
            d->at++;d->t[t].end = (int)d->at;d->t[t].after = d->used;return t;
        }
        if (d->s[d->at++] != ',') {d->bad = 1;return -1;}
        ws(d);
        if (d->at == d->n || d->s[d->at] == (ch == '{' ? '}' : ']')) {
            d->bad = 1;return -1; /* trailing comma */
        }
    }
}
static int doc_open(JD *d, const char *s) {
    if (!d || !s) return 0;
    size_t n = strlen(s);
    if (n > JSON_MAX_BYTES) return 0;
    memset(d, 0, sizeof(*d));
    d->t = (JT *)calloc(JSON_MAX_TOKENS, sizeof(JT));
    if (!d->t) return 0;
    d->s = s;d->n = n;d->cap = JSON_MAX_TOKENS;
    int root = parse_value(d, 0);
    ws(d);
    if (root != 0 || d->bad || d->at != n) {
        free(d->t);d->t = NULL;return 0;
    }
    return 1;
}
static int eq(const JD *d, int i, const char *name) {
    return i >= 0 && i < d->used && d->t[i].type == 's' &&
           d->t[i].end - d->t[i].start == (int)strlen(name) &&
           !memcmp(d->s + d->t[i].start, name, strlen(name));
}
static int field(const JD *d, int obj, const char *name) {
    if (obj < 0 || obj >= d->used || d->t[obj].type != 'o') return -1;
    for (int i = obj + 1; i < d->t[obj].after;) {
        int v = i + 1;
        if (v >= d->used) return -1;
        if (eq(d, i, name)) return v;
        i = d->t[v].after;
    }
    return -1;
}
static int str(const JD *d, int t, char *out, size_t cap) {
    if (t < 0 || t >= d->used || d->t[t].type != 's') return 0;
    int len = d->t[t].end - d->t[t].start;
    if (len < 0 || (size_t)len >= cap) return 0;
    for (int i = 0; i < len; i++) {
        char ch = d->s[d->t[t].start + i];
        if (ch == '\\') return 0; /* IDs and roles are always plain ASCII */
        out[i] = ch;
    }
    out[len] = 0;
    return 1;
}
static int num(const JD *d, int t, double *out) {
    if (t < 0 || t >= d->used || d->t[t].type != 'n') return 0;
    char *end;
    double n = strtod(d->s + d->t[t].start, &end);
    if (end != d->s + d->t[t].end || !isfinite(n)) return 0;
    *out = n;return 1;
}
static int int_field(const JD *d, int obj, const char *key, int *out) {
    double v;
    if (!num(d, field(d, obj, key), &v) || v < -2000000000.0 ||
        v > 2000000000.0 || floor(v) != v) return 0;
    *out = (int)v;return 1;
}
static int float_field(const JD *d, int obj, const char *key, float *out) {
    double v;
    if (!num(d, field(d, obj, key), &v) || v < -1e9 || v > 1e9) return 0;
    *out = (float)v;return isfinite(*out);
}
static int bool_field(const JD *d, int obj, const char *key, int *out) {
    int t = field(d, obj, key);
    if (t < 0) return 0;
    if (d->t[t].type == 't') { *out = 1; return 1; }
    if (d->t[t].type == 'f') { *out = 0; return 1; }
    return 0;
}
static int role(const JD *d, int t, int *out) {
    if (t < 0 || d->t[t].type == 'z') { *out = ON_NO_ROLE;return 1; }
    if (eq(d, t, "plants")) {*out = ON_ROLE_PLANTS;return 1;}
    if (eq(d, t, "zombies")) {*out = ON_ROLE_ZOMBIES;return 1;}
    return 0;
}
int on_protocol_valid_room_id(const char *id) {
    if (!id || strlen(id) != 6) return 0;
    for (int i = 0; i < 6; i++)
        if ((id[i] < 'A' || id[i] > 'Z') && (id[i] < '2' || id[i] > '9')) return 0;
    return 1;
}
int on_protocol_valid_player_id(const char *id) {
    if (!id || strlen(id) != 32) return 0;
    for (int i = 0; i < 32; i++)
        if (!isxdigit((unsigned char)id[i])) return 0;
    return 1;
}
static int match(const JD *d, int node, OnMatch *out) {
    if (node < 0 || d->t[node].type != 'o' || !out) return 0;
    OnMatch s;on_match_new(&s, 1);
    if (!int_field(d, node, "version", &s.version) ||
        !int_field(d, node, "map", &s.map) ||
        !float_field(d, node, "time", &s.time) ||
        !int_field(d, node, "plantCash", &s.plant_cash) ||
        !int_field(d, node, "zombieCash", &s.zombie_cash) ||
        !float_field(d, node, "zombieIncome", &s.zombie_income) ||
        !int_field(d, node, "left", &s.left) ||
        !int_field(d, node, "nextId", &s.next_id) ||
        !int_field(d, node, "ackGuest", &s.ack_guest)) return 0;
    int w = field(d, node, "winner");
    if (eq(d, w, "plants")) s.winner = ON_WIN_PLANTS;
    else if (eq(d, w, "zombies")) s.winner = ON_WIN_ZOMBIES;
    else if (!eq(d, w, "")) return 0;
    int a = field(d, node, "plants");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count != ON_CELLS) return 0;
    int i = a + 1;
    for (int p = 0; p < ON_CELLS; p++) {
        if (d->t[i].type == 'o') {
            if (!int_field(d, i, "type", &s.plants[p].type) ||
                !float_field(d, i, "hp", &s.plants[p].hp) ||
                !float_field(d, i, "fire", &s.plants[p].fire)) return 0;
        } else if (d->t[i].type != 'f' && d->t[i].type != 'z') return 0;
        i = d->t[i].after;
    }
    a = field(d, node, "lilies");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count != ON_CELLS) return 0;
    i = a + 1;
    for (int p = 0; p < ON_CELLS; p++) {
        if (d->t[i].type != 't' && d->t[i].type != 'f') return 0;
        s.lilies[p] = d->t[i].type == 't';
        i = d->t[i].after;
    }
    a = field(d, node, "ducks");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count > ON_DUCK_CAP) return 0;
    i = a + 1;
    if (!(d->t[a].count == 1 && d->t[i].type == 'f'))
        for (int p = 0; p < d->t[a].count; p++) {
            if (d->t[i].type != 'o') return 0;
            OnDuck *z = &s.ducks[s.duck_count++];
            if (!int_field(d, i, "id", &z->id) ||
                !int_field(d, i, "type", &z->type) ||
                !int_field(d, i, "row", &z->row) ||
                !float_field(d, i, "x", &z->x) ||
                !float_field(d, i, "hp", &z->hp) ||
                !float_field(d, i, "maxHp", &z->max_hp) ||
                !float_field(d, i, "speed", &z->speed) ||
                !float_field(d, i, "anim", &z->anim)) return 0;
            i = d->t[i].after;
        }
    a = field(d, node, "peas");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count > ON_PEA_CAP) return 0;
    i = a + 1;
    if (!(d->t[a].count == 1 && d->t[i].type == 'f'))
        for (int p = 0; p < d->t[a].count; p++) {
            if (d->t[i].type != 'o') return 0;
            OnPea *pea = &s.peas[s.pea_count++];
            if (!int_field(d, i, "row", &pea->row) ||
                !float_field(d, i, "x", &pea->x) ||
                !float_field(d, i, "y", &pea->y)) return 0;
            i = d->t[i].after;
        }
    a = field(d, node, "coins");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count > ON_COIN_CAP) return 0;
    i = a + 1;
    if (!(d->t[a].count == 1 && d->t[i].type == 'f'))
        for (int p = 0; p < d->t[a].count; p++) {
            if (d->t[i].type != 'o') return 0;
            OnCoin *coin = &s.coins[s.coin_count++];
            if (!int_field(d, i, "id", &coin->id) ||
                !float_field(d, i, "x", &coin->x) ||
                !float_field(d, i, "y", &coin->y) ||
                !float_field(d, i, "life", &coin->life)) return 0;
            i = d->t[i].after;
        }
    a = field(d, node, "mowers");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count != ON_ROWS) return 0;
    i = a + 1;
    for (int p = 0; p < ON_ROWS; p++) {
        if (d->t[i].type != 'o' ||
            !bool_field(d, i, "used", &s.mowers[p].used) ||
            !bool_field(d, i, "running", &s.mowers[p].running) ||
            !float_field(d, i, "x", &s.mowers[p].x)) return 0;
        i = d->t[i].after;
    }
    a = field(d, node, "plantCooldown");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count != ON_PLANT_TYPES) return 0;
    i = a + 1;
    for (int p = 0; p < ON_PLANT_TYPES; p++) {
        double n;
        if (!num(d, i, &n) || n < -1e9 || n > 1e9) return 0;
        s.plant_cooldown[p] = (float)n;
        i = d->t[i].after;
    }
    a = field(d, node, "duckCooldown");
    if (a < 0 || d->t[a].type != 'a' || d->t[a].count != 3) return 0;
    i = a + 1;
    for (int p = 0; p < 3; p++) {
        double n;
        if (!num(d, i, &n) || n < -1e9 || n > 1e9) return 0;
        s.duck_cooldown[p] = (float)n;
        i = d->t[i].after;
    }
    if (!on_match_valid(&s)) return 0;
    *out = s;
    return 1;
}
int on_protocol_match(const char *json, OnMatch *out) {
    JD d;
    if (!doc_open(&d, json)) return 0;
    int ok = match(&d, 0, out);
    free(d.t);
    return ok;
}
static int parse_ping(const JD *d, int obj, const char *name, int64_t *out) {
    double n;
    if (!num(d, field(d, obj, name), &n) || n < 0 ||
        n > 9000000000000000.0 || floor(n) != n) return 0;
    *out = (int64_t)n;return 1;
}
static int parse_room(const JD *d, int node, OnRoomData *out) {
    if (d->t[node].type == 'z') {memset(out, 0, sizeof(*out));return 1;}
    if (d->t[node].type != 'o') return 0;
    OnRoomData r;
    memset(&r, 0, sizeof(r));
    r.present = 1;
    int host = field(d, node, "host");
    if (!int_field(d, node, "version", &r.version) || r.version != 1 ||
        !int_field(d, node, "map", &r.map) || (r.map != 1 && r.map != 5) ||
        host < 0 || d->t[host].type != 'o' ||
        !str(d, field(d, host, "id"), r.host_id, sizeof(r.host_id)) ||
        !on_protocol_valid_player_id(r.host_id) ||
        !parse_ping(d, host, "ping", &r.host_ping) ||
        !role(d, field(d, host, "role"), &r.host_role)) return 0;
    int guest = field(d, node, "guest");
    if (guest >= 0 && d->t[guest].type != 'z') {
        if (d->t[guest].type != 'o' ||
            !str(d, field(d, guest, "id"), r.guest_id, sizeof(r.guest_id)) ||
            !on_protocol_valid_player_id(r.guest_id) ||
            !parse_ping(d, guest, "ping", &r.guest_ping) ||
            !role(d, field(d, guest, "role"), &r.guest_role)) return 0;
    }
    int state = field(d, node, "state");
    if (state >= 0 && d->t[state].type != 'z') {
        if (!match(d, state, &r.state) || r.state.map != r.map) return 0;
        r.has_state = 1;
    }
    int command = field(d, node, "command");
    if (command >= 0 && d->t[command].type != 'z') {
        if (d->t[command].type != 'o' ||
            !str(d, field(d, command, "id"), r.command.player_id,
                 sizeof(r.command.player_id)) ||
            !on_protocol_valid_player_id(r.command.player_id) ||
            !int_field(d, command, "seq", &r.command.seq) || r.command.seq < 1)
            return 0;
        int kind = field(d, command, "kind");
        if (eq(d, kind, "plant")) {
            r.command.kind = ON_CMD_PLANT;
            if (!int_field(d, command, "row", &r.command.row) ||
                !int_field(d, command, "col", &r.command.col) ||
                !int_field(d, command, "type", &r.command.type)) return 0;
        } else if (eq(d, kind, "coin")) {
            r.command.kind = ON_CMD_COIN;
            /* Older browser clients overwrote the coin's `id` with the
             * player's ID. Treat their command as rejected, but still ACK it
             * rather than invalidating the entire room. */
            if (!int_field(d, command, "coinId", &r.command.id)) r.command.id = -1;
        } else if (eq(d, kind, "spawn")) {
            r.command.kind = ON_CMD_SPAWN;
            if (!int_field(d, command, "row", &r.command.row) ||
                !int_field(d, command, "type", &r.command.type)) return 0;
        } else if (eq(d, kind, "finish")) r.command.kind = ON_CMD_FINISH;
        else return 0;
        r.has_command = 1;
    }
    *out = r;return 1;
}
int on_protocol_room(const char *json, OnRoomData *out) {
    if (!out) return 0;
    JD d;
    if (!doc_open(&d, json)) return 0;
    int ok = parse_room(&d, 0, out);
    free(d.t);
    return ok;
}
int on_protocol_rooms(const char *json, OnRoomSummary *out, int cap, int64_t now_ms) {
    if (!out || cap <= 0 || cap > ON_ROOM_LIST_CAP) return -1;
    JD d;
    if (!doc_open(&d, json)) return -1;
    int count = 0;
    if (d.t[0].type == 'z') {free(d.t);return 0;}
    if (d.t[0].type != 'o') {free(d.t);return -1;}
    for (int key = 1; key < d.t[0].after;) {
        int val = key + 1;
        char id[ON_ROOM_ID_SIZE];
        if (str(&d, key, id, sizeof(id)) && on_protocol_valid_room_id(id) &&
            d.t[val].type == 'o') {
            int host = field(&d, val, "host"), version, map;
            int64_t ping;
            char host_id[ON_PLAYER_ID_SIZE];
            int guest = field(&d, val, "guest"), state = field(&d, val, "state");
            if (int_field(&d, val, "version", &version) && version == 1 &&
                int_field(&d, val, "map", &map) && (map == 1 || map == 5) &&
                host >= 0 && d.t[host].type == 'o' &&
                str(&d, field(&d, host, "id"), host_id, sizeof(host_id)) &&
                on_protocol_valid_player_id(host_id) &&
                parse_ping(&d, host, "ping", &ping) &&
                ping >= now_ms - 600000 && ping <= now_ms + 600000 &&
                (guest < 0 || d.t[guest].type == 'z') &&
                (state < 0 || d.t[state].type == 'z')) {
                int where = count;
                while (where > 0 && ping > out[where - 1].ping) where--;
                if (where < cap) {
                    if (count < cap) count++;
                    memmove(&out[where + 1], &out[where],
                            (size_t)(count - where - 1) * sizeof(*out));
                    memset(&out[where], 0, sizeof(*out));
                    memcpy(out[where].id, id, sizeof(id));
                    out[where].map = map;
                    out[where].ping = ping;
                }
            }
        }
        key = d.t[val].after;
    }
    free(d.t);
    return count;
}

typedef struct { char *out; size_t cap, at; int bad; } JW;
static void put(JW *w, const char *fmt, ...) {
    if (w->bad) return;
    if (w->at >= w->cap) {w->bad = 1;return;}
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(w->out + w->at, w->cap - w->at, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->cap - w->at) {w->bad = 1;return;}
    w->at += (size_t)n;
}
size_t on_protocol_match_json(const OnMatch *s, char *out, size_t cap) {
    if (!out || !cap || !on_match_valid(s)) return 0;
    JW w = {out, cap, 0, 0};
    put(&w, "{\"version\":1,\"map\":%d,\"time\":%.5f,\"plants\":[", s->map, (double)s->time);
    for (int i = 0; i < ON_CELLS; i++) {
        if (i) put(&w, ",");
        const OnPlant *p = &s->plants[i];
        if (p->type < 0) put(&w, "false");
        else put(&w, "{\"type\":%d,\"hp\":%.5f,\"fire\":%.5f}",
                 p->type, (double)p->hp, (double)p->fire);
    }
    put(&w, "],\"lilies\":[");
    for (int i = 0; i < ON_CELLS; i++) put(&w, "%s%s", i ? "," : "", s->lilies[i] ? "true" : "false");
    put(&w, "],\"ducks\":[");
    if (!s->duck_count) put(&w, "false");
    for (int i = 0; i < s->duck_count; i++) {
        const OnDuck *d = &s->ducks[i];
        put(&w, "%s{\"id\":%d,\"type\":%d,\"row\":%d,\"x\":%.5f,"
                "\"hp\":%.5f,\"maxHp\":%.5f,\"speed\":%.5f,\"anim\":%.5f}",
            i ? "," : "", d->id, d->type, d->row, (double)d->x, (double)d->hp,
            (double)d->max_hp, (double)d->speed, (double)d->anim);
    }
    put(&w, "],\"peas\":[");
    if (!s->pea_count) put(&w, "false");
    for (int i = 0; i < s->pea_count; i++) {
        OnPea p = s->peas[i];
        put(&w, "%s{\"row\":%d,\"x\":%.5f,\"y\":%.5f}",
            i ? "," : "", p.row, (double)p.x, (double)p.y);
    }
    put(&w, "],\"coins\":[");
    if (!s->coin_count) put(&w, "false");
    for (int i = 0; i < s->coin_count; i++) {
        OnCoin c = s->coins[i];
        put(&w, "%s{\"id\":%d,\"x\":%.5f,\"y\":%.5f,\"life\":%.5f}",
            i ? "," : "", c.id, (double)c.x, (double)c.y, (double)c.life);
    }
    put(&w, "],\"mowers\":[");
    for (int i = 0; i < ON_ROWS; i++) {
        const OnMower *m = &s->mowers[i];
        put(&w, "%s{\"used\":%s,\"running\":%s,\"x\":%.5f}",
            i ? "," : "", m->used ? "true" : "false", m->running ? "true" : "false",
            (double)m->x);
    }
    put(&w, "],\"plantCash\":%d,\"zombieCash\":%d,\"plantCooldown\":[",
        s->plant_cash, s->zombie_cash);
    for (int i = 0; i < ON_PLANT_TYPES; i++)
        put(&w, "%s%.5f", i ? "," : "", (double)s->plant_cooldown[i]);
    put(&w, "],\"duckCooldown\":[");
    for (int i = 0; i < 3; i++) put(&w, "%s%.5f", i ? "," : "", (double)s->duck_cooldown[i]);
    put(&w, "],\"zombieIncome\":%.5f,\"left\":%d,\"winner\":\"%s\","
        "\"nextId\":%d,\"ackGuest\":%d}",
        (double)s->zombie_income, s->left, s->winner == ON_WIN_PLANTS ? "plants" :
        s->winner == ON_WIN_ZOMBIES ? "zombies" : "", s->next_id, s->ack_guest);
    if (w.bad) {out[0] = 0;return 0;}
    return w.at;
}
size_t on_protocol_command_json(const OnCommand *c, const char *player_id,
                                char *out, size_t cap) {
    if (!c || !on_protocol_valid_player_id(player_id) || !out || cap < 96 ||
        c->seq <= 0) return 0;
    JW w = {out, cap, 0, 0};
    const char *kind = c->kind == ON_CMD_PLANT ? "plant" :
                       c->kind == ON_CMD_COIN ? "coin" :
                       c->kind == ON_CMD_SPAWN ? "spawn" :
                       c->kind == ON_CMD_FINISH ? "finish" : NULL;
    if (!kind) return 0;
    put(&w, "{\"kind\":\"%s\",\"seq\":%d,\"id\":\"%s\"",
        kind, c->seq, player_id);
    if (c->kind == ON_CMD_PLANT)
        put(&w, ",\"row\":%d,\"col\":%d,\"type\":%d",c->row,c->col,c->type);
    else if (c->kind == ON_CMD_COIN) put(&w, ",\"coinId\":%d", c->id);
    else if (c->kind == ON_CMD_SPAWN) put(&w, ",\"row\":%d,\"type\":%d",c->row,c->type);
    put(&w, "}");
    if (w.bad) {out[0] = 0;return 0;}
    return w.at;
}
