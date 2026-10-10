#ifndef PVG3_ONLINE_LEVEL_H
#include "online_account.h"
#define PVG3_ONLINE_LEVEL_H

#include <stdint.h>
#include <string.h>

/* Bounded, account-free reader/runtime for already-published user levels.
 * Artwork is referenced by built-in PNG-backed object types; geometry and
 * trigger configuration remain compatible with the existing JSON records. */
#define ON_LEVEL_ID_SIZE 8       /* decimal 1..999999 plus NUL */
#define ON_LEVEL_OFFICIAL_ID "338069"
#define ON_LEVEL_TITLE_SIZE 241 /* 80 UTF-16 code units, worst-case UTF-8 */

static inline int on_level_id_is_official(const char *id) {
    return id && strcmp(id, ON_LEVEL_OFFICIAL_ID) == 0;
}
/* The badge follows the flag a moderator set, and the historic ID as before. */
static inline int on_level_is_official(const char *id, int flagged) {
    return !!flagged || on_level_id_is_official(id);
}
#define ON_LEVEL_DESCRIPTION_SIZE 481 /* 160 UTF-16 code units, worst-case UTF-8 */
#define ON_LEVEL_OBJECT_NAME_SIZE 145 /* 48 UTF-16 code units, worst-case UTF-8 */
#define ON_LEVEL_TAG_CAP 8
#define ON_LEVEL_TAG_SIZE 49 /* 16 UTF-16 code units, worst-case UTF-8 */
#define ON_LEVEL_LIST_CAP 80

enum { ON_LEVEL_DIFFICULTY_UNSPECIFIED, ON_LEVEL_DIFFICULTY_EASY,
       ON_LEVEL_DIFFICULTY_NORMAL, ON_LEVEL_DIFFICULTY_HARD,
       ON_LEVEL_DIFFICULTY_EXPERT };
enum { ON_LEVEL_ABILITY_DOUBLE_JUMP = 1u, ON_LEVEL_ABILITY_DASH = 2u,
       ON_LEVEL_ABILITY_WALL_SLIDE = 4u };
/* This is a total-per-level ceiling across every object type, not a per-type
 * allowance. Keep the wire and both workshops aligned with this value. */
#define ON_LEVEL_OBJECT_CAP 20000
#define ON_LEVEL_WORLD_LIMIT 100000

enum {
    ON_LEVEL_BLOCK, ON_LEVEL_GROUND, ON_LEVEL_HAZARD, ON_LEVEL_COIN,
    ON_LEVEL_ENEMY, ON_LEVEL_PLAYER, ON_LEVEL_GOAL, ON_LEVEL_TRIGGER,
    ON_LEVEL_SLOPE, /* appended to keep all existing published type IDs stable */
    ON_LEVEL_ORB_YELLOW, ON_LEVEL_ORB_ORANGE,
    ON_LEVEL_PARTICLE, /* configurable particle trail */
    ON_LEVEL_CHECKPOINT, /* activates on overlap and remembers a respawn point */
    ON_LEVEL_PORTAL_NORMAL, /* restores the normal player form */
    ON_LEVEL_PORTAL_JETPACK /* switches to the jetpack form */
};
enum { ON_TRIGGER_TOUCH, ON_TRIGGER_COIN, ON_TRIGGER_MANUAL, ON_TRIGGER_START };
enum { ON_TRIGGER_TOGGLE, ON_TRIGGER_MOVE, ON_TRIGGER_RECOLOR,
       ON_TRIGGER_NUMBER, ON_TRIGGER_ROTATE, ON_TRIGGER_ACTIVATE,
       ON_TRIGGER_UNACTIVATE, ON_TRIGGER_INVISIBLE,
       ON_TRIGGER_NO_COLLISION, ON_TRIGGER_SET_GRAVITY,
       ON_TRIGGER_SET_BACKGROUND };
enum { ON_TRIGGER_KIND_MOVE, ON_TRIGGER_KIND_ROTATE, ON_TRIGGER_KIND_FOREVER,
       ON_TRIGGER_KIND_INVISIBILITY, ON_TRIGGER_KIND_NO_COLLISION,
       ON_TRIGGER_KIND_GRAVITY, ON_TRIGGER_KIND_RECOLOR,
       ON_TRIGGER_KIND_BACKGROUND, ON_TRIGGER_KIND_COUNT,
       ON_TRIGGER_KIND_TOGGLE, ON_TRIGGER_KIND_SPAWN };
enum { ON_TRIGGER_TOUCH_ENTER, ON_TRIGGER_TOUCH_EXIT, ON_TRIGGER_TOUCH_STAY };

