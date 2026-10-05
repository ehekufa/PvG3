/* Desktop screenshots: menu, book, Zen Garden, canal/lily, waves and robot.
 * clang -O2 -Wall -Wextra -Werror -Isrc src/game.c src/font.c src/preferences.c \
 *     tools/host_test.c -o host_test -lm && ./host_test
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "game.h"
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

static void assert_frame_grayscale(const uint32_t *pixels, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        unsigned red = pixels[i] & 255u;
        unsigned green = (pixels[i] >> 8) & 255u;
        unsigned blue = (pixels[i] >> 16) & 255u;
        assert(red == green && green == blue);
    }
}

static void test_frame_grayscale_filter(void) {
    uint32_t pixels[] = {
        0xff0000ffu, /* red in RGBA byte order */
        0xff00ff00u, /* green */
        0xffff0000u, /* blue */
        0x80202020u  /* gray with non-opaque alpha */
    };
    game_frame_apply_grayscale(pixels, sizeof pixels / sizeof pixels[0]);
    assert(pixels[0] == 0xff4d4d4du);
    assert(pixels[1] == 0xff959595u);
    assert(pixels[2] == 0xff1d1d1du);
    assert(pixels[3] == 0x80202020u);
    assert_frame_grayscale(pixels, sizeof pixels / sizeof pixels[0]);
}

static void test_blue_player_frame_grayscale(void) {
    static uint32_t frame[GAME_W * GAME_H];
    OnPublishedLevel level = {0};
    level.width = 16;level.height = 10;level.object_count = 3;
    level.objects[0] = (OnLevelObject){.id=1,.type=ON_LEVEL_GROUND,
        .x=0,.y=8,.w=16,.h=2,.visible=1,.color=0xffffffu};
    level.objects[1] = (OnLevelObject){.id=2,.type=ON_LEVEL_PLAYER,
        .x=2,.y=7,.w=.65f,.h=.85f,.visible=1,.color=0x0000ffu};
    level.objects[2] = (OnLevelObject){.id=3,.type=ON_LEVEL_GOAL,
        .x=14,.y=6,.w=1,.h=2,.visible=1,.color=0xffffffu};
    game_init();game_workshop_open();game_workshop_open_details();
    game_workshop_open_editor();
    assert(game_workshop_preview(&level));
    game_tick(0, frame);
    size_t colored_pixels = 0;
    for (size_t i = 0; i < (size_t)GAME_W * GAME_H; ++i) {
        unsigned red = frame[i] & 255u;
        unsigned green = (frame[i] >> 8) & 255u;
        unsigned blue = (frame[i] >> 16) & 255u;
        colored_pixels += red != green || green != blue;
    }
    assert(colored_pixels == 0); /* including the blue-tinted player */
    assert_frame_grayscale(frame, (size_t)GAME_W * GAME_H);
    game_custom_level_exit();
}

static void test_game_preferences(void) {
    const char *path = "pvg3-settings-test.preference";
    if (remove(path) != 0) assert(errno == ENOENT);
    preferences_set_path(path);
    assert(preferences_music_enabled());
    assert(preferences_neutral_background_enabled());
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
    game_init();
    assert(preferences_neutral_background_enabled());
    game_input_press(630, 80);
    game_input_press(190, 260);
    assert(game_phase() == GAME_PLAY);
    game_tick(0, frame);
    const uint32_t neutral = 0xff4a4a4au;
    assert(frame[400 * GAME_W + 1000] == neutral);
    preferences_set_neutral_background_enabled(0);
    game_tick(0, frame);
    assert(frame[400 * GAME_W + 1000] != neutral);
    preferences_set_neutral_background_enabled(1);
    game_tick(0, frame);
    assert(frame[400 * GAME_W + 1000] == neutral);
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
    test_frame_grayscale_filter();
    test_language_preference();
    test_game_preferences();
    assert(preferences_neutral_background_enabled());
    if (mkdir("shots", 0755) != 0 && errno != EEXIST) {
        perror("shots");
        return 1;
    }
    static uint32_t fb[GAME_W * GAME_H];
    game_init();
    game_tick(0.016f, fb);
    assert(game_phase() == GAME_MENU);
    /* A neutral gray background replaces the old menu's Queen robot. */
    assert(fb[365 * GAME_W + 1165] == 0xFFAEAEAEu);
    assert(fb[50 * GAME_W + 100] == 0xFFFFFFFFu); /* player-level tile restored */
    assert(fb[50 * GAME_W + 480] == 0xFFFFFFFFu); /* campaign is white */
    assert(fb[70 * GAME_W + 900] == 0xFFFFFFFFu); /* no Zen Garden picture */
    assert(fb[580 * GAME_W + 105] == 0xFFFFFFFFu); /* book is white */
    assert(fb[560 * GAME_W + 450] == 0xFFFFFFFFu); /* start is white */
    assert(fb[580 * GAME_W + 900] == 0xFFFFFFFFu); /* online is white */
    assert(fb[538 * GAME_W + 1058] == 0xff999999u); /* no level-count caption */
    assert(fb[689 * GAME_W + 640] == 0xff999999u); /* no autosave footer */
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
    /* The book spread uses contrasting white and light-gray paper. */
    assert(fb[350 * GAME_W + 125] == 0xFFFFFFFFu);
    assert(fb[350 * GAME_W + 1155] == 0xFFE1E1E1u);
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
    assert(fb[35 * GAME_W + 930] == 0xFFFFFFFFu); /* shop's book button is white */
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
    assert(((land >> 16) & 255u) > ((water >> 16) & 255u) + 20u);
    write_bmp("shots/water_empty.bmp", GAME_W, GAME_H, fb);
    game_input_press(125, 615);            /* lily packet */
    game_input_press(535, 288);            /* water row 2, column 3 */
    game_input_press(125, 187);            /* peashooter packet */
    game_input_press(535, 288);            /* peashooter on lily */
    game_tick(0, fb);
    write_bmp("shots/water_planted.bmp", GAME_W, GAME_H, fb);
    preferences_set_neutral_background_enabled(1);
    test_blue_player_frame_grayscale();
    return 0;
}
