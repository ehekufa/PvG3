#ifndef PVG3_ONLINE_LEVEL_H
#define PVG3_ONLINE_LEVEL_H

#include <stdint.h>

/* Bounded, account-free reader/runtime for already-published user levels.
 * Artwork is referenced by built-in PNG-backed object types; geometry and
 * trigger configuration remain compatible with the existing JSON records. */
#define ON_LEVEL_ID_SIZE 8       /* decimal 1..999999 plus NUL */
#define ON_LEVEL_TITLE_SIZE 241 /* 80 UTF-16 code units, worst-case UTF-8 */
#define ON_LEVEL_DESCRIPTION_SIZE 481 /* 160 UTF-16 code units, worst-case UTF-8 */
#define ON_LEVEL_OBJECT_NAME_SIZE 145 /* 48 UTF-16 code units, worst-case UTF-8 */
#define ON_LEVEL_LIST_CAP 24
/* This is a total-per-level ceiling across every object type, not a per-type
 * allowance. Keep the wire and both workshops aligned with this value. */
#define ON_LEVEL_OBJECT_CAP 20000
#define ON_LEVEL_WORLD_LIMIT 100000

enum {
    ON_LEVEL_BLOCK, ON_LEVEL_GROUND, ON_LEVEL_HAZARD, ON_LEVEL_COIN,
    ON_LEVEL_ENEMY, ON_LEVEL_PLAYER, ON_LEVEL_GOAL, ON_LEVEL_TRIGGER,
    ON_LEVEL_SLOPE /* appended to keep all existing published type IDs stable */
};
enum { ON_TRIGGER_TOUCH, ON_TRIGGER_COIN, ON_TRIGGER_MANUAL, ON_TRIGGER_START };
enum { ON_TRIGGER_TOGGLE, ON_TRIGGER_MOVE, ON_TRIGGER_RECOLOR,
       ON_TRIGGER_NUMBER, ON_TRIGGER_ROTATE, ON_TRIGGER_ACTIVATE,
       ON_TRIGGER_UNACTIVATE, ON_TRIGGER_INVISIBLE,
       ON_TRIGGER_NO_COLLISION };
enum { ON_TRIGGER_KIND_MOVE, ON_TRIGGER_KIND_ROTATE, ON_TRIGGER_KIND_FOREVER,
       ON_TRIGGER_KIND_INVISIBILITY, ON_TRIGGER_KIND_NO_COLLISION };

typedef struct {
    char id[ON_LEVEL_ID_SIZE];
    char title[ON_LEVEL_TITLE_SIZE];
    char description[ON_LEVEL_DESCRIPTION_SIZE];
} OnPublishedLevelSummary;

typedef struct {
    int id, type;
    char name[ON_LEVEL_OBJECT_NAME_SIZE];
    float x, y, w, h, angle;
    int flip_x, flip_y;
    uint32_t color;
    int number, visible;
    int trigger_kind, trigger_event, trigger_action, target_id;
    float trigger_value, trigger_value_y;
    uint32_t trigger_color;
    int trigger_group_id, trigger_has_group;
    int trigger_duration, trigger_has_duration; /* timed group rotation, seconds */
} OnLevelObject;

typedef struct {
    char id[ON_LEVEL_ID_SIZE];
    char title[ON_LEVEL_TITLE_SIZE];
    char description[ON_LEVEL_DESCRIPTION_SIZE];
    int width, height;
    int object_count;
    OnLevelObject objects[ON_LEVEL_OBJECT_CAP];
} OnPublishedLevel;

int on_protocol_valid_level_id(const char *id);
int on_protocol_level_index(const char *json, OnPublishedLevelSummary *out, int cap);
int on_protocol_published_level(const char *json, const char *expected_id,
                                OnPublishedLevel *out);

#endif
