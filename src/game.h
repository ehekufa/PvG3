#ifndef GAME_H_INCLUDED
#define GAME_H_INCLUDED

#include <stdint.h>

/* Internal virtual resolution the game renders at. The platform layer
 * stretches this framebuffer to the real screen. */
#define GAME_W 1280
#define GAME_H 720

/* Public phases (use these names rather than assuming integer values). */
enum { GAME_MENU, GAME_INTRO, GAME_PLAY, GAME_LEVEL_CLEAR, GAME_WIN, GAME_LOSE };

/* Reset the game back to the title screen. */
void game_init(void);

/* Current level, 1..10. */
int game_level(void);

/* Pointer events in virtual coordinates (0..GAME_W, 0..GAME_H). */
void game_input_press(int x, int y);
void game_input_release(int x, int y);

/* Advance by dt seconds. If fb is non-NULL, render to GAME_W*GAME_H pixels
 * in RGBA8 byte order. Pass NULL to simulate without drawing (tests). */
void game_tick(float dt, uint32_t *fb);

/* Host screenshot helper: jump into a populated level-10 scene. */
void game_debug_snapshot(void);

/* Current GAME_* phase. */
int game_phase(void);

#ifdef GAME_TEST
/* Deterministic test hooks; excluded from the Android build. */
void game_debug_finish_wave(void);
void game_debug_defeat_boss(void);
int game_debug_boss_alive(void);
float game_debug_boss_x(void);
int game_debug_plant_type(int row, int col);
int game_debug_mower_used(int row);
#endif

#endif /* GAME_H_INCLUDED */
