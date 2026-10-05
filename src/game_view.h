#ifndef PVG3_GAME_VIEW_H
#define PVG3_GAME_VIEW_H

#include "online_rules.h"
#include <stdint.h>
#include <stddef.h>

/* Read-only sprites decoded from the author's original PNGs by game_init().
 * Byte order in memory is RGBA; the LVGL adapter converts to ARGB8888 once. */
enum {
    PV_ART_BREAD, PV_ART_DIMA, PV_ART_KIRILL, PV_ART_DUCK,
    PV_ART_DUCK_CONE, PV_ART_DUCK_BUCKET, PV_ART_ROBOT,
    PV_ART_PEA, PV_ART_WALNUT, PV_ART_SUNFLOWER,
    PV_ART_JUMPER, PV_ART_LILY, PV_ART_LAWN, PV_ART_WATER, PV_ART_MOWER,
    PV_ART_COIN, PV_ART_LEVEL_BLOCK, PV_ART_LEVEL_PLATFORM,
    PV_ART_LEVEL_TRIGGER, PV_ART_LEVEL_TRIGGER_ROTATE,
    PV_ART_LEVEL_TRIGGER_FOREVER, PV_ART_LEVEL_TRIGGER_INVISIBILITY,
    PV_ART_LEVEL_TRIGGER_NO_COLLISION, PV_ART_LEVEL_FLAG, PV_ART_LEVEL_SPIKE,
    PV_ART_LEVEL_SLOPE, PV_ART_LEVEL_TRIGGER_GRAVITY,
    PV_ART_LEVEL_ORB_ORANGE, PV_ART_LEVEL_ORB_YELLOW,
    PV_ART_LEVEL_CHECKPOINT_INACTIVE, PV_ART_LEVEL_CHECKPOINT_ACTIVE,
    PV_ART_LEVEL_PORTAL_NORMAL, PV_ART_LEVEL_PORTAL_JETPACK,
    PV_ART_JETPACK_ACTIVE, PV_ART_JETPACK_INACTIVE,
    PV_ART_LEVEL_TRIGGER_COLOR, PV_ART_WORKSHOP_ROTATE,
    PV_ART_COLOR_WHEEL, PV_ART_COUNT
};
const uint32_t *game_art_rgba(int id, int *width, int *height);

/* A main-thread snapshot for the LVGL HUD; networking/gameplay stay in game.c.
 * Must only be called from the render thread after game_tick. */
void game_online_ui_snapshot(OnMatch *match, int *role, int *selection,
                             char *hint, size_t hint_size, float *hint_seconds);

/* Read-only values from the campaign, cut-scene and Zen Garden.
 * LVGL does not own game state; legacy save values and authoritative rules
 * remain supported. */
typedef struct {
    int level, coins, selection, garden_selection;
    int garden_mode, garden_map; /* 0 = plants, 1 = geese; maps 1 = lawn, 5 = water */
    int wave_remaining, wave_total, boss_health_percent;
    int intro_step, book_enemy_tab, book_selection;
    float cooldown[5];
} GameOfflineUIState;
void game_offline_ui_snapshot(GameOfflineUIState *out);

typedef struct {
    const char *name, *short_name, *description, *detail;
    int art_id, cost, hp, enemy_variant;
    float recharge;
} GameBookEntry;
/* 5 plants; 4 enemies (duck, cone duck, bucket duck, queen/robot). */
int game_book_entry(int enemy_tab, int index, GameBookEntry *out);

#endif
