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

/* A 20,000-object level can require several megabytes of JSON and roughly
 * 1.1 million parser tokens. Grow the bounded token table as needed so
 * ordinary room traffic keeps its small memory footprint. */
#define JSON_MAX_BYTES (ON_LEVEL_JSON_CAP - 1u)
#define JSON_INITIAL_TOKENS 256
#define JSON_MAX_TOKENS 1300000
#define JSON_MAX_DEPTH 32

typedef struct { char type; int start, end, after, count; } JT;
typedef struct { const char *s; size_t n, at; JT *t; int used, cap, bad; } JD;

static void ws(JD *d) {
    while (d->at < d->n && (d->s[d->at] == ' ' || d->s[d->at] == '\t' ||
                             d->s[d->at] == '\n' || d->s[d->at] == '\r')) d->at++;
}
static int token(JD *d, char type, int start) {
    if (d->used >= d->cap) {
        if (d->cap >= JSON_MAX_TOKENS) { d->bad = 1; return -1; }
        int next = d->cap > JSON_MAX_TOKENS / 2 ? JSON_MAX_TOKENS : d->cap * 2;
        JT *grown = (JT *)realloc(d->t, (size_t)next * sizeof *grown);
        if (!grown) { d->bad = 1; return -1; }
        d->t = grown;d->cap = next;
    }
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
    d->t = (JT *)calloc(JSON_INITIAL_TOKENS, sizeof(JT));
    if (!d->t) return 0;
    d->s = s;d->n = n;d->cap = JSON_INITIAL_TOKENS;
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
static int append_utf8(char *out, size_t cap, size_t *used, unsigned cp) {
    unsigned char bytes[4];size_t n;
    if (cp <= 0x7f) {bytes[0] = (unsigned char)cp;n = 1;}
    else if (cp <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3f));n = 2;
    } else if (cp <= 0xffff && (cp < 0xd800 || cp > 0xdfff)) {
        bytes[0] = (unsigned char)(0xe0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3f));n = 3;
    } else if (cp <= 0x10ffff) {
        bytes[0] = (unsigned char)(0xf0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3f));n = 4;
    } else return 0;
    if (*used + n >= cap) return 0;
    memcpy(out + *used, bytes, n);*used += n;return 1;
}
static int hex4(const char *s, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isxdigit(c)) return 0;
        v = (v << 4) | (unsigned)(isdigit(c) ? c - '0' :
                                  tolower(c) - 'a' + 10);
    }
    *out = v;return 1;
}
static int text_string(const JD *d, int t, char *out, size_t cap) {
    if (t < 0 || t >= d->used || d->t[t].type != 's' || cap < 1) return 0;
    size_t used = 0;
    for (int i = d->t[t].start; i < d->t[t].end;) {
        unsigned char c = (unsigned char)d->s[i++];
        if (c != '\\') {
            if (used + 1 >= cap) return 0;
            out[used++] = (char)c; /* Preserve raw UTF-8 bytes from JSON. */
            continue;
        }
        if (i >= d->t[t].end) return 0;
        char esc = d->s[i++];unsigned cp;
        switch (esc) {
        case '"': cp = '"';break; case '\\': cp = '\\';break;
        case '/': cp = '/';break; case 'b': cp = '\b';break;
        case 'f': cp = '\f';break; case 'n': cp = '\n';break;
        case 'r': cp = '\r';break; case 't': cp = '\t';break;
        case 'u':
            if (i + 4 > d->t[t].end || !hex4(d->s + i, &cp)) return 0;
            i += 4;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                unsigned low;
                if (i + 6 > d->t[t].end || d->s[i] != '\\' ||
                    d->s[i + 1] != 'u' || !hex4(d->s + i + 2, &low) ||
                    low < 0xdc00 || low > 0xdfff) return 0;
                i += 6;cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            } else if (cp >= 0xdc00 && cp <= 0xdfff) return 0;
            break;
        default: return 0;
        }
        if (!append_utf8(out, cap, &used, cp)) return 0;
    }
    out[used] = 0;return 1;
}
static int color_value(const JD *d, int token, uint32_t *out) {
    char value[8];
    if (!text_string(d, token, value, sizeof value) || strlen(value) != 7 || value[0] != '#') return 0;
    unsigned long rgb = strtoul(value + 1, NULL, 16);
    if (rgb > 0xffffff) return 0;
    *out = (uint32_t)rgb;return 1;
}
static int level_object_type(const JD *d, int token) {
    static const char *const names[] = {
        "block", "ground", "hazard", "coin", "enemy", "player", "goal", "trigger",
        "slope", "orb-yellow", "orb-orange", "particle", "checkpoint",
        "portal-normal", "portal-jetpack"
    };
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (eq(d, token, names[i])) return i;
    return -1;
}
static int level_event(const JD *d, int token) {
    if (eq(d, token, "touch")) return ON_TRIGGER_TOUCH;
    if (eq(d, token, "coin")) return ON_TRIGGER_COIN;
    if (eq(d, token, "manual")) return ON_TRIGGER_MANUAL;
    if (eq(d, token, "start")) return ON_TRIGGER_START;
    return -1;
}
static int level_action(const JD *d, int token) {
    if (eq(d, token, "toggle")) return ON_TRIGGER_TOGGLE;
    if (eq(d, token, "move")) return ON_TRIGGER_MOVE;
    if (eq(d, token, "recolor")) return ON_TRIGGER_RECOLOR;
    if (eq(d, token, "number")) return ON_TRIGGER_NUMBER;
    if (eq(d, token, "rotate")) return ON_TRIGGER_ROTATE;
    if (eq(d, token, "activate")) return ON_TRIGGER_ACTIVATE;
    if (eq(d, token, "unactivate")) return ON_TRIGGER_UNACTIVATE;
    if (eq(d, token, "invisible")) return ON_TRIGGER_INVISIBLE;
    if (eq(d, token, "no-collision")) return ON_TRIGGER_NO_COLLISION;
    if (eq(d, token, "set-gravity")) return ON_TRIGGER_SET_GRAVITY;
    return -1;
}
static int level_trigger_kind(const JD *d, int token) {
    if (eq(d, token, "move")) return ON_TRIGGER_KIND_MOVE;
    if (eq(d, token, "rotate")) return ON_TRIGGER_KIND_ROTATE;
    if (eq(d, token, "forever")) return ON_TRIGGER_KIND_FOREVER;
    if (eq(d, token, "invisibility")) return ON_TRIGGER_KIND_INVISIBILITY;
    if (eq(d, token, "no-collision")) return ON_TRIGGER_KIND_NO_COLLISION;
    if (eq(d, token, "gravity")) return ON_TRIGGER_KIND_GRAVITY;
    return -1;
}
static int parse_particle_emitter(const JD *d, int object,
                                 OnLevelParticle *emitter) {
    if (!d || !emitter) return 0;
    *emitter = on_level_particle_default();
    int config = field(d, object, "emitter");
    if (config < 0) return 1; /* older published records use these defaults */
    if (d->t[config].type != 'o') return 0;
    int token = field(d, config, "enabled");
    if (token >= 0 && !bool_field(d, config, "enabled", &emitter->enabled)) return 0;
    token = field(d, config, "continuous");
    if (token >= 0 && !bool_field(d, config, "continuous", &emitter->continuous)) return 0;
    token = field(d, config, "gravityEnabled");
    if (token >= 0 && !bool_field(d, config, "gravityEnabled", &emitter->gravity_enabled)) return 0;
    token = field(d, config, "glow");
    if (token >= 0 && !bool_field(d, config, "glow", &emitter->glow)) return 0;
    token = field(d, config, "rate");
    if (token >= 0 && !int_field(d, config, "rate", &emitter->rate)) return 0;
    token = field(d, config, "lifetime");
    if (token >= 0 && !float_field(d, config, "lifetime", &emitter->lifetime)) return 0;
    token = field(d, config, "speed");
    if (token >= 0 && !int_field(d, config, "speed", &emitter->speed)) return 0;
    token = field(d, config, "spread");
    if (token >= 0 && !int_field(d, config, "spread", &emitter->spread)) return 0;
    token = field(d, config, "size");
    if (token >= 0 && !int_field(d, config, "size", &emitter->size)) return 0;
    token = field(d, config, "direction");
    if (token >= 0 && !int_field(d, config, "direction", &emitter->direction)) return 0;
    token = field(d, config, "gravity");
    if (token >= 0 && !int_field(d, config, "gravity", &emitter->gravity)) return 0;
    return on_level_particle_valid(emitter);
}
int on_protocol_valid_level_id(const char *id) {
    if (!id || !*id || strlen(id) >= ON_LEVEL_ID_SIZE) return 0;
    if (id[0] == '0' && id[1]) return 0;
    for (const unsigned char *p = (const unsigned char *)id; *p; p++)
        if (!isdigit(*p)) return 0;
    char *end = NULL;long value = strtol(id, &end, 10);
    return end && !*end && value > 0 && value <= 999999;
}

