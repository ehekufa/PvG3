#ifndef GAME_H_INCLUDED
#define GAME_H_INCLUDED

#include <stdint.h>

/* Internal virtual resolution the game renders at. The platform layer
 * stretches this framebuffer to the real screen. */
#define GAME_W 1280
#define GAME_H 720

/* Reset the game back to the title screen. */
void game_init(void);

/* Pointer events in virtual coordinates (0..GAME_W, 0..GAME_H). */
void game_input_press(int x, int y);
void game_input_release(int x, int y);
/* Pointer drag (touch move) — used for the look controls and the move
 * joystick so the camera can be steered continuously. */
void game_input_move(int x, int y);

/* Advance the simulation by dt seconds and render one frame into fb.
 * fb must hold GAME_W*GAME_H uint32_t pixels in RGBA8 byte order (R,G,B,A). */
void game_tick(float dt, uint32_t *fb);

/* Host-only helper: jump straight into a populated scene (for screenshots). */
void game_debug_snapshot(void);

/* Current game phase: 0=menu 1=play 2=win 3=lose. */
int game_phase(void);

#endif /* GAME_H_INCLUDED */
