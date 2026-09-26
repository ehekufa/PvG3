/* Deterministic desktop regression tests; no framebuffer or Android needed.
 * gcc -std=c11 -O2 -Wall -Wextra -Werror -DGAME_TEST -Isrc \
 *     src/game.c tools/sim_test.c -o sim_test -lm && ./sim_test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

static int cell_x(int col) { return 120 + col * 120 + 60; }
static int cell_y(int row) { return 150 + row * 108 + 54; }

static void seed_at(int type, int row, int col) {
    game_input_press(285 + type * 205 + 85, 70);
    game_input_press(cell_x(col), cell_y(row));
}

static void garden_at(int type, int row, int col) {
    game_input_press(285 + type * 205 + 85, 70);
    game_input_press(cell_x(col), cell_y(row));
}

int main(void) {
    game_init();
    assert(game_phase() == GAME_MENU && game_level() == 1);
    game_input_press(5, 5);                  /* menu isn't a tap-anywhere start */
    assert(game_phase() == GAME_MENU);

    /* Zen Garden contains exactly the three drawings and never spends coins. */
    assert(game_debug_coin_balance() == 250);
    game_input_press(480, 640);              /* САД ДЗЕН */
    assert(game_phase() == GAME_GARDEN);
    assert(game_debug_seed_count() == 3);
    assert(game_debug_garden_plant_type(0, 1) == -1);
    garden_at(2, 0, 1);                       /* Kirill's coin sunflower */
    garden_at(0, 2, 2);                       /* peashooter */
    garden_at(1, 3, 4);                       /* walnut */
    assert(game_debug_garden_plant_type(0, 1) == 2);
    assert(game_debug_garden_plant_type(2, 2) == 0);
    assert(game_debug_garden_plant_type(3, 4) == 1);
    assert(game_debug_coin_balance() == 250);
    game_input_press(1100, 114);             /* УБРАТЬ, then tap a planted cell */
    game_input_press(cell_x(2), cell_y(2));
    assert(game_debug_garden_plant_type(2, 2) == -1);
    garden_at(0, 2, 2);                       /* place it again for saving */
    game_input_press(985, 63);               /* garden -> book */
    assert(game_phase() == GAME_BOOK);
    game_input_press(320, 175 + 2 * 155 + 25);
    assert(game_debug_book_plant() == 2);    /* sunflower's picture + info */
    game_input_press(1130, 50);              /* book -> garden */
    assert(game_phase() == GAME_GARDEN);
    game_input_press(1160, 60);              /* garden -> menu */
    assert(game_phase() == GAME_MENU);
    game_input_press(480, 640);              /* garden survives menu visits */
    assert(game_debug_garden_plant_type(0, 1) == 2);
    game_input_press(1160, 60);

    /* Android persists exactly these bytes in private storage. Invalid files
     * must leave a previously loaded garden untouched. */
    uint8_t saved[GAME_GARDEN_CELLS], bad[GAME_GARDEN_CELLS];
    game_garden_export(saved);
    game_init();
    assert(game_debug_garden_plant_type(0, 1) == -1);
    assert(game_garden_import(saved));
    assert(game_debug_garden_plant_type(0, 1) == 2);
    assert(game_debug_garden_plant_type(2, 2) == 0);
    assert(game_debug_garden_plant_type(3, 4) == 1);
    memcpy(bad, saved, sizeof bad);
    bad[0] = 255;
    assert(!game_garden_import(bad));
    assert(game_debug_garden_plant_type(0, 1) == 2);
    game_input_press(790, 640);              /* menu -> book -> menu */
    assert(game_phase() == GAME_BOOK);
    game_input_press(320, 175 + 155 + 25);
    assert(game_debug_book_plant() == 1);
    game_input_press(1130, 50);
    assert(game_phase() == GAME_MENU);

    game_input_press(640, 540);              /* ИГРАТЬ */
    assert(game_phase() == GAME_INTRO);
    game_tick(3.6f, NULL);                   /* first shot advances automatically */
    assert(game_phase() == GAME_INTRO);
    game_input_press(640, 620);              /* Dima -> Kirill */
    assert(game_phase() == GAME_INTRO);
    game_input_press(640, 620);              /* Kirill -> first level */
    assert(game_phase() == GAME_PLAY && game_level() == 1);

    /* The book pauses the battle, and no money falls from the sky. */
    assert(game_debug_coin_balance() == 250 && game_debug_coin_count() == 0);
    game_input_press(970, 115);              /* open the book from the battlefield */
    assert(game_phase() == GAME_BOOK);
    game_tick(30.0f, NULL);
    assert(game_debug_coin_count() == 0 && game_debug_first_enemy_type() == -1);
    game_input_press(1130, 50);
    assert(game_phase() == GAME_PLAY);
    game_tick(9.0f, NULL);
    assert(game_debug_coin_count() == 0 && game_debug_coin_balance() == 250);
    assert(game_debug_plant_type(0, 0) == -1);

    seed_at(2, 0, 1);                         /* flower costs 50 coins */
    assert(game_debug_plant_type(0, 1) == 2);
    assert(game_debug_coin_balance() == 200);
    game_tick(4.9f, NULL);
    assert(game_debug_coin_count() == 0);
    game_tick(0.2f, NULL);                   /* first +25 coin after ~5 seconds */
    assert(game_debug_coin_count() == 1);
    assert(game_debug_coin_balance() == 200);/* coins must be picked up */
    game_input_press(cell_x(1) + 26, cell_y(0) - 14);
    assert(game_debug_coin_count() == 0 && game_debug_coin_balance() == 225);
    seed_at(0, 0, 0);                         /* peashooter costs 100 coins */
    assert(game_debug_plant_type(0, 0) == 0);
    assert(game_debug_coin_balance() == 125);
    seed_at(1, 0, 0);                         /* can't replace an occupied cell */
    assert(game_debug_plant_type(0, 0) == 0);
    seed_at(1, 1, 0);                         /* walnut costs 50 coins */
    assert(game_debug_plant_type(1, 0) == 1);
    assert(game_debug_coin_balance() == 75);
    assert(game_debug_first_enemy_type() == 0); /* yellow PNG duck, not Khlebushek */

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

    /* No queen before level 10; she spawns AFTER its regular duck wave. */
    assert(game_level() == 10 && !game_debug_boss_alive());
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    assert(game_debug_first_enemy_type() == 1); /* boss is the only other enemy */
    float start_x = game_debug_boss_x();
    seed_at(2, 1, 8);                         /* robot spans rows 1, 2 and 3 */
    seed_at(1, 2, 8);
    seed_at(0, 3, 8);
    assert(game_debug_plant_type(1, 8) == 2); /* blue flower (NOT a snow pea) */
    assert(game_debug_plant_type(2, 8) == 1); /* walnut */
    assert(game_debug_plant_type(3, 8) == 0); /* peashooter */
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

    /* The actual boss can also be beaten with Kirill's peashooters, without
     * calling the debug defeat hook. Their three lanes all target the mech. */
    for (int n = 1; n <= 9; n++) {
        game_debug_finish_wave(); game_tick(0, NULL);
        game_input_press(640, 510);
    }
    game_debug_finish_wave(); game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    for (int i = 0; i < 5; i++) {
        seed_at(0, 1 + i % 3, i / 3);
        for (int t = 0; t < 152; t++) game_tick(0.05f, NULL);
    }
    for (int t = 0; t < 2400 && game_phase() == GAME_PLAY; t++) game_tick(0.05f, NULL);
    assert(game_phase() == GAME_WIN);
    puts("OK: Zen Garden, Kirill's book, coin sunflower, ten levels and robot");
    return 0;
}
