#ifndef GAME_H_INCLUDED
#define GAME_H_INCLUDED

#include <stdint.h>

/* Virtual resolution the game renders at. */
#define GAME_W 1280
#define GAME_H 720

void game_init(void);

/* Multitouch input — each finger has a unique pointer id.
 * Coordinates in virtual space (0..GAME_W, 0..GAME_H). */
void game_touch_down(int id, int x, int y);
void game_touch_move(int id, int x, int y);
void game_touch_up(int id, int x, int y);

/* Advance simulation by dt seconds, render into fb (RGBA8, GAME_W*GAME_H). */
void game_tick(float dt, uint32_t *fb);

void game_debug_snapshot(void);
int game_phase(void);

#endif
