/* Desktop screenshots: menu, book, Zen Garden, canal/lily, waves and robot.
 * gcc -O2 -Wall -Wextra -Werror -Isrc src/game.c src/font.c \
 *     tools/host_test.c -o host_test -lm && ./host_test
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "game.h"
#include "online_net.h"

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
    assert(fb[50 * GAME_W + 100] == 0xFFAEAEAEu); /* player-level tile removed */
    assert(fb[50 * GAME_W + 480] == 0xFFFFFFFFu); /* campaign is white */
    assert(fb[70 * GAME_W + 900] == 0xFFFFFFFFu); /* no Zen Garden picture */
    assert(fb[580 * GAME_W + 105] == 0xFFFFFFFFu); /* book is white */
    assert(fb[560 * GAME_W + 450] == 0xFFFFFFFFu); /* start is white */
    assert(fb[580 * GAME_W + 900] == 0xFFFFFFFFu); /* online is white */
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
    return 0;
}
