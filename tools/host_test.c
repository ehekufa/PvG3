/* Desktop screenshots: menu, book, Zen Garden, canal/lily, waves and robot.
 * clang -O2 -Wall -Wextra -Werror -Isrc src/game.c src/font.c src/preferences.c \
 *     tools/host_test.c -o host_test -lm && ./host_test
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "game.h"
#include "game_view.h"
#include "font.h"
#include "preferences.h"
#include "online_net.h"

static void test_language_preference(void) {
    const char *path = "pvg3-language-test.preference";
    if (remove(path) != 0) assert(errno == ENOENT);
    font_set_language(FONT_LANG_RU);
    font_set_language_path(path);
    assert(font_language() == FONT_LANG_RU);
    font_set_language(FONT_LANG_EN);
    FILE *file = fopen(path, "rb");
    assert(file);
    char saved[4] = {0};
    assert(fread(saved, 1, 3, file) == 3);
    assert(fclose(file) == 0);
    assert(saved[0] == 'e' && saved[1] == 'n' && saved[2] == '\n');
    font_set_language_path(path);
    assert(font_language() == FONT_LANG_EN);
    font_set_language(FONT_LANG_RU);
    font_set_language_path(path);
    assert(font_language() == FONT_LANG_RU);
    font_set_language_path(NULL);
    assert(remove(path) == 0);
}

static size_t count_exact_sprite_samples(const uint32_t *frame, int art_id,
                                         int x, int y, int width, int height) {
    int source_width = 0, source_height = 0;
    const uint32_t *source = game_art_rgba(art_id, &source_width, &source_height);
    assert(source && source_width > 0 && source_height > 0);
    size_t matches = 0;
    for (int dy = 0; dy < height; ++dy) {
        int sy = dy * source_height / height;
        for (int dx = 0; dx < width; ++dx) {
            int sx = dx * source_width / width;
            uint32_t source_pixel = source[sy * source_width + sx];
            if ((source_pixel >> 24) != 255u) continue;
            assert(x + dx >= 0 && x + dx < GAME_W);
            assert(y + dy >= 0 && y + dy < GAME_H);
            matches += frame[(y + dy) * GAME_W + x + dx] == source_pixel;
        }
    }
    return matches;
}

static void test_custom_level_keeps_original_player_and_flag_art(void) {
    static uint32_t frame[GAME_W * GAME_H];
    OnPublishedLevel level = {0};
    level.width = 16;level.height = 10;level.object_count = 3;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.color=0xffffffu};
    /* Legacy/custom data may carry any colors; fixed-art objects ignore them. */
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=2,.y=7,.w=.65f,.h=.85f,.visible=1,.color=0x69d16cu};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=4,.y=6,.w=1,.h=2,.visible=1,.color=0x69d16cu};
    game_init();game_workshop_open();game_workshop_open_details();
    game_workshop_open_editor();
    assert(game_workshop_preview(&level));
    game_tick(0, frame);

    float camera_x = 2.0f * 80.0f + .65f * 80.0f * .5f - GAME_W * .40f;
    float camera_y = 7.0f * 72.0f + .85f * 72.0f * .5f - 411.0f;
    int player_x = (int)lrintf(2.0f * 80.0f - camera_x);
    int player_y = (int)lrintf(7.0f * 72.0f - camera_y);
    int flag_x = (int)lrintf(4.0f * 80.0f - camera_x);
    int flag_y = (int)lrintf(6.0f * 72.0f - camera_y);
    size_t original_player_pixels = count_exact_sprite_samples(
        frame, PV_ART_BREAD, player_x, player_y, (int)(.65f * 80.0f),
        (int)(.85f * 72.0f));
    size_t original_flag_pixels = count_exact_sprite_samples(
        frame, PV_ART_LEVEL_FLAG, flag_x, flag_y, 80, 144);
    assert(original_player_pixels > 20);
    assert(original_flag_pixels > 20);
    game_custom_level_exit();
}

static void test_legacy_preferences_migration(void) {
    const char *path = "pvg3-legacy-settings-test.preference";
    remove(path);
    FILE *file = fopen(path, "wb");assert(file);
    assert(fputs("PVG3-PREFERENCES 1\nmusic=0\nneutral_background=1\n",
                 file) >= 0);
    assert(fclose(file) == 0);
    preferences_set_path(path);
    assert(!preferences_music_enabled());
    assert(!preferences_neutral_background_enabled());
    file = fopen(path, "rb");assert(file);
    char saved[96] = {0};
    assert(fgets(saved, sizeof saved, file));
    assert(strcmp(saved, "PVG3-PREFERENCES 3\n") == 0);
    assert(fclose(file) == 0);
    preferences_set_path(NULL);
    assert(remove(path) == 0);
}