typedef struct {
    char id[ON_LEVEL_ID_SIZE];
    char title[ON_LEVEL_TITLE_SIZE];
    char description[ON_LEVEL_DESCRIPTION_SIZE];
    /* A moderator marks a level official; the historic ID stays official too. */
    int official;
    /* Login of the account that published the level, empty for older levels. */
    char author[ON_LOGIN_SIZE];
    int difficulty; /* ON_LEVEL_DIFFICULTY_*, 0 for unrated legacy entries */
    int tag_count;
    char tags[ON_LEVEL_TAG_CAP][ON_LEVEL_TAG_SIZE];
    unsigned likes, downloads;
    int liked, downloaded; /* this installation's votes */
    int64_t updated_at; /* server timestamp in milliseconds; 0 for legacy */
} OnPublishedLevelSummary;

typedef struct {
    int enabled, continuous, gravity_enabled, glow;
    int rate;              /* particles/second, or particles per burst */
    float lifetime;        /* seconds */
    int speed, spread, size, direction, gravity; /* px/s, degrees, px, degrees, px/s^2 */
} OnLevelParticle;

#define ON_LEVEL_PARTICLE_MAX_VISIBLE 48

static inline OnLevelParticle on_level_particle_default(void) {
    return (OnLevelParticle){
        .enabled=1, .continuous=1, .gravity_enabled=0, .glow=1,
        .rate=8, .lifetime=1.2f, .speed=90, .spread=40,
        .size=4, .direction=-90, .gravity=90
    };
}

static inline int on_level_particle_valid(const OnLevelParticle *emitter) {
    return emitter &&
        (emitter->enabled == 0 || emitter->enabled == 1) &&
        (emitter->continuous == 0 || emitter->continuous == 1) &&
        (emitter->gravity_enabled == 0 || emitter->gravity_enabled == 1) &&
        (emitter->glow == 0 || emitter->glow == 1) &&
        emitter->rate >= 1 && emitter->rate <= 30 &&
        emitter->lifetime >= .2f && emitter->lifetime <= 3.0f &&
        emitter->speed >= 0 && emitter->speed <= 300 &&
        emitter->spread >= 0 && emitter->spread <= 180 &&
        emitter->size >= 1 && emitter->size <= 12 &&
        emitter->direction >= -180 && emitter->direction <= 180 &&
        emitter->gravity >= 0 && emitter->gravity <= 300;
}

typedef struct {
    int id, type;
    char name[ON_LEVEL_OBJECT_NAME_SIZE];
    float x, y, w, h, angle;
    int flip_x, flip_y;
    int layer, layer2, z_order;
    int alpha; /* 0..100 */
    int pulse, shake;
    uint32_t color;
    /* Objects flagged as default keep the colours of their own artwork:
     * no tint is mixed into the author's picture. */
    int color_default;
    int number, visible;
    int trigger_kind, trigger_event, trigger_action, target_id;
    int trigger_touch_mode, trigger_count;
    float trigger_value, trigger_value_y;
    uint32_t trigger_color;
    /* A recolor/background trigger flagged as default restores the normal
     * look of its targets or of the level backdrop. */
    int trigger_color_default;
    int trigger_group_id, trigger_has_group;
    int trigger_duration, trigger_has_duration; /* timed group rotation, seconds */
    OnLevelParticle emitter; /* ignored for non-particle objects */
} OnLevelObject;

typedef struct {
    char id[ON_LEVEL_ID_SIZE];
    char title[ON_LEVEL_TITLE_SIZE];
    char description[ON_LEVEL_DESCRIPTION_SIZE];
    int width, height;
    int object_count;
    int official; /* moderator's «ОФИЦИАЛЬНЫЙ» badge, 0 for ordinary levels */
    /* Publishing account. The short-lived token is included only in an
     * outbound write, then rotated; readers keep the public login only. */
    char author[ON_LOGIN_SIZE];
    char author_token[ON_TOKEN_SIZE];
    int difficulty; /* ON_LEVEL_DIFFICULTY_* */
    int tag_count;
    char tags[ON_LEVEL_TAG_CAP][ON_LEVEL_TAG_SIZE];
    unsigned movement_abilities; /* ON_LEVEL_ABILITY_* bitset */
    OnLevelObject objects[ON_LEVEL_OBJECT_CAP];
} OnPublishedLevel;

int on_protocol_valid_level_id(const char *id);
int on_protocol_level_index(const char *json, OnPublishedLevelSummary *out, int cap);
/* Merge sparse RTDB like/download child maps into already-parsed summaries. */
int on_protocol_level_stats(const char *json, OnPublishedLevelSummary *levels,
                            int count, const char *client_id);
int on_protocol_published_level(const char *json, const char *expected_id,
                                OnPublishedLevel *out);

#endif