#define LEVEL_OBJECT_ID_MAX 1000000
#define LEVEL_OBJECT_ID_BITMAP_BYTES ((LEVEL_OBJECT_ID_MAX + 7u) / 8u)
static int mark_level_object_id(uint8_t *seen, int id) {
    if (!seen || id < 1 || id > LEVEL_OBJECT_ID_MAX) return 0;
    size_t bit = (size_t)(id - 1);
    uint8_t mask = (uint8_t)(1u << (bit & 7u));
    uint8_t *byte = &seen[bit >> 3];
    if (*byte & mask) return 0;
    *byte |= mask;
    return 1;
}
int on_protocol_level_index(const char *json, OnPublishedLevelSummary *out, int cap) {
    if (!json || cap < 0 || (cap && !out)) return -1;
    JD d;if (!doc_open(&d, json)) return -1;
    if (d.t[0].type == 'z') {free(d.t);return 0;}
    if (d.t[0].type != 'o') {free(d.t);return -1;}
    OnPublishedLevelSummary tmp[ON_LEVEL_LIST_CAP];int count = 0;
    for (int key = 1; key < d.t[0].after;) {
        int value = key + 1;
        char id[ON_LEVEL_ID_SIZE], title[ON_LEVEL_TITLE_SIZE];
        char description[ON_LEVEL_DESCRIPTION_SIZE] = {0};
        if (str(&d, key, id, sizeof id) && on_protocol_valid_level_id(id) &&
            d.t[value].type == 'o') {
            int id_token = field(&d, value, "id");
            int title_token = field(&d, value, "title");
            int description_token = field(&d, value, "description");
            char stored_id[ON_LEVEL_ID_SIZE];
            int description_ok = description_token < 0 ||
                text_string(&d, description_token, description, sizeof description);
            if (str(&d, id_token, stored_id, sizeof stored_id) && !strcmp(id, stored_id) &&
                text_string(&d, title_token, title, sizeof title) && title[0] &&
                description_ok && count < cap && count < ON_LEVEL_LIST_CAP) {
                snprintf(tmp[count].id, sizeof tmp[count].id, "%s", id);
                snprintf(tmp[count].title, sizeof tmp[count].title, "%s", title);
                snprintf(tmp[count].description, sizeof tmp[count].description, "%s", description);
                count++;
            }
        }
        key = d.t[value].after;
    }
    if (count) memcpy(out, tmp, (size_t)count * sizeof tmp[0]);
    free(d.t);return count;
}
static int parse_level_object(const JD *d, int node, OnLevelObject *out) {
    if (node < 0 || d->t[node].type != 'o' || !out) return 0;
    OnLevelObject item = {0};
    int type = level_object_type(d, field(d, node, "type"));
    int flip_x_token = field(d, node, "flipX");
    int flip_y_token = field(d, node, "flipY");
    if ((flip_x_token >= 0 && !bool_field(d, node, "flipX", &item.flip_x)) ||
        (flip_y_token >= 0 && !bool_field(d, node, "flipY", &item.flip_y))) return 0;
    if (type < 0 || !int_field(d, node, "id", &item.id) || item.id < 1 || item.id > 1000000 ||
        !float_field(d, node, "x", &item.x) || !float_field(d, node, "y", &item.y) ||
        !float_field(d, node, "w", &item.w) || !float_field(d, node, "h", &item.h) ||
        !float_field(d, node, "angle", &item.angle) ||
        !color_value(d, field(d, node, "color"), &item.color) ||
        !int_field(d, node, "number", &item.number) || item.number < 0 || item.number > 9999 ||
        !bool_field(d, node, "visible", &item.visible) ||
        item.x < -ON_LEVEL_WORLD_LIMIT || item.y < -ON_LEVEL_WORLD_LIMIT ||
        item.w <= 0 || item.h <= 0 ||
        item.x + item.w > ON_LEVEL_WORLD_LIMIT ||
        item.y + item.h > ON_LEVEL_WORLD_LIMIT ||
        item.w > 64 || item.h > 40 || item.angle < 0 || item.angle >= 360) return 0;
    item.type = type;
    int name = field(d, node, "name");
    if (!text_string(d, name, item.name, sizeof item.name)) return 0;
    if (type == ON_LEVEL_PARTICLE &&
        !parse_particle_emitter(d, node, &item.emitter)) return 0;
    if (type == ON_LEVEL_TRIGGER) {
        int trigger = field(d, node, "trigger");
        if (trigger < 0 || d->t[trigger].type != 'o') return 0;
        int kind_token = field(d, trigger, "kind");
        item.trigger_kind = kind_token < 0 ? ON_TRIGGER_KIND_MOVE :
                            level_trigger_kind(d, kind_token);
        item.trigger_event = level_event(d, field(d, trigger, "event"));
        item.trigger_action = level_action(d, field(d, trigger, "action"));
        int target_token = field(d, trigger, "targetId");
        int group_token = field(d, trigger, "groupId");
        int value_token = field(d, trigger, "value");
        int value_x_token = field(d, trigger, "valueX");
        int value_y_token = field(d, trigger, "valueY");
        int degrees_token = field(d, trigger, "degrees");
        int duration_token = field(d, trigger, "duration");
        if (item.trigger_kind == ON_TRIGGER_KIND_ROTATE && duration_token >= 0)
            value_token = -1;
        else if (item.trigger_kind == ON_TRIGGER_KIND_ROTATE && degrees_token >= 0)
            value_token = degrees_token;
        else if (item.trigger_kind == ON_TRIGGER_KIND_MOVE && value_x_token >= 0)
            value_token = value_x_token;
        item.target_id = 0;
        item.trigger_value = item.trigger_value_y = 0;
        item.trigger_group_id = 0;item.trigger_has_group = 0;
        item.trigger_duration = 0;item.trigger_has_duration = 0;
        item.trigger_color = 0xffc54eu;
        if (value_token >= 0) {
            double parsed_value;
            if (!num(d, value_token, &parsed_value)) return 0;
            item.trigger_value = (float)parsed_value;
        }
        if (item.trigger_kind == ON_TRIGGER_KIND_ROTATE && duration_token >= 0) {
            if (!int_field(d, trigger, "duration", &item.trigger_duration)) return 0;
            item.trigger_has_duration = 1;
        }
        if (item.trigger_kind < ON_TRIGGER_KIND_MOVE ||
            item.trigger_kind > ON_TRIGGER_KIND_GRAVITY ||
            item.trigger_event < 0 || item.trigger_action < 0 ||
            (target_token >= 0 && !int_field(d, trigger, "targetId", &item.target_id)) ||
            (value_y_token >= 0 && !float_field(d, trigger, "valueY", &item.trigger_value_y)) ||
            !isfinite(item.trigger_value) || !isfinite(item.trigger_value_y) ||
            (group_token >= 0 && (!int_field(d, trigger, "groupId", &item.trigger_group_id) ||
                                  item.trigger_group_id < 0 || item.trigger_group_id > 9999)) ||
            item.target_id < 0 || item.target_id > 1000000 ||
            (field(d, trigger, "color") >= 0 &&
             !color_value(d, field(d, trigger, "color"), &item.trigger_color))) return 0;
        item.trigger_has_group = group_token >= 0;
        if (item.trigger_kind == ON_TRIGGER_KIND_MOVE) {
            if (item.trigger_action > ON_TRIGGER_ROTATE ||
                item.trigger_value < -9999 || item.trigger_value > 9999 ||
                item.trigger_value_y < -9999 || item.trigger_value_y > 9999 ||
                (item.trigger_has_group && item.trigger_group_id > 9999)) return 0;
        } else if (item.trigger_kind == ON_TRIGGER_KIND_ROTATE) {
            if (item.trigger_has_duration) {
                if (item.trigger_action != ON_TRIGGER_ROTATE || !item.trigger_has_group ||
                    item.trigger_duration < 1 || item.trigger_duration > 9999) return 0;
            } else if (item.trigger_action > ON_TRIGGER_ROTATE ||
                       item.trigger_value < -360 || item.trigger_value > 360) return 0;
        } else if (item.trigger_kind == ON_TRIGGER_KIND_FOREVER) {
            if (item.trigger_has_group) {
                if (item.trigger_action != ON_TRIGGER_ACTIVATE &&
                    item.trigger_action != ON_TRIGGER_UNACTIVATE) return 0;
            } else if (item.trigger_action > ON_TRIGGER_ROTATE ||
                       item.trigger_value < -100 || item.trigger_value > 100) return 0;
        } else if (item.trigger_kind == ON_TRIGGER_KIND_GRAVITY) {
            if (item.trigger_action != ON_TRIGGER_SET_GRAVITY || item.trigger_has_group ||
                item.target_id != 0 || item.trigger_has_duration || value_token < 0 ||
                item.trigger_value < -100 || item.trigger_value > 100 ||
                floorf(item.trigger_value) != item.trigger_value || item.trigger_value_y != 0)
                return 0;
        } else if (!item.trigger_has_group && item.target_id <= 0) {
            return 0;
        } else if ((item.trigger_kind == ON_TRIGGER_KIND_INVISIBILITY &&
                    item.trigger_action != ON_TRIGGER_INVISIBLE) ||
                   (item.trigger_kind == ON_TRIGGER_KIND_NO_COLLISION &&
                    item.trigger_action != ON_TRIGGER_NO_COLLISION)) {
            return 0;
        }
    }
    *out = item;return 1;
}
int on_protocol_published_level(const char *json, const char *expected_id,
                                OnPublishedLevel *out) {
    if (!json || !out || !on_protocol_valid_level_id(expected_id)) return 0;
    JD d;if (!doc_open(&d, json)) return 0;
    OnPublishedLevel *value = (OnPublishedLevel *)calloc(1, sizeof(*value));
    if (!value) {free(d.t);return 0;}
    int project = field(&d, 0, "project"), version = 0;
    int ok = eq(&d, field(&d, 0, "format"), "PVG3-PUBLISHED-LEVEL") &&
             int_field(&d, 0, "version", &version) && version == 1 &&
             str(&d, field(&d, 0, "id"), value->id, sizeof value->id) &&
             !strcmp(value->id, expected_id) &&
             text_string(&d, field(&d, 0, "title"), value->title, sizeof value->title) &&
             value->title[0] && project >= 0 && d.t[project].type == 'o' &&
             eq(&d, field(&d, project, "format"), "PVG3-MAKER") &&
             int_field(&d, project, "version", &version) && version == 1 &&
             int_field(&d, project, "width", &value->width) && value->width == 16 &&
             int_field(&d, project, "height", &value->height) && value->height == 10;
    int description_token = field(&d, 0, "description");
    if (ok && description_token >= 0 &&
        !text_string(&d, description_token, value->description, sizeof value->description)) ok = 0;
    if (ok && description_token < 0) {
        description_token = field(&d, project, "description");
        if (description_token >= 0 &&
            !text_string(&d, description_token, value->description, sizeof value->description)) ok = 0;
    }
    int objects = field(&d, project, "objects");
    if (ok && (objects < 0 || d.t[objects].type != 'a' ||
               d.t[objects].count < 1 || d.t[objects].count > ON_LEVEL_OBJECT_CAP)) ok = 0;
    uint8_t *seen = ok ? (uint8_t *)calloc(LEVEL_OBJECT_ID_BITMAP_BYTES, 1) : NULL;
    if (ok && !seen) ok = 0;
    int has_player = 0, has_goal = 0;
    if (ok) {
        for (int token = objects + 1; token < d.t[objects].after; token = d.t[token].after) {
            OnLevelObject *item = &value->objects[value->object_count];
            if (!parse_level_object(&d, token, item) ||
                !mark_level_object_id(seen, item->id)) {ok = 0;break;}
            has_player |= item->type == ON_LEVEL_PLAYER;
            has_goal |= item->type == ON_LEVEL_GOAL;
            value->object_count++;
        }
    }
    if (ok && (!has_player || !has_goal)) ok = 0;
    if (ok) *out = *value;
    free(seen);free(value);free(d.t);return ok;
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
    if (w->bad || w->at >= w->cap) {w->bad = 1;return;}
    va_list args;
    va_start(args, fmt);
    int n = w->out ?
        vsnprintf(w->out + w->at, w->cap - w->at, fmt, args) :
        vsnprintf(NULL, 0, fmt, args);
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

static void put_json_string(JW *w, const char *text) {
    put(w, "\"");
    for (const unsigned char *p = (const unsigned char *)text; *p && !w->bad; ++p) {
        switch (*p) {
        case '"': put(w, "\\\"");break;
        case '\\': put(w, "\\\\");break;
        case '\b': put(w, "\\b");break;
        case '\f': put(w, "\\f");break;
        case '\n': put(w, "\\n");break;
        case '\r': put(w, "\\r");break;
        case '\t': put(w, "\\t");break;
        default:
            if (*p < 0x20) put(w, "\\u%04x", (unsigned)*p);
            else put(w, "%c", (int)*p);
        }
    }
    put(w, "\"");
}

static int utf8_units(const char *text, size_t cap, size_t limit) {
    const unsigned char *p = (const unsigned char *)text;
    const unsigned char *end = p + cap;
    size_t units = 0;
    while (p < end && *p) {
        unsigned cp;size_t width;
        if (*p < 0x80) {cp = *p;width = 1;}
        else if ((*p & 0xe0) == 0xc0) {cp = *p & 0x1f;width = 2;}
        else if ((*p & 0xf0) == 0xe0) {cp = *p & 0x0f;width = 3;}
        else if ((*p & 0xf8) == 0xf0) {cp = *p & 0x07;width = 4;}
        else return 0;
        if ((size_t)(end - p) < width) return 0;
        for (size_t i = 1; i < width; ++i) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (p[i] & 0x3f);
        }
        if ((width == 2 && cp < 0x80) || (width == 3 && cp < 0x800) ||
            (width == 4 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return 0;
        units += cp > 0xffff ? 2 : 1;
        if (units > limit) return 0;
        p += width;
    }
    return p < end && *p == 0;
}
static int published_trigger_valid(const OnLevelObject *o) {
    if (!o || o->trigger_kind < ON_TRIGGER_KIND_MOVE ||
        o->trigger_kind > ON_TRIGGER_KIND_GRAVITY ||
        o->trigger_event < ON_TRIGGER_TOUCH || o->trigger_event > ON_TRIGGER_START ||
        o->trigger_action < ON_TRIGGER_TOGGLE ||
        o->trigger_action > ON_TRIGGER_SET_GRAVITY ||
        o->target_id < 0 || o->target_id > 1000000 ||
        (o->trigger_has_group != 0 && o->trigger_has_group != 1) ||
        (o->trigger_has_group &&
         (o->trigger_group_id < 0 || o->trigger_group_id > 9999)) ||
        (o->trigger_has_duration != 0 && o->trigger_has_duration != 1) ||
        (o->trigger_has_duration && o->trigger_kind != ON_TRIGGER_KIND_ROTATE) ||
        !isfinite(o->trigger_value) || !isfinite(o->trigger_value_y) ||
        o->trigger_color > 0xffffffu) return 0;
    if (o->trigger_kind == ON_TRIGGER_KIND_MOVE) {
        return o->trigger_action <= ON_TRIGGER_ROTATE &&
            o->trigger_value >= -9999 && o->trigger_value <= 9999 &&
            o->trigger_value_y >= -9999 && o->trigger_value_y <= 9999;
    }
    if (o->trigger_kind == ON_TRIGGER_KIND_ROTATE) {
        if (o->trigger_has_duration)
            return o->trigger_has_group && o->trigger_action == ON_TRIGGER_ROTATE &&
                o->trigger_duration >= 1 && o->trigger_duration <= 9999;
        return o->trigger_action <= ON_TRIGGER_ROTATE &&
            o->trigger_value >= -360 && o->trigger_value <= 360;
    }
    if (o->trigger_kind == ON_TRIGGER_KIND_FOREVER) {
        if (o->trigger_has_group)
            return o->trigger_action == ON_TRIGGER_ACTIVATE ||
                   o->trigger_action == ON_TRIGGER_UNACTIVATE;
        return o->trigger_action <= ON_TRIGGER_ROTATE &&
            o->trigger_value >= -100 && o->trigger_value <= 100;
    }
    if (o->trigger_kind == ON_TRIGGER_KIND_GRAVITY)
        return o->trigger_action == ON_TRIGGER_SET_GRAVITY &&
            !o->trigger_has_group && o->target_id == 0 &&
            !o->trigger_has_duration && o->trigger_value >= -100 &&
            o->trigger_value <= 100 && floorf(o->trigger_value) == o->trigger_value &&
            o->trigger_value_y == 0;
    if (!o->trigger_has_group && o->target_id <= 0) return 0;
    return (o->trigger_kind == ON_TRIGGER_KIND_INVISIBILITY &&
            o->trigger_action == ON_TRIGGER_INVISIBLE) ||
           (o->trigger_kind == ON_TRIGGER_KIND_NO_COLLISION &&
            o->trigger_action == ON_TRIGGER_NO_COLLISION);
}

static int published_level_valid(const OnPublishedLevel *level) {
    if (!level || !on_protocol_valid_level_id(level->id) ||
        !level->title[0] || !memchr(level->title, 0, sizeof level->title) ||
        !memchr(level->description, 0, sizeof level->description) ||
        !utf8_units(level->title, sizeof level->title, 80) ||
        !utf8_units(level->description, sizeof level->description, 160) ||
        !level->width || !level->height || level->width != 16 || level->height != 10 ||
        level->object_count < 1 || level->object_count > ON_LEVEL_OBJECT_CAP) return 0;
    uint8_t *seen = (uint8_t *)calloc(LEVEL_OBJECT_ID_BITMAP_BYTES, 1);
    if (!seen) return 0;
    int has_player = 0, has_goal = 0, valid = 1;
    for (int i = 0; i < level->object_count; ++i) {
        const OnLevelObject *o = &level->objects[i];
        if (!mark_level_object_id(seen, o->id) || o->type < ON_LEVEL_BLOCK ||
            o->type > ON_LEVEL_PORTAL_JETPACK || !memchr(o->name, 0, sizeof o->name) ||
            !utf8_units(o->name, sizeof o->name, 48) || !isfinite(o->x) || !isfinite(o->y) || !isfinite(o->w) ||
            !isfinite(o->h) || !isfinite(o->angle) ||
            o->x < -ON_LEVEL_WORLD_LIMIT || o->y < -ON_LEVEL_WORLD_LIMIT ||
            o->w <= 0 || o->h <= 0 ||
            o->x + o->w > ON_LEVEL_WORLD_LIMIT ||
            o->y + o->h > ON_LEVEL_WORLD_LIMIT || o->w > 64 || o->h > 40 ||
            o->angle < 0 || o->angle >= 360 || o->color > 0xffffffu ||
            (o->flip_x != 0 && o->flip_x != 1) ||
            (o->flip_y != 0 && o->flip_y != 1) ||
            o->number < 0 || o->number > 9999 || (o->visible != 0 && o->visible != 1)) {
            valid = 0;break;
        }
        has_player |= o->type == ON_LEVEL_PLAYER;
        has_goal |= o->type == ON_LEVEL_GOAL;
        if ((o->type == ON_LEVEL_TRIGGER && !published_trigger_valid(o)) ||
            (o->type == ON_LEVEL_PARTICLE &&
             !on_level_particle_valid(&o->emitter))) {
            valid = 0;break;
        }
    }
    free(seen);
    return valid && has_player && has_goal;
}

size_t on_protocol_published_level_json(const OnPublishedLevel *level,
                                        char *out, size_t cap) {
    int measure_only = !out && !cap;
    if ((!out && !measure_only) || (out && !cap) ||
        !published_level_valid(level)) return 0;
    static const char *const types[] = {
        "block", "ground", "hazard", "coin", "enemy", "player", "goal", "trigger",
        "slope", "orb-yellow", "orb-orange", "particle", "checkpoint",
        "portal-normal", "portal-jetpack"
    };
    static const char *const events[] = {"touch", "coin", "manual", "start"};
    static const char *const actions[] = {
        "toggle", "move", "recolor", "number", "rotate", "activate", "unactivate",
        "invisible", "no-collision", "set-gravity"
    };
    static const char *const trigger_kinds[] = {
        "move", "rotate", "forever", "invisibility", "no-collision", "gravity"
    };
    static const char *const names[] = {
        "Блок", "Платформа", "Шипы", "Монета", "Гусь", "Игрок", "Финиш", "Триггер",
        "Склон", "Жёлтый орб", "Оранжевый орб", "Эмиттер частиц", "Чекпоинт",
        "Обычный портал", "Портал Jetpack"
    };
    JW w = {out, measure_only ? SIZE_MAX : cap, 0, 0};
    put(&w, "{\"format\":\"PVG3-PUBLISHED-LEVEL\",\"version\":1,\"id\":");
    put_json_string(&w, level->id);
    put(&w, ",\"title\":");put_json_string(&w, level->title);
    put(&w, ",\"description\":");put_json_string(&w, level->description);
    put(&w, ",\"project\":{\"format\":\"PVG3-MAKER\",\"version\":1,"
        "\"width\":%d,\"height\":%d,\"objects\":[",
        level->width, level->height);
    for (int i = 0; i < level->object_count; ++i) {
        const OnLevelObject *o = &level->objects[i];
        put(&w, "%s{\"id\":%d,\"type\":\"%s\",\"name\":",
            i ? "," : "", o->id, types[o->type]);
        put_json_string(&w, o->name[0] ? o->name : names[o->type]);
        put(&w, ",\"x\":%.4f,\"y\":%.4f,\"w\":%.4f,\"h\":%.4f,"
            "\"angle\":%.3f,\"flipX\":%s,\"flipY\":%s,"
            "\"color\":\"#%06x\",\"number\":%d,"
            "\"visible\":%s,\"layer\":0,\"layer2\":0,\"zOrder\":0",
            (double)o->x, (double)o->y, (double)o->w, (double)o->h,
            (double)o->angle, o->flip_x ? "true" : "false",
            o->flip_y ? "true" : "false", (unsigned)o->color, o->number,
            o->visible ? "true" : "false");
        if (o->type == ON_LEVEL_TRIGGER) {
            put(&w, ",\"trigger\":{\"kind\":\"%s\",\"event\":\"%s\",\"action\":\"%s\",\"targetId\":%d",
                trigger_kinds[o->trigger_kind], events[o->trigger_event],
                actions[o->trigger_action], o->target_id);
            if (o->trigger_has_group)
                put(&w, ",\"groupId\":%d", o->trigger_group_id);
            if (o->trigger_kind == ON_TRIGGER_KIND_MOVE) {
                put(&w, ",\"valueX\":%.4f,\"valueY\":%.4f,\"value\":%.4f",
                    (double)o->trigger_value, (double)o->trigger_value_y,
                    (double)o->trigger_value);
            } else if (o->trigger_kind == ON_TRIGGER_KIND_ROTATE) {
                if (o->trigger_has_duration)
                    put(&w, ",\"duration\":%d", o->trigger_duration);
                else
                    put(&w, ",\"degrees\":%.4f,\"value\":%.4f",
                        (double)o->trigger_value, (double)o->trigger_value);
            } else if (o->trigger_kind == ON_TRIGGER_KIND_FOREVER &&
                       !o->trigger_has_group) {
                put(&w, ",\"value\":%.4f", (double)o->trigger_value);
            } else if (o->trigger_kind == ON_TRIGGER_KIND_GRAVITY) {
                put(&w, ",\"value\":%.4f", (double)o->trigger_value);
            }
            put(&w, ",\"color\":\"#%06x\"}", (unsigned)o->trigger_color);
        } else if (o->type == ON_LEVEL_PARTICLE) {
            const OnLevelParticle *emitter = &o->emitter;
            put(&w, ",\"emitter\":{\"enabled\":%s,\"continuous\":%s,"
                "\"gravityEnabled\":%s,\"glow\":%s,\"rate\":%d,"
                "\"lifetime\":%.1f,\"speed\":%d,\"spread\":%d,"
                "\"size\":%d,\"direction\":%d,\"gravity\":%d}",
                emitter->enabled ? "true" : "false",
                emitter->continuous ? "true" : "false",
                emitter->gravity_enabled ? "true" : "false",
                emitter->glow ? "true" : "false", emitter->rate,
                (double)emitter->lifetime, emitter->speed, emitter->spread,
                emitter->size, emitter->direction, emitter->gravity);
        }
        put(&w, "}");
    }
    put(&w, "]}}");
    if (w.bad) {if (out) out[0] = 0;return 0;}
    return w.at;
}
size_t on_protocol_level_summary_json(const OnPublishedLevel *level,
                                      char *out, size_t cap, int64_t updated_at) {
    if (!out || !cap || updated_at < 0 || !published_level_valid(level)) return 0;
    JW w = {out, cap, 0, 0};
    put(&w, "{\"id\":");put_json_string(&w, level->id);
    put(&w, ",\"title\":");put_json_string(&w, level->title);
    put(&w, ",\"description\":");put_json_string(&w, level->description);
    put(&w, ",\"updatedAt\":%lld}", (long long)updated_at);
    if (w.bad) {out[0] = 0;return 0;}
    return w.at;
}
