/* Desktop screenshots for menu, cutscene, level one and the final robot fight.
 * gcc -O2 -Wall -Isrc src/game.c tools/host_test.c -o host_test -lm
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "game.h"

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
    write_bmp("shots/menu.bmp", GAME_W, GAME_H, fb);

    game_input_press(640, 540);             /* ИГРАТЬ -> intro */
    game_tick(1.5f, fb);
    write_bmp("shots/intro.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 540);             /* Dima's line */
    game_tick(0, fb);
    write_bmp("shots/dima.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 540);             /* Kirill's line */
    game_tick(0, fb);
    write_bmp("shots/kirill.bmp", GAME_W, GAME_H, fb);
    game_input_press(640, 540);             /* first level */
    game_tick(0, fb);
    write_bmp("shots/level1.bmp", GAME_W, GAME_H, fb);

    game_debug_snapshot();                 /* final level, queen in robot */
    for (int i = 0; i < 30; i++) game_tick(0.016f, fb);
    write_bmp("shots/play.bmp", GAME_W, GAME_H, fb);
    return 0;
}
