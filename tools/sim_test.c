/* Deterministic desktop regression tests; no framebuffer or Android needed.
 * gcc -std=c11 -O2 -Wall -Wextra -Werror -DGAME_TEST -Isrc \
 *     src/game.c tools/sim_test.c -o sim_test -lm && ./sim_test
 */
#include <assert.h>
#include <stdio.h>
#include "game.h"

static void seed_at(int type, int row, int col) {
    game_input_press(250 + type * 122 + 55, 70);
    game_input_press(120 + col * 120 + 60, 150 + row * 108 + 54);
}

int main(void) {
    game_init();
    assert(game_phase() == GAME_MENU && game_level() == 1);
    game_input_press(5, 5);                  /* menu is NOT a tap-anywhere start */
    assert(game_phase() == GAME_MENU);
    game_input_press(640, 540);              /* ИГРАТЬ */
    assert(game_phase() == GAME_INTRO);
    game_tick(3.6f, NULL);                   /* first shot advances automatically */
    assert(game_phase() == GAME_INTRO);
    game_input_press(640, 620);              /* Dima -> Kirill */
    assert(game_phase() == GAME_INTRO);
    game_input_press(640, 620);              /* Kirill -> first level */
    assert(game_phase() == GAME_PLAY && game_level() == 1);

    assert(game_debug_plant_type(0, 0) == -1); /* cleared field is plantable */
    seed_at(1, 0, 0);                         /* pea */
    assert(game_debug_plant_type(0, 0) == 1);
    seed_at(0, 0, 0);                         /* occupied cell stays occupied */
    assert(game_debug_plant_type(0, 0) == 1);

    for (int n = 1; n <= 9; n++) {
        assert(game_level() == n && !game_debug_boss_alive());
        game_debug_finish_wave();
        game_tick(0, NULL);
        assert(game_phase() == GAME_LEVEL_CLEAR && game_level() == n);
        game_input_press(10, 10);
        assert(game_phase() == GAME_LEVEL_CLEAR); /* button hit test */
        game_input_press(640, 510);
        assert(game_phase() == GAME_PLAY && game_level() == n + 1);
        assert(game_debug_plant_type(0, 0) == -1);
    }

    /* No queen before level 10; then she spawns AFTER its regular wave. */
    assert(game_level() == 10 && !game_debug_boss_alive());
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    float start_x = game_debug_boss_x();
    seed_at(0, 1, 8);                         /* robot spans rows 1, 2 and 3 */
    seed_at(2, 2, 8);
    seed_at(5, 3, 8);
    assert(game_debug_plant_type(1, 8) == 0);
    assert(game_debug_plant_type(2, 8) == 2);
    assert(game_debug_plant_type(3, 8) == 5);
    for (int i = 0; i < 200; i++) game_tick(0.05f, NULL);
    assert(game_debug_boss_x() < start_x - 95 && game_debug_boss_x() > start_x - 105);
    for (int i = 0; i < 400; i++) game_tick(0.05f, NULL);
    for (int r = 1; r <= 3; r++) assert(game_debug_plant_type(r, 8) == -1);
    for (int i = 0; i < 2600 && game_phase() == GAME_PLAY; i++) game_tick(0.05f, NULL);
    assert(game_phase() == GAME_LOSE);        /* undefeated robot reaches home */
    for (int r = 1; r <= 3; r++) assert(game_debug_mower_used(r));
    game_input_press(490, 515);              /* retry the CURRENT level */
    assert(game_phase() == GAME_PLAY && game_level() == 10);
    assert(!game_debug_boss_alive());
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_debug_boss_alive());
    game_debug_defeat_boss();
    game_tick(0, NULL);
    assert(game_phase() == GAME_WIN && game_level() == 10);
    game_input_press(640, 520);              /* final victory -> menu */
    assert(game_phase() == GAME_MENU && game_level() == 1);
    game_input_press(640, 540);
    game_input_press(1150, 50);              /* skip entire intro */
    assert(game_phase() == GAME_PLAY && game_level() == 1);

    game_init();                             /* untouched cutscene auto-plays */
    game_input_press(640, 540);
    for (int i = 0; i < 12 * 60; i++) game_tick(1.0f / 60, NULL);
    assert(game_phase() == GAME_PLAY && game_level() == 1);
    puts("OK: menu, auto/manual cutscene, ten levels, robot, defeat/retry/victory");
    return 0;
}
