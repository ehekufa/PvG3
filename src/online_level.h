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
#define ON_LEVEL_OBJECT_CAP 120

enum {
    ON_LEVEL_BLOCK, ON_LEVEL_GROUND, ON_LEVEL_HAZARD, ON_LEVEL_COIN,
    ON_LEVEL_ENEMY, ON_LEVEL_PLAYER, ON_LEVEL_GOAL, ON_LEVEL_TRIGGER
};
enum { ON_TRIGGER_TOUCH, ON_TRIGGER_COIN, ON_TRIGGER_MANUAL };
enum { ON_TRIGGER_TOGGLE, ON_TRIGGER_MOVE, ON_TRIGGER_RECOLOR, ON_TRIGGER_NUMBER };

typedef struct {
    char id[ON_LEVEL_ID_SIZE];
    char title[ON_LEVEL_TITLE_SIZE];
    char description[ON_LEVEL_DESCRIPTION_SIZE];
} OnPublishedLevelSummary;

typedef struct {
    int id, type;
    char name[ON_LEVEL_OBJECT_NAME_SIZE];
    float x, y, w, h, angle;
    uint32_t color;
    int number, visible;
    int trigger_event, trigger_action, target_id;
    float trigger_value;
    uint32_t trigger_color;
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
