/* host_test.c — compiles game.c on a desktop and writes BMP screenshots so the
 * renderer/logic can be verified without an Android device. Not part of the APK.
 *
 *   gcc -O2 -Wall -Isrc src/game.c tools/host_test.c -o host_test -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
    static uint32_t fb[GAME_W * GAME_H];

    /* Title screen. */
    game_init();
    game_tick(0.016f, fb);
    write_bmp("shots/menu.bmp", GAME_W, GAME_H, fb);

    /* A populated play scene. */
    game_debug_snapshot();
    for (int i = 0; i < 30; i++) game_tick(0.016f, fb);
    write_bmp("shots/play.bmp", GAME_W, GAME_H, fb);

    return 0;
}
