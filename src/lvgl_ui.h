#ifndef PVG3_LVGL_UI_H
#define PVG3_LVGL_UI_H
#include <stdint.h>

/* Main-thread LVGL adapter: the game owns gameplay, saves, and networking.
 * Level catalog/player and formula-drawn touch buttons are native C UI. */
int lvgl_ui_init(void);
void lvgl_ui_shutdown(void);
int lvgl_ui_fullscreen(int phase);
/* Returns 1 when LVGL consumed a touch; pass other touches to game_input_*.
 * All coordinates are in the game's 1280x720 virtual space. A packet is
 * dragged with DOWN, MOVE and UP; CANCEL must never plant a packet. */
int lvgl_ui_pointer(int x, int y, int pressed);
/* Android sends each finger's stable pointer ID here so direction + jump can
 * remain held independently during the same multitouch gesture. */
int lvgl_ui_touch_pointer(int pointer_id, int x, int y, int pressed);
int lvgl_ui_move(int x, int y);
int lvgl_ui_cancel(void);
/* Call after game_tick; composites LVGL's ARGB8888 layer over RGBA8 gameplay. */
void lvgl_ui_frame(float dt, uint32_t *game_rgba);

#ifdef PVG3_LVGL_TEST
#include "online_level.h"
int lvgl_ui_test_art_loaded(int id);
int lvgl_ui_test_workshop_level(OnPublishedLevel *level);
#endif

#endif
