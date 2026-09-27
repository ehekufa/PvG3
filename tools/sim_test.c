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

/* Match the battle sidebar, the garden header and the 9x5 lawn. */
static int garden_card_x(int type) { return 260 + type * 130 + 60; }
static int battle_card_y(int type) { return 137 + type * 107 + 50; }
static int book_row_y(int type) { return 160 + type * 99 + 44; }
static int cell_x(int col) { return 250 + col * 114 + 57; }
static int cell_y(int row) { return 120 + row * 112 + 56; }
#define LEGACY_SAVE_LEN 9988u /* V1-V4 prefix in the V5 export */
static float migrated_x(float x) { return 250 + (x - 120) * 114 / 120; }
static float migrated_y(float y) { return 120 + (y - 150) * 112 / 108; }

static void seed_at(int type, int row, int col) {
    game_input_press(125, battle_card_y(type));
    game_input_press(cell_x(col), cell_y(row));
}

static void garden_at(int type, int row, int col) {
    game_input_press(garden_card_x(type), 70);
    game_input_press(cell_x(col), cell_y(row));
}

static void advance(float seconds) {
    while (seconds > 0.0001f && game_phase() == GAME_PLAY) {
        float dt = seconds < 0.05f ? seconds : 0.05f;
        game_tick(dt, NULL);
        seconds -= dt;
    }
}

/* V1-V4 shared a 9988-byte layout. V5 appends a second checksummed section.
 * These fixtures simulate the old binary prefix, including its old checksum. */
static void rehash(uint8_t *bytes, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length - 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    memcpy(bytes + length - 4, &hash, 4);
}
static void make_legacy_save(uint8_t *bytes, size_t length,
                             uint32_t version, uint32_t completed) {
    float old_level_left = 30.0f, old_boss_left = 90.0f;
    assert(length == LEGACY_SAVE_LEN && version >= 1 && version <= 4);
    memcpy(bytes + 4, &version, 4);
    memcpy(bytes + 8, &completed, 4);
    if (version <= 2) {
        memcpy(bytes + 24, &old_level_left, 4);
        memcpy(bytes + 28, &old_boss_left, 4);
    }
    rehash(bytes, length);
}