/* The remembered account is the nick plus what the database handed out — never
 * the password, and never a hand-edited value. */
static void test_account_preferences(void) {
    const char *path = "pvg3-account-settings-test.preference";
    char login[25], token[65], hash[65];
    remove(path);
    preferences_set_path(path);
    assert(!preferences_account_login(login, sizeof login));
    assert(!preferences_account_admin());
    preferences_set_account("qwertyuiopaj1234",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210", 1);
    assert(preferences_account_login(login, sizeof login));
    assert(strcmp(login, "qwertyuiopaj1234") == 0);
    assert(preferences_account_admin() == 1);
    preferences_set_path(path); /* reload from disk */
    assert(preferences_account_login(login, sizeof login));
    assert(strcmp(login, "qwertyuiopaj1234") == 0);
    assert(preferences_account_token(token, sizeof token));
    assert(strcmp(token,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") == 0);
    assert(preferences_account_hash(hash, sizeof hash));
    assert(preferences_account_admin() == 1);
    preferences_set_account(NULL, NULL, NULL, 0);
    preferences_set_path(path);
    assert(!preferences_account_login(login, sizeof login));
    /* A damaged record simply means «not signed in». */
    FILE *file = fopen(path, "wb");assert(file);
    assert(fputs("PVG3-PREFERENCES 3\naccount_login=bad nick\n"
                 "account_token=zz\naccount_hash=\naccount_admin=1\n", file) >= 0);
    assert(fclose(file) == 0);
    preferences_set_path(path);
    assert(!preferences_account_login(login, sizeof login));
    assert(!preferences_account_admin());
    preferences_set_path(NULL);
    assert(remove(path) == 0);
}

static int pixel_has_color(uint32_t pixel) {
    unsigned red = pixel & 255u;
    unsigned green = (pixel >> 8) & 255u;
    unsigned blue = (pixel >> 16) & 255u;
    unsigned maximum = red > green ? red : green;
    if (blue > maximum) maximum = blue;
    unsigned minimum = red < green ? red : green;
    if (blue < minimum) minimum = blue;
    return maximum - minimum >= 18u;
}

static void test_game_preferences(void) {
    const char *path = "pvg3-settings-test.preference";
    if (remove(path) != 0) assert(errno == ENOENT);
    preferences_set_path(path);
    assert(preferences_music_enabled());
    assert(!preferences_neutral_background_enabled());
    preferences_set_music_enabled(0);
    preferences_set_neutral_background_enabled(0);
    preferences_set_path(path);
    assert(!preferences_music_enabled());
    assert(!preferences_neutral_background_enabled());
    preferences_set_music_enabled(1);
    preferences_set_neutral_background_enabled(1);
    preferences_set_path(path);
    assert(preferences_music_enabled());
    assert(preferences_neutral_background_enabled());
    preferences_set_path(NULL);
    assert(remove(path) == 0);
}

static void test_campaign_background_preference(void) {
    static uint32_t frame[GAME_W * GAME_H];
    preferences_set_neutral_background_enabled(0);
    game_init();
    assert(!preferences_neutral_background_enabled());
    game_input_press(630, 80);
    game_input_press(190, 260);
    assert(game_phase() == GAME_PLAY);
    game_tick(0, frame);
    const uint32_t artwork = frame[400 * GAME_W + 1000];
    assert(pixel_has_color(artwork)); /* original authored map is the default */
    preferences_set_neutral_background_enabled(1);
    game_tick(0, frame);
    const uint32_t plain = frame[400 * GAME_W + 1000];
    assert(plain != artwork && pixel_has_color(plain));
    preferences_set_neutral_background_enabled(0);
    game_tick(0, frame);
    assert(frame[400 * GAME_W + 1000] == artwork);
    game_init();
}

static void write_bmp(const char *path, int w, int h, const uint32_t *rgba) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    int row = w * 3, imgsize = row * h, filesize = 54 + imgsize;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = filesize & 255; hdr[3] = (filesize >> 8) & 255;
    hdr[4] = (filesize >> 16) & 255; hdr[5] = (filesize >> 24) & 255;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = w & 255; hdr[19] = (w >> 8) & 255; hdr[20] = (w >> 16) & 255; hdr[21] = (w >> 24) & 255;
    hdr[22] = h & 255; hdr[23] = (h >> 8) & 255; hdr[24] = (h >> 16) & 255; hdr[25] = (h >> 24) & 255;
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    unsigned char *buf = (unsigned char *)malloc(imgsize);
    if (!buf) { fprintf(stderr, "out of memory writing %s\n", path); fclose(f); return; }
    for (int y = 0; y < h; y++) {
        const uint32_t *src = rgba + (h - 1 - y) * w; /* BMP is bottom-up */
        unsigned char *dst = buf + y * row;
        for (int x = 0; x < w; x++) {
            uint32_t p = src[x];
            dst[x * 3 + 0] = (p >> 16) & 255; /* B */
            dst[x * 3 + 1] = (p >> 8) & 255;  /* G */
            dst[x * 3 + 2] = p & 255;         /* R */
        }
    }
    fwrite(buf, 1, imgsize, f);
    free(buf);
    fclose(f);
    printf("wrote %s\n", path);
}

int main(void) {
    test_language_preference();
    test_legacy_preferences_migration();
    test_account_preferences();
    test_game_preferences();
    preferences_set_neutral_background_enabled(0);
    assert(!preferences_neutral_background_enabled());
    if (mkdir("shots", 0755) != 0 && errno != EEXIST) {
        perror("shots");
        return 1;
    }
    static uint32_t fb[GAME_W * GAME_H];
    game_init();
    game_tick(0.016f, fb);
    assert(game_phase() == GAME_MENU);
    /* The menu sky is a soft blue, and the Queen's robot stays on level 5. */
    assert(fb[365 * GAME_W + 1165] == 0xFFECE0C6u);
    assert(fb[50 * GAME_W + 100] == 0xFFF8FDFFu); /* player-level tile restored */
    assert(fb[50 * GAME_W + 480] == 0xFFF8FDFFu); /* campaign uses warm white */
    assert(fb[70 * GAME_W + 900] == 0xFFF8FDFFu); /* no Zen Garden picture */
    assert(fb[580 * GAME_W + 105] == 0xFFF8FDFFu); /* book uses warm white */
    assert(fb[560 * GAME_W + 450] == 0xFF708EE6u); /* start uses coral */
    assert(fb[580 * GAME_W + 900] == 0xFFF8FDFFu); /* online uses warm white */
    assert(fb[538 * GAME_W + 1058] == 0xffb5d3e0u); /* no level-count caption */
    assert(fb[689 * GAME_W + 640] == 0xffb5d3e0u); /* no autosave footer */
    test_campaign_background_preference();
    game_tick(0.016f, fb);
    write_bmp("shots/menu.bmp", GAME_W, GAME_H, fb);
    game_input_press(1058, 615);            /* native room browser */
    on_net_pump_once();                      /* desktop fake: no rooms */
    game_tick(0, fb);
    assert(game_phase() == GAME_ONLINE_ROOMS);
    write_bmp("shots/online_rooms.bmp", GAME_W, GAME_H, fb);
    game_input_press(785, 180);             /* square opens search */
    game_tick(0, fb);
    write_bmp("shots/online_search.bmp", GAME_W, GAME_H, fb);
    game_input_press(950, 133);
    game_input_press(1140, 55);             /* back to offline menu */
    game_input_press(630, 80);              /* кампанийные уровни */
    game_tick(0, fb);
    write_bmp("shots/select.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 600);             /* level 0: replayable story */
    game_tick(0, fb);
    write_bmp("shots/intro_replay.bmp", GAME_W, GAME_H, fb);
    game_input_press(1150, 50);             /* skip -> selector */
    game_input_press(1130, 50);             /* back to menu */

    preferences_set_neutral_background_enabled(0); /* inspect supplied map art */
    game_input_press(1080, 85);             /* САД ДЗЕН */
    game_input_press(580, 70);              /* Kirill's blue coin sunflower */
    game_input_press(421, 176);
    game_input_press(320, 70);              /* peashooter */
    game_input_press(535, 400);
    game_input_press(450, 70);              /* walnut */
    game_input_press(763, 512);
    game_input_press(710, 70);              /* illustrated Jumper Fighter */
    game_input_press(877, 512);
    game_input_press(840, 70);              /* new two-eyed lily pad */
    game_input_press(991, 288);
    game_tick(0, fb);
    assert(game_phase() == GAME_GARDEN);
    write_bmp("shots/garden.bmp", GAME_W, GAME_H, fb);
    game_input_press(985, 50);              /* book from garden */
    game_input_press(320, 160 + 3 * 99 + 44); /* Jumper's page */
    game_tick(0, fb);
    assert(game_phase() == GAME_BOOK);
    /* The book spread uses warm white and blue-gray paper. */
    assert(fb[350 * GAME_W + 125] == 0xFFF8FDFFu);
    assert(fb[350 * GAME_W + 1155] == 0xFFF2ECE1u);
    write_bmp("shots/book.bmp", GAME_W, GAME_H, fb);
    game_input_press(320, 160 + 2 * 99 + 44); /* sunflower's page */
    game_tick(0, fb);
    write_bmp("shots/book_sunflower.bmp", GAME_W, GAME_H, fb);
    game_input_press(320, 160 + 4 * 99 + 44); /* lily's page */
    game_tick(0, fb);
    write_bmp("shots/book_lily.bmp", GAME_W, GAME_H, fb);
    game_input_press(450, 141);             /* enemy tab */
    game_input_press(320, 160 + 1 * 99 + 44); /* cone duck */
    game_tick(0, fb);
    write_bmp("shots/book_cone.bmp", GAME_W, GAME_H, fb);
    game_input_press(320, 160 + 2 * 99 + 44); /* bucket duck */
    game_tick(0, fb);
    write_bmp("shots/book_bucket.bmp", GAME_W, GAME_H, fb);
    game_input_press(250, 141);             /* back to the plant tab */
    game_input_press(1130, 50);             /* garden */
    game_input_press(1160, 60);             /* menu */

    game_input_press(640, 600);             /* СТАРТ -> intro */
    game_tick(1.5f, fb);
    write_bmp("shots/intro.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 600);             /* Dima's line */
    game_tick(0, fb);
    write_bmp("shots/dima.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 600);             /* Kirill's line */
    game_tick(0, fb);
    write_bmp("shots/kirill.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 600);             /* first level */
    game_tick(0, fb);
    assert(game_phase() == GAME_PLAY);
    assert(fb[35 * GAME_W + 930] == 0xFFF8FDFFu); /* shop's book button uses warm white */
    /* The first playable row starts immediately below the top HUD. */
    assert(fb[125 * GAME_W + 960] != fb[90 * GAME_W + 960]);
    write_bmp("shots/level1.bmp", GAME_W, GAME_H, fb);
    game_input_press(125, 401);             /* buy Kirill's sunflower (left rack) */
    game_input_press(421, 176);
    game_tick(6.3f, fb);                    /* first coin appears beside it */
    write_bmp("shots/coin.bmp", GAME_W, GAME_H, fb);

    game_debug_snapshot();                 /* final level, queen in robot */
    for (int i = 0; i < 30; i++) game_tick(0.016f, fb);
    write_bmp("shots/play.bmp", GAME_W, GAME_H, fb);
    game_debug_armored_snapshot();
    game_tick(0, fb);
    write_bmp("shots/armored_ducks.bmp", GAME_W, GAME_H, fb);

    game_init();
    game_input_press(630, 80);             /* campaign level selector */
    game_input_press(1070, 270);           /* level 5: author's water map */
    assert(game_phase() == GAME_PLAY && game_level() == 5);
    game_tick(0, fb);
    uint32_t water = fb[285 * GAME_W + 700];
    uint32_t land = fb[175 * GAME_W + 700];
    assert(((water >> 16) & 255u) > ((land >> 16) & 255u) + 20u);
    write_bmp("shots/water_empty.bmp", GAME_W, GAME_H, fb);
    game_input_press(125, 615);            /* lily packet */
    game_input_press(535, 288);            /* water row 2, column 3 */
    game_input_press(125, 187);            /* peashooter packet */
    game_input_press(535, 288);            /* peashooter on lily */
    game_tick(0, fb);
    write_bmp("shots/water_planted.bmp", GAME_W, GAME_H, fb);
    preferences_set_neutral_background_enabled(1);
    test_custom_level_keeps_original_player_and_flag_art();
    return 0;
}
