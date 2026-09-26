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
       GAME_GARDEN, GAME_BOOK, GAME_SELECT };

/* Reset the game back to the title screen. */
void game_init(void);

/* Campaign progress and currently running countdown (seconds). */
int game_level(void);
int game_completed_level(void);
int game_resume_level(void);
float game_seconds_left(void);

/* Versioned, checksummed campaign snapshot. Includes the board, countdown,
 * coins, enemies and unlocked progress; load returns to the menu, where Play
 * resumes an in-progress battle. The separately saved Zen Garden is excluded.
 * Import validates the entire snapshot before changing any game state. */
size_t game_save_size(void);
int game_save_export(void *dst, size_t capacity);
int game_save_import(const void *src, size_t length);

/* Pointer events in virtual coordinates (0..GAME_W, 0..GAME_H). */
void game_input_press(int x, int y);
void game_input_release(int x, int y);

/* Advance by dt seconds. If fb is non-NULL, render to GAME_W*GAME_H pixels
 * in RGBA8 byte order. Pass NULL to simulate without drawing (tests). */
void game_tick(float dt, uint32_t *fb);

/* Host screenshot helper: jump into a populated level-10 scene. */
void game_debug_snapshot(void);

/* Zen Garden layout is stored by the Android host in the app's private data.
 * 0 = empty plot, 1..3 = one of Kirill's three drawn plants. */
#define GAME_GARDEN_CELLS 45
void game_garden_export(uint8_t cells[GAME_GARDEN_CELLS]);
int game_garden_import(const uint8_t cells[GAME_GARDEN_CELLS]);

/* Current GAME_* phase. */
int game_phase(void);

#ifdef GAME_TEST
/* Deterministic test hooks; excluded from the Android build. */
void game_debug_finish_wave(void);
void game_debug_defeat_boss(void);
int game_debug_boss_alive(void);
float game_debug_boss_x(void);
void game_debug_boss_set_x(float x);
int game_debug_plant_type(int row, int col);
int game_debug_seed_count(void);
int game_debug_first_enemy_type(void);
int game_debug_mower_used(int row);
int game_debug_coin_balance(void);
int game_debug_coin_count(void);
int game_debug_garden_plant_type(int row, int col);
int game_debug_book_plant(void);
#endif

#endif /* GAME_H_INCLUDED */