int main(void) {
    /* PT Sans is a REAL embedded, anti-aliased Cyrillic TrueType font. */
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
    assert(game_wave_remaining() == 8 && game_wave_total() == 8);
    game_input_press(5, 5);
    assert(game_phase() == GAME_MENU);
    game_input_press(630, 80);               /* labeled levels button */
    assert(game_phase() == GAME_SELECT);
    game_input_press(1130, 50);
    assert(game_phase() == GAME_MENU);

    /* The painted map also opens the selector and level 0 remains replayable. */
    game_input_press(235, 100);              /* painted map tile */
    assert(game_phase() == GAME_SELECT);
    game_input_press(640, 600);              /* LEVEL 0: cutscene */
    assert(game_phase() == GAME_INTRO && game_level() == 0);
    game_input_press(1150, 50);              /* skip replay -> selector */
    assert(game_phase() == GAME_SELECT && game_resume_level() == 1);
    game_input_press(1130, 50);
    assert(game_phase() == GAME_MENU);

    /* All FIVE illustrated plants appear in the Garden and interactive book. */
    assert(game_debug_coin_balance() == 250 && game_debug_seed_count() == 5);
    game_input_press(1080, 85);              /* garden */
    assert(game_phase() == GAME_GARDEN);
    garden_at(2, 0, 1);                       /* Kirill's coin sunflower */
    garden_at(0, 2, 2);                       /* peashooter */
    garden_at(1, 3, 4);                       /* walnut */
    garden_at(3, 4, 5);                       /* author's Jumper Fighter */
    garden_at(4, 1, 7);                       /* author's new lily pad */
    assert(game_debug_garden_plant_type(1, 7) == 4);
    assert(game_debug_garden_plant_type(0, 1) == 2);
    assert(game_debug_garden_plant_type(2, 2) == 0);
    assert(game_debug_garden_plant_type(3, 4) == 1);
    assert(game_debug_garden_plant_type(4, 5) == 3);
    assert(game_debug_coin_balance() == 250); /* garden is free */
    game_input_press(1100, 114);             /* eraser, then repaint */
    game_input_press(cell_x(2), cell_y(2));
    assert(game_debug_garden_plant_type(2, 2) == -1);
    garden_at(0, 2, 2);
    game_input_press(985, 50);               /* garden -> book */
    assert(game_phase() == GAME_BOOK);
    game_input_press(320, book_row_y(3));
    assert(game_debug_book_plant() == 3);    /* Jumper's picture and 250 coins */
    game_input_press(320, book_row_y(4));
    assert(game_debug_book_plant() == 4);    /* lily's picture and 25 coins */
    game_input_press(1130, 50);
    assert(game_phase() == GAME_GARDEN);
    game_input_press(1160, 60);
    assert(game_phase() == GAME_MENU);

    /* Preserve the same legacy 45-cell garden file, including plant ID 4. */
    uint8_t garden[GAME_GARDEN_CELLS], bad_garden[GAME_GARDEN_CELLS];
    game_garden_export(garden);
    assert(garden[1 * 9 + 7] == 5 && garden[4 * 9 + 5] == 4);
    game_init();
    assert(game_debug_garden_plant_type(4, 5) == -1);
    assert(game_garden_import(garden));
    assert(game_debug_garden_plant_type(4, 5) == 3);
    assert(game_debug_garden_plant_type(1, 7) == 4);
    memcpy(bad_garden, garden, sizeof garden);
    bad_garden[0] = 255;
    assert(!game_garden_import(bad_garden));
    assert(game_debug_garden_plant_type(4, 5) == 3);
    game_input_press(245, 610);              /* menu -> book -> menu */
    assert(game_phase() == GAME_BOOK);
    game_input_press(320, book_row_y(1));
    assert(game_debug_book_plant() == 1);
    game_input_press(1130, 50);

    /* First Play enters the story (level 0), then Kirill leads into level 1. */
    game_input_press(640, 600);
    assert(game_phase() == GAME_INTRO && game_level() == 0);
    game_tick(3.6f, NULL);                   /* first scene advances automatically */
    game_input_press(640, 620);              /* Dima -> Kirill */
    game_input_press(640, 620);              /* Kirill -> battle */
    assert(game_phase() == GAME_PLAY && game_level() == 1);

    /* Book pauses the wave. Sunflower coins are picked up, not sky income. */
    assert(game_debug_coin_balance() == 250 && game_debug_coin_count() == 0);
    game_input_press(985, 55);
    game_tick(30.0f, NULL);
    assert(game_phase() == GAME_BOOK && game_wave_remaining() == 8);
    assert(game_debug_first_enemy_type() == -1);
    game_input_press(1130, 50);
    game_tick(9.0f, NULL);
    assert(game_debug_coin_count() == 0 && game_debug_coin_balance() == 250);
    seed_at(4, 0, 2);                       /* lily packet disabled on dry levels */
    assert(game_debug_coin_balance() == 250 && !game_debug_lily_at(0, 2));
    game_input_press(655, 70);              /* old horizontal packet is now HUD */
    game_input_press(cell_x(1), cell_y(0));
    assert(game_debug_plant_type(0, 1) == -1);
    seed_at(2, 0, 1);
    assert(game_debug_plant_type(0, 1) == 2 && game_debug_coin_balance() == 200);
    game_tick(4.9f, NULL);
    assert(game_debug_coin_count() == 0);
    game_tick(0.2f, NULL);
    assert(game_debug_coin_count() == 1);
    game_input_press(cell_x(1) + 26, cell_y(0) - 14);
    assert(game_debug_coin_count() == 0 && game_debug_coin_balance() == 225);
    seed_at(0, 0, 0);
    assert(game_debug_plant_type(0, 0) == 0 && game_debug_coin_balance() == 125);
    seed_at(1, 0, 0);                         /* occupied cell cannot be replaced */
    assert(game_debug_plant_type(0, 0) == 0);
    seed_at(1, 1, 0);
    assert(game_debug_plant_type(1, 0) == 1 && game_debug_coin_balance() == 75);
    assert(game_debug_first_enemy_type() == 0); /* author's yellow duck */

    /* Invalid campaign saves do not mutate state; a real restart restores
     * enemies, coins, plants and the unfinished wave (plus Garden separately). */
    size_t len = game_save_size();
    assert(len == 10044 && len > LEGACY_SAVE_LEN);
    uint8_t *saved = (uint8_t *)malloc(len), *bad = (uint8_t *)malloc(len);
    assert(saved && bad);
    assert(!game_save_export(saved, len - 1));
    assert(game_save_export(saved, len));
    memcpy(bad, saved, len);
    bad[len / 2] ^= 0x80;
    assert(!game_save_import(bad, len) && !game_save_import(saved, len - 1));
    int wave_saved = game_wave_remaining();
    game_init();
    assert(game_garden_import(garden));
    assert(game_save_import(saved, len));
    assert(game_phase() == GAME_MENU && game_resume_level() == 1);
    game_input_press(640, 600);              /* continue, no replay/reset */
    assert(game_phase() == GAME_PLAY && game_wave_remaining() == wave_saved);
    assert(game_debug_coin_balance() == 75 && game_debug_plant_type(0, 1) == 2);
    assert(game_debug_first_enemy_type() == 0);

    /* Rewatching level 0 from the selector cannot erase that saved battle. */
    game_input_press(1180, 55);             /* battle -> menu */
    game_input_press(235, 100);
    game_input_press(640, 600);
    assert(game_phase() == GAME_INTRO && game_level() == 0);
    for (int i = 0; i < 3; i++) game_input_press(640, 620);
    assert(game_phase() == GAME_SELECT && game_completed_level() == 0);
    game_input_press(1130, 50);
    game_input_press(640, 600);
    assert(game_phase() == GAME_PLAY && game_level() == 1);
    assert(game_wave_remaining() == wave_saved && game_debug_plant_type(1, 0) == 1);

    /* Real V2 in-progress layout is still accepted; time no longer decides
     * victory, but previously saved plants/ducks do not disappear on upgrade. */
    memcpy(bad, saved, len);
    make_legacy_save(bad, LEGACY_SAVE_LEN, 2, 0);
    game_init();
    assert(game_save_import(bad, LEGACY_SAVE_LEN));
    game_input_press(640, 600);
    assert(game_phase() == GAME_PLAY && game_wave_remaining() == wave_saved);
    assert(game_debug_plant_type(0, 1) == 2 && game_debug_coin_balance() == 75);

    /* Killing the full wave early IMMEDIATELY clears level 1. */
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_phase() == GAME_LEVEL_CLEAR && game_level() == 1);
    assert(game_completed_level() == 1 && game_resume_level() == 2);
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 600);
    assert(game_phase() == GAME_PLAY && game_level() == 2);
    assert(game_wave_total() == 11 && game_wave_remaining() == 11);
    assert(game_debug_plant_type(0, 0) == -1);

    for (int n = 2; n <= 9; n++) {
        assert(game_level() == n && !game_debug_boss_alive());
        assert(game_wave_total() == 5 + n * 3);
        game_debug_finish_wave();
        game_tick(0, NULL);
        assert(game_phase() == GAME_LEVEL_CLEAR && game_level() == n);
        assert(game_completed_level() == n && game_resume_level() == n + 1);
        game_input_press(10, 10);            /* result isn't tap-anywhere */
        assert(game_phase() == GAME_LEVEL_CLEAR);
        game_input_press(640, 510);
        assert(game_phase() == GAME_PLAY && game_level() == n + 1);
    }

    /* The queen's slow robot arrives ONLY after the tenth duck wave dies. */
    assert(game_level() == 10 && !game_debug_boss_alive());
    assert(game_wave_total() == 35);
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    assert(game_debug_first_enemy_type() == 1);
    float start_x = game_debug_boss_x();
    assert(game_wave_remaining() == 0);
    seed_at(3, 1, 8);                         /* adjacent lane Jumper vs robot */
    assert(game_debug_coin_balance() == 520 - 250);
    assert(game_debug_plant_type(1, 8) == 3);
    game_tick(0.05f, NULL);
    assert(game_debug_plant_type(1, 8) == -1 && game_debug_boss_alive());
    assert(game_debug_boss_x() > start_x + 340); /* three 114px cells */
    float pushed_x = game_debug_boss_x();
    advance(10.0f);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    assert(game_debug_boss_x() < pushed_x - 99 && game_debug_boss_x() > pushed_x - 101);

    /* Save and resume the EXACT boss position; defeat near home still retries. */
    assert(game_save_export(saved, len));
    float x_saved = game_debug_boss_x();
    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 600);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());
    assert(fabsf(game_debug_boss_x() - x_saved) < 0.001f);
    game_debug_boss_set_x(86);
    game_tick(0.2f, NULL);
    assert(game_phase() == GAME_LOSE && game_resume_level() == 10);
    for (int r = 1; r <= 3; r++) assert(game_debug_mower_used(r));
    game_input_press(490, 515);              /* retry level 10 */
    assert(game_phase() == GAME_PLAY && game_level() == 10);
    assert(game_wave_remaining() == 35 && !game_debug_boss_alive());
    game_debug_finish_wave();
    game_tick(0, NULL);
    assert(game_debug_boss_alive());
    advance(10.0f);                          /* still in play while boss lives */
    assert(game_phase() == GAME_PLAY);
    game_debug_defeat_boss();
    game_tick(0, NULL);                      /* boss kill ends game immediately */
    assert(game_phase() == GAME_WIN && game_completed_level() == 10);

    /* V1 highest-level saves and V2 completion masks remain readable. */
    game_input_press(640, 520);
    assert(game_phase() == GAME_MENU && game_save_export(saved, len));
    memcpy(bad, saved, len);
    make_legacy_save(bad, LEGACY_SAVE_LEN, 1, 10);
    game_init();
    assert(game_save_import(bad, LEGACY_SAVE_LEN) && game_completed_level() == 10);
    for (int n = 1; n <= 10; n++) assert(game_debug_level_completed(n));
    memcpy(bad, saved, len);
    make_legacy_save(bad, LEGACY_SAVE_LEN, 2, 1023);
    game_init();
    assert(game_save_import(bad, LEGACY_SAVE_LEN) && game_completed_level() == 10);

    /* A fresh install can pick level 10 directly: only that level is marked. */
    game_init();
    game_input_press(235, 100);
    game_input_press(1070, 470);
    assert(game_phase() == GAME_PLAY && game_level() == 10);
    game_debug_finish_wave(); game_tick(0, NULL);
    assert(game_debug_boss_alive());
    game_debug_defeat_boss(); game_tick(0, NULL);
    assert(game_phase() == GAME_WIN && game_completed_level() == 1);
    assert(game_debug_level_completed(10) && !game_debug_level_completed(1));
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len) && game_completed_level() == 1);
    assert(game_debug_level_completed(10) && !game_debug_level_completed(1));

    /* One early duck kill is NOT a victory while seven more still need to
     * spawn. Peashooters actually kill; killing the final duck clears at once. */
    game_init();
    game_input_press(235, 100); game_input_press(190, 270);
    seed_at(0, 2, 0);
    game_debug_spawn_duck(2, 500);
    advance(10.0f);
    assert(game_phase() == GAME_PLAY && game_debug_duck_x(2) == -1);
    assert(game_wave_remaining() == 7); /* no one is alive, seven not spawned */
    game_debug_finish_wave(); game_tick(0, NULL);
    assert(game_phase() == GAME_LEVEL_CLEAR);

    /* Even with spawning finished, level 10's boss waits for the LAST duck. */
    game_init();
    game_input_press(235, 100); game_input_press(1070, 470);
    game_debug_finish_wave();
    game_debug_spawn_duck(2, 900);
    game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && !game_debug_boss_alive());
    game_debug_finish_wave(); game_tick(0, NULL);
    assert(game_phase() == GAME_PLAY && game_debug_boss_alive());

    /* A mower still sweeps the first lawn cell after the sidebar moves. */
    game_init();
    game_input_press(235, 100);
    game_input_press(190, 270);
    game_debug_spawn_duck(1, 310);
    game_tick(0.05f, NULL);
    assert(game_debug_mower_used(1) && game_debug_duck_x(1) == -1);
    assert(game_phase() == GAME_PLAY && game_wave_remaining() == 7);
    float old_sweeping_mower = game_debug_mower_x(1);
    assert(game_save_export(saved, len));
    memcpy(bad, saved, len);
    make_legacy_save(bad, LEGACY_SAVE_LEN, 3, 0);
    game_init();
    assert(game_save_import(bad, LEGACY_SAVE_LEN));
    game_input_press(640, 600);
    assert(game_debug_mower_used(1));
    assert(fabsf(game_debug_mower_x(1) - migrated_x(old_sweeping_mower)) < 0.01f);

    /* The 250-coin Jumper works against a single drawn duck. It vanishes on
     * impact and knocks that duck back exactly three cells, without a kill. */
    game_init();
    game_input_press(235, 100);
    game_input_press(190, 270);              /* choose level 1, no intro */
    assert(game_phase() == GAME_PLAY && game_debug_coin_balance() == 250);
    seed_at(3, 2, 5);
    assert(game_debug_coin_balance() == 0 && game_debug_plant_type(2, 5) == 3);
    assert(game_debug_cooldown(3) > 0);
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 600);
    assert(game_debug_plant_type(2, 5) == 3 && game_debug_coin_balance() == 0);
    assert(game_debug_cooldown(3) > 0);
    game_debug_spawn_duck(2, (float)cell_x(5) + 24);
    assert(game_wave_remaining() == 8);
    game_tick(0.05f, NULL);
    assert(game_debug_plant_type(2, 5) == -1);
    assert(game_debug_duck_x(2) > cell_x(5) + 340);
    assert(game_wave_remaining() == 8 && game_phase() == GAME_PLAY);
    float duck_x = game_debug_duck_x(2);
    game_tick(0.05f, NULL);
    assert(game_debug_duck_x(2) < duck_x && game_debug_duck_x(2) > duck_x - 2);

    /* V3 snapshots keep the old prefix but use the ORIGINAL lawn geometry.
     * Migrate every world-space object, then re-export V5: it must not move
     * again the next time the player resumes the same battle. */
    game_debug_snapshot();
    float old_boss_x = game_debug_boss_x();
    float old_duck_x = game_debug_duck_x(4);
    float old_pea_x = game_debug_first_pea_x();
    float old_pea_y = game_debug_first_pea_y();
    float old_coin_x = game_debug_first_coin_x();
    float old_coin_y = game_debug_first_coin_y();
    float old_coin_target = game_debug_first_coin_target_y();
    float old_mower_x = game_debug_mower_x(0);
    assert(old_pea_x > 0 && old_coin_x > 0 && old_duck_x > 0);
    assert(game_save_export(saved, len));
    memcpy(bad, saved, len);
    make_legacy_save(bad, LEGACY_SAVE_LEN, 3, 0);
    game_init();
    assert(game_save_import(bad, LEGACY_SAVE_LEN));
    game_input_press(640, 600);
    assert(game_phase() == GAME_PLAY && game_level() == 10);
    assert(fabsf(game_debug_boss_x() - migrated_x(old_boss_x)) < 0.01f);
    assert(fabsf(game_debug_duck_x(4) - migrated_x(old_duck_x)) < 0.01f);
    assert(fabsf(game_debug_first_pea_x() - migrated_x(old_pea_x)) < 0.01f);
    assert(fabsf(game_debug_first_pea_y() - migrated_y(old_pea_y)) < 0.01f);
    assert(fabsf(game_debug_first_coin_x() - migrated_x(old_coin_x)) < 0.01f);
    assert(fabsf(game_debug_first_coin_y() - migrated_y(old_coin_y)) < 0.01f);
    assert(fabsf(game_debug_first_coin_target_y() - migrated_y(old_coin_target)) < 0.01f);
    assert(old_mower_x == 280); /* unused mower now follows the new edge */
    assert(fabsf(game_debug_mower_x(0) - old_mower_x) < 0.01f);
    assert(game_debug_plant_type(2, 1) == 0 && game_debug_coin_balance() == 420);
    assert(game_save_export(saved, len));
    uint32_t version;
    memcpy(&version, saved + 4, sizeof version);
    assert(version == 5);
    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 600);
    assert(fabsf(game_debug_boss_x() - migrated_x(old_boss_x)) < 0.01f);
    assert(fabsf(game_debug_first_coin_x() - migrated_x(old_coin_x)) < 0.01f);

    /* Level 5 uses the author's canal map: rows 2 and 3 (zero-based 1,2)
     * need a lily BEFORE any normal plant. Land still accepts normal plants. */
    game_init();
    game_input_press(235, 100);
    game_input_press(1070, 270);             /* choose level 5 */
    assert(game_phase() == GAME_PLAY && game_level() == 5);
    assert(game_debug_coin_balance() == 370);
    seed_at(0, 1, 2);                         /* bare water refuses pea */
    assert(game_debug_plant_type(1, 2) == -1 && !game_debug_lily_at(1, 2));
    assert(game_debug_coin_balance() == 370 && game_debug_cooldown(0) <= 0);
    seed_at(4, 0, 2);                         /* lily refuses dry land */
    assert(!game_debug_lily_at(0, 2) && game_debug_coin_balance() == 370);
    seed_at(4, 1, 2);                         /* 25-coin support */
    assert(game_debug_lily_at(1, 2) && game_debug_plant_type(1, 2) == -1);
    assert(game_debug_coin_balance() == 345 && game_debug_cooldown(4) > 0);
    seed_at(0, 1, 2);                         /* plant ON the lily */
    assert(game_debug_lily_at(1, 2) && game_debug_plant_type(1, 2) == 0);
    assert(game_debug_coin_balance() == 245);
    seed_at(0, 2, 1);                         /* next water row still bare */
    assert(game_debug_plant_type(2, 1) == -1 && game_debug_coin_balance() == 245);
    seed_at(2, 3, 1);                         /* ordinary grass lane */
    assert(game_debug_plant_type(3, 1) == 2 && game_debug_coin_balance() == 195);
    game_tick(7.6f, NULL);                    /* lily packet becomes ready again */
    seed_at(4, 2, 5);                         /* another lily, with nobody on it */
    assert(game_debug_lily_at(2, 5) && game_debug_plant_type(2, 5) == -1);
    assert(game_debug_coin_balance() == 170 && game_debug_cooldown(4) > 0);
    game_debug_spawn_duck(0, 900);
    assert(game_save_export(saved, len));
    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 600);              /* V5 keeps BOTH water layers */
    assert(game_level() == 5 && game_debug_lily_at(1, 2) && game_debug_lily_at(2, 5));
    assert(game_debug_plant_type(1, 2) == 0 && game_debug_plant_type(3, 1) == 2);
    assert(game_debug_cooldown(4) > 0 && game_debug_coin_balance() == 170);
    assert(fabsf(game_debug_duck_x(0) - 900) < 0.01f);
    memcpy(bad, saved, len);
    bad[LEGACY_SAVE_LEN + 2 * 9 + 5] = 2;   /* checksummed but illegal pad flag */
    rehash(bad, len);
    assert(!game_save_import(bad, len));
    assert(game_debug_lily_at(2, 5) && game_debug_coin_balance() == 170);

    /* An old V4 level-5 game used to be all grass: retain its placed plants
     * and position, granting occupied canal cells supporting pads for free. */
    memcpy(bad, saved, len);
    make_legacy_save(bad, LEGACY_SAVE_LEN, 4, 0);
    game_init();
    assert(game_save_import(bad, LEGACY_SAVE_LEN));
    game_input_press(640, 600);
    assert(game_level() == 5 && game_debug_lily_at(1, 2));
    assert(game_debug_plant_type(1, 2) == 0 && !game_debug_lily_at(2, 5));
    assert(fabsf(game_debug_duck_x(0) - 900) < 0.01f); /* no V4 reprojection */

    game_init();
    assert(game_save_import(saved, len));
    game_input_press(640, 600);
    game_debug_spawn_duck(2, (float)cell_x(5) + 20);
    game_tick(0.05f, NULL);                  /* exposed lily can be knocked away */
    assert(!game_debug_lily_at(2, 5) && game_debug_plant_type(1, 2) == 0);
    game_debug_finish_wave(); game_tick(0, NULL);
    assert(game_phase() == GAME_LEVEL_CLEAR && game_level() == 5);
    game_input_press(640, 510);
    assert(game_phase() == GAME_PLAY && game_level() == 6);
    seed_at(4, 1, 2);                         /* lily only works on level 5 */
    assert(!game_debug_lily_at(1, 2));
    seed_at(0, 1, 2);                         /* water becomes ordinary grass */
    assert(game_debug_plant_type(1, 2) == 0);
    free(saved);
    free(bad);

    game_init();                             /* untouched cutscene auto-plays */
    game_input_press(640, 600);
    for (int i = 0; i < 12 * 60; i++) game_tick(1.0f / 60, NULL);
    assert(game_phase() == GAME_PLAY && game_level() == 1);

    puts("OK: water level and lilies, five drawn plants, V1-V5 saves, waves, boss and Cyrillic font");
    return 0;
}
