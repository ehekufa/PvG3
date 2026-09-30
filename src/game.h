#ifndef GAME_H_INCLUDED
#define GAME_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

/* Internal virtual resolution the game renders at. The platform layer
 * stretches this framebuffer to the real screen. */
#define GAME_W 1280
#define GAME_H 720

/* Public phases (use these names rather than assuming integer values). */
enum { GAME_MENU, GAME_INTRO, GAME_PLAY, GAME_LEVEL_CLEAR, GAME_WIN, GAME_LOSE,
       GAME_GARDEN, GAME_BOOK, GAME_SELECT,
       GAME_ONLINE_ROOMS, GAME_ONLINE_LOBBY, GAME_ONLINE_MATCH };

/* Reset the game back to the title screen. */
void game_init(void);

/* Current level is 0 during the replayable intro, otherwise 1..10.
 * game_completed_level() counts individually completed combat levels. */
int game_level(void);
int game_completed_level(void);
int game_resume_level(void);
/* Includes not-yet-spawned opponents; excludes the separate robot boss. */
int game_wave_remaining(void);
int game_wave_total(void);

/* Versioned, checksummed campaign snapshot. Includes the board, wave,
 * coins, enemies and unlocked progress; load returns to the menu, where Play
 * resumes an in-progress battle. The separately saved Zen Garden is excluded.
 * Import validates the entire snapshot before changing any game state and
 * also accepts the shorter V1-V4 and same-sized V5 files from earlier APKs. */
size_t game_save_size(void);
int game_save_export(void *dst, size_t capacity);
int game_save_import(const void *src, size_t length);

/* Pointer events in virtual coordinates (0..GAME_W, 0..GAME_H). */
void game_input_press(int x, int y);
void game_input_release(int x, int y);
/* The menu's ONLINE button enters native rooms inside this game; networking
 * runs on a separate thread. The campaign and garden save formats exclude it. */

/* Advance by dt seconds. If fb is non-NULL, render to GAME_W*GAME_H pixels
 * in RGBA8 byte order. Pass NULL to simulate without drawing (tests). */
void game_tick(float dt, uint32_t *fb);
/* Android's LVGL adapter draws the online match HUD itself. Keep the old HUD
 * for independent renderer tests and as a fallback if LVGL cannot initialize. */
void game_set_lvgl_ui(int enabled);

/* Host screenshot helpers: jump into illustrated combat scenes. */
void game_debug_snapshot(void);
void game_debug_armored_snapshot(void);

/* Zen Garden layout is stored by the Android host in the app's private data.
 * 0 = empty plot, 1..5 = one of Kirill's five drawn plants. Old IDs 1..4 stay. */
#define GAME_GARDEN_CELLS 45
void game_garden_export(uint8_t cells[GAME_GARDEN_CELLS]);
int game_garden_import(const uint8_t cells[GAME_GARDEN_CELLS]);

/* Current GAME_* phase. */
int game_phase(void);

#ifdef GAME_TEST
/* Deterministic test hooks; excluded from the Android build. */
void game_debug_finish_wave(void);
void game_debug_spawn_duck(int row, float x);
void game_debug_spawn_armored_duck(int row, float x, int type);
float game_debug_enemy_hp(int type);
float game_debug_duck_x(int row);
void game_debug_defeat_boss(void);
int game_debug_boss_alive(void);
float game_debug_boss_x(void);
void game_debug_boss_set_x(float x);
float game_debug_first_pea_x(void);
float game_debug_first_pea_y(void);
float game_debug_first_coin_x(void);
float game_debug_first_coin_y(void);
float game_debug_first_coin_target_y(void);
float game_debug_mower_x(int row);
int game_debug_plant_type(int row, int col);
int game_debug_lily_at(int row, int col);
int game_debug_seed_count(void);
int game_debug_first_enemy_type(void);
int game_debug_mower_used(int row);
int game_debug_coin_balance(void);
int game_debug_coin_count(void);
float game_debug_cooldown(int plant);
int game_debug_garden_plant_type(int row, int col);
int game_debug_book_plant(void);
int game_debug_book_enemy(void);
int game_debug_level_completed(int level);
#endif

#endif /* GAME_H_INCLUDED */
