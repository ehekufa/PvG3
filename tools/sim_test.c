/* Deterministic desktop regression tests; no Android or framebuffer needed.
 * gcc -std=c11 -O2 -Wall -Wextra -Werror -DGAME_TEST -Isrc \
 *     src/game.c src/font.c tools/sim_test.c -o sim_test -lm && ./sim_test
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "font.h"

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

/* Small steps, like the Android game loop; stop when a result screen opens. */
static void advance(float seconds) {
    while (seconds > 0.0001f && game_phase() == GAME_PLAY) {
        float dt = seconds < 0.05f ? seconds : 0.05f;
        game_tick(dt, NULL);
        seconds -= dt;
    }
}

static void pass_timer(void) { advance(game_seconds_left() + 0.15f); }

int main(void) {
    /* Embedded PT Sans really covers Cyrillic and has anti-aliased edges. */
    assert(font_init() && font_has_glyph(0x416) && font_has_glyph(0x451));
    assert(font_width(3, "КИРИЛЛ И ЁЖ") > 80);
    uint32_t letters[180 * 30] = {0};
    font_draw(letters, 180, 30, 0, 0, 2, 0xffffffffu, "Ёж");
    int edge_pixels = 0;
    for (size_t i = 0; i < 180 * 30; i++)
        if ((letters[i] & 255u) > 0 && (letters[i] & 255u) < 255)
            edge_pixels++;
    assert(edge_pixels > 0);

    game_init();
    assert(game_phase() == GAME_MENU && game_level() == 1);
    assert(game_completed_level() == 0 && game_resume_level() == 1);
    game_input_press(5, 5);                  /* menu is not tap-anywhere */
    assert(game_phase() == GAME_MENU);

    /* Every level can be selected, even on a fresh install; back is clickable. */
    game_input_press(325, 640);              /* УРОВНИ */
    assert(game_phase() == GAME_SELECT);
    game_input_press(10, 10);
    assert(game_phase() == GAME_SELECT);
    game_input_press(1130, 50);
    assert(game_phase() == GAME_MENU);

    /* Garden contains exactly Kirill's three drawings and spends no coins. */
    assert(game_debug_coin_balance() == 250);
    game_input_press(625, 640);              /* САД ДЗЕН */
    assert(game_phase() == GAME_GARDEN);
    assert(game_debug_seed_count() == 3);
    garden_at(2, 0, 1);                       /* coin sunflower */
    garden_at(0, 2, 2);                       /* peashooter */
    garden_at(1, 3, 4);                       /* walnut */
    assert(game_debug_garden_plant_type(0, 1) == 2);
    assert(game_debug_garden_plant_type(2, 2) == 0);
    assert(game_debug_garden_plant_type(3, 4) == 1);
    assert(game_debug_coin_balance() == 250);
    game_input_press(1100, 114);             /* eraser */
    game_input_press(cell_x(2), cell_y(2));
    assert(game_debug_garden_plant_type(2, 2) == -1);
    garden_at(0, 2, 2);
    game_input_press(985, 63);               /* garden -> interactive book */
    assert(game_phase() == GAME_BOOK);
    game_input_press(320, 175 + 2 * 155 + 25);
    assert(game_debug_book_plant() == 2);
    game_input_press(1130, 50);              /* book -> garden */
    assert(game_phase() == GAME_GARDEN);
    game_input_press(1160, 60);              /* garden -> menu */
    assert(game_phase() == GAME_MENU);

    /* Android keeps this separate legacy file. Malformed data is rejected. */
    uint8_t garden[GAME_GARDEN_CELLS], bad_garden[GAME_GARDEN_CELLS];
    game_garden_export(garden);
    game_init();
    assert(game_debug_garden_plant_type(0, 1) == -1);
    assert(game_garden_import(garden));
    assert(game_debug_garden_plant_type(0, 1) == 2);
    memcpy(bad_garden, garden, sizeof garden);
    bad_garden[0] = 255;
    assert(!game_garden_import(bad_garden));
    assert(game_debug_garden_plant_type(0, 1) == 2);
    game_input_press(950, 640);              /* menu -> book -> menu */
    assert(game_phase() == GAME_BOOK);
    game_input_press(320, 175 + 155 + 25);
    assert(game_debug_book_plant() == 1);
    game_input_press(1130, 50);
    assert(game_phase() == GAME_MENU);

    game_input_press(640, 540);              /* ИГРАТЬ -> cutscene */
    assert(game_phase() == GAME_INTRO);
    game_tick(3.6f, NULL);                   /* first shot advances automatically */
    assert(game_phase() == GAME_INTRO);
    game_input_press(640, 620);              /* Dima -> Kirill */
    game_input_press(640, 620);              /* Kirill -> first level */
    assert(game_phase() == GAME_PLAY && game_level() == 1);
    assert(game_seconds_left() == 50.0f);

    /* Book pauses both the battle and timer. Sunflowers make pick-up coins. */
    assert(game_debug_coin_balance() == 250 && game_debug_coin_count() == 0);
    game_input_press(970, 115);
    assert(game_phase() == GAME_BOOK);
    game_tick(30.0f, NULL);
    assert(game_seconds_left() == 50.0f && game_debug_first_enemy_type() == -1);
    game_input_press(1130, 50);
    game_tick(9.0f, NULL);
    assert(game_debug_coin_count() == 0 && game_debug_coin_balance() == 250);
    seed_at(2, 0, 1);
    assert(game_debug_plant_type(0, 1) == 2 && game_debug_coin_balance() == 200);
    game_tick(4.9f, NULL);
    assert(game_debug_coin_count() == 0);
    game_tick(0.2f, NULL);
    assert(game_debug_coin_count() == 1 && game_debug_coin_balance() == 200);
    game_input_press(cell_x(1) + 26, cell_y(0) - 14);
    assert(game_debug_coin_count() == 0 && game_debug_coin_balance() == 225);
    seed_at(0, 0, 0);
    assert(game_debug_plant_type(0, 0) == 0 && game_debug_coin_balance() == 125);
    seed_at(1, 0, 0);                         /* cannot overwrite */
    assert(game_debug_plant_type(0, 0) == 0);
    seed_at(1, 1, 0);
    assert(game_debug_plant_type(1, 0) == 1 && game_debug_coin_balance() == 75);
    assert(game_debug_first_enemy_type() == 0); /* drawn duck, not Khlebushek */

    /* Checksum/length failures leave the whole game unchanged. A real restart
     * restores timer, coins, plants and enemies, then Play resumes the board. */
    size_t len = game_save_size();
    assert(len > 100 && len < 65536);
    uint8_t *saved = (uint8_t *)malloc(len), *bad = (uint8_t *)malloc(len);
    assert(saved && bad);
    assert(!game_save_export(saved, len - 1));
    assert(game_save_export(saved, len));
    memcpy(bad, saved, len);
    bad[len / 2] ^= 0x80;
    assert(!game_save_import(bad, len) && !game_save_import(saved, len - 1));
    float time_saved = game_seconds_left();
    game_init();
    assert(game_debug_garden_plant_type(0, 1) == -1);
    assert(game_garden_import(garden));      /* independently persisted garden */
    assert(game_save_import(saved, len));
    assert(game_phase() == GAME_MENU && game_resume_level() == 1);
    game_input_press(640, 540);              /* continue, no intro or reset */
    assert(game_phase() == GAME_PLAY && game_level() == 1);
    assert(fabsf(game_seconds_left() - time_saved) < 0.001f);
    assert(game_debug_coin_balance() == 75 && game_debug_plant_type(0, 1) == 2);
    assert(game_debug_first_enemy_type() == 0);
    game_input_press(1200, 115);             /* leaving battle saves it too */
    assert(game_phase() == GAME_MENU);
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 540);
    assert(game_phase() == GAME_PLAY && game_debug_plant_type(1, 0) == 1);

    /* Eliminating the entire wave early does NOT win or summon a boss. */
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY);
    pass_timer();
    assert(game_phase() == GAME_LEVEL_CLEAR && game_level() == 1);
    assert(game_completed_level() == 1 && game_resume_level() == 2);
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len));
    assert(game_phase() == GAME_MENU && game_completed_level() == 1);
    game_input_press(640, 540);              /* continue from checkpoint */
    assert(game_phase() == GAME_PLAY && game_level() == 2);
    assert(game_debug_plant_type(0, 0) == -1);

    for (int n = 2; n <= 9; n++) {
        assert(game_level() == n && !game_debug_boss_alive());
        assert(fabsf(game_seconds_left() - (45.0f + n * 5.0f)) < 0.001f);
        game_debug_finish_wave();
        game_tick(0, NULL);
        assert(game_phase() == GAME_PLAY);
        pass_timer();
        assert(game_phase() == GAME_LEVEL_CLEAR && game_level() == n);
        assert(game_completed_level() == n && game_resume_level() == n + 1);
        game_input_press(10, 10);            /* result isn't tap-anywhere */
        assert(game_phase() == GAME_LEVEL_CLEAR);
        game_input_press(640, 510);
        assert(game_phase() == GAME_PLAY && game_level() == n + 1);
    }

    /* Queen appears only after the FULL countdown of level 10, regardless of
     * whether any ducks remain. The slow robot destroys plants in 3 lanes. */
    assert(game_level() == 10 && !game_debug_boss_alive());
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && !game_debug_boss_alive());
    advance(94.0f);
    assert(game_phase() == GAME_PLAY && !game_debug_boss_alive());
    pass_timer();
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    assert(game_debug_first_enemy_type() == 1 && game_seconds_left() > 99.0f);
    float start_x = game_debug_boss_x();
    seed_at(2, 1, 8);
    seed_at(1, 2, 8);
    seed_at(0, 3, 8);
    assert(game_debug_plant_type(1, 8) == 2);
    assert(game_debug_plant_type(2, 8) == 1);
    assert(game_debug_plant_type(3, 8) == 0);
    advance(10.0f);
    assert(game_debug_boss_x() < start_x - 99 && game_debug_boss_x() > start_x - 101);
    for (int r = 1; r <= 3; r++) assert(game_debug_plant_type(r, 8) == -1);

    /* Resume the actual robot, not a fresh copy; retry resets after a defeat. */
    assert(game_save_export(saved, len));
    time_saved = game_seconds_left();
    float x_saved = game_debug_boss_x();
    game_init();
    assert(game_save_import(saved, len));
    assert(game_phase() == GAME_MENU);
    game_input_press(640, 540);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    assert(fabsf(game_seconds_left() - time_saved) < 0.001f);
    assert(fabsf(game_debug_boss_x() - x_saved) < 0.001f);
    game_debug_boss_set_x(86);
    game_tick(0.2f, NULL);
    assert(game_phase() == GAME_LOSE && game_resume_level() == 10);
    for (int r = 1; r <= 3; r++) assert(game_debug_mower_used(r));
    game_input_press(490, 515);              /* retry level 10, full timer */
    assert(game_phase() == GAME_PLAY && game_level() == 10);
    assert(game_seconds_left() == 95.0f && !game_debug_boss_alive());
    game_debug_finish_wave();
    pass_timer();
    assert(game_debug_boss_alive());
    pass_timer();                             /* surviving, not killing, wins */
    assert(game_phase() == GAME_WIN && game_completed_level() == 10);
    assert(game_debug_boss_alive());
    for (int r = 1; r <= 3; r++) assert(game_debug_mower_used(r));

    /* Kill the robot early on a replay: victory still waits for the timer. */
    game_input_press(640, 520);              /* final -> menu */
    assert(game_phase() == GAME_MENU && game_completed_level() == 10);
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len));
    assert(game_completed_level() == 10 && game_resume_level() == 10);
    game_input_press(325, 640);
    assert(game_phase() == GAME_SELECT);
    game_input_press(1070, 470);             /* freely select level 10 */
    assert(game_phase() == GAME_PLAY && game_level() == 10);
    game_debug_finish_wave();
    pass_timer();
    assert(game_debug_boss_alive());
    game_debug_defeat_boss();
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && !game_debug_boss_alive());
    pass_timer();
    assert(game_phase() == GAME_WIN);
    game_input_press(640, 520);
    game_input_press(325, 640);
    game_input_press(190, 270);              /* replay level 1 from selection */
    assert(game_phase() == GAME_PLAY && game_level() == 1);
    assert(game_completed_level() == 10 && game_seconds_left() == 50.0f);
    free(saved);
    free(bad);

    game_init();                             /* untouched cutscene auto-plays */
    game_input_press(640, 540);
    for (int i = 0; i < 12 * 60; i++) game_tick(1.0f / 60, NULL);
    assert(game_phase() == GAME_PLAY && game_level() == 1);

    puts("OK: timed levels, level select, autosave, Kirill's book and garden, robot finale");
    return 0;
}
