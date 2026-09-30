/* ОГОРОД: рапаковка спрайтов из RLE-данных оригинальной игры и
 * примитивы программного рендерера. Формат данных — из
 * tools/pack_sprites.py: слово = (длина серии << 32) | RGBA8. */
#include "og_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h; const uint64_t *runs; unsigned nruns; } SpritePacked;
#include "sprites_data.h"

static uint32_t *decoded[SPR_COUNT];
static int decoded_ok[SPR_COUNT];
static int images_ready;

static const char *SPR_NAMES[SPR_COUNT] = {
    "khlebushek", "mask", "kirill", "duck", "robot", "pea", "wall",
    "sunflower", "jumper", "lily", "map", "water", "mower"
};

static void og_image_init(void) {
    if (images_ready) return;
    images_ready = 1;
    for (int i = 0; i < SPR_COUNT; i++) {
        const SpritePacked *sp = &SPRITE_DATA[i];
        size_t n = (size_t)sp->w * sp->h, at = 0;
        uint32_t *px = (uint32_t *)malloc(n * sizeof(uint32_t));
        if (!px) continue;
        for (unsigned j = 0; j < sp->nruns; j++) {
            uint64_t run = sp->runs[j];
            size_t count = (size_t)(run >> 32);
            if (count > n - at) break;
            uint32_t color = (uint32_t)run;
            for (size_t k = 0; k < count; k++) px[at++] = color;
        }
        if (at == n) {
            decoded[i] = px;
            decoded_ok[i] = 1;
        } else {
            free(px);
        }
    }
}

int og_image_count(void) { og_image_init(); return SPR_COUNT; }

const OgImage *og_image_get(int id) {
    static OgImage img;
    og_image_init();
    if (id < 0 || id >= SPR_COUNT || !decoded_ok[id]) return NULL;
    img.name = SPR_NAMES[id];
    img.w = SPRITE_DATA[id].w;
    img.h = SPRITE_DATA[id].h;
    img.px = decoded[id];
    return &img;
}

int og_image_index_by_name(const char *name) {
    og_image_init();
    if (!name) return -1;
    for (int i = 0; i < SPR_COUNT; i++)
        if (strcmp(SPR_NAMES[i], name) == 0) return i;
    return -1;
}

const OgImage *og_image_find(const char *name) {
    int i = og_image_index_by_name(name);
    return i >= 0 ? og_image_get(i) : NULL;
}

/* Смешивание как в оригинальном рендерере игры. */
static uint32_t blend_px(uint32_t dst, uint32_t src, int alpha) {
    int inv = 255 - alpha;
    int r = ((src >> 16) & 255) * alpha + ((dst >> 16) & 255) * inv;
    int g = ((src >> 8) & 255) * alpha + ((dst >> 8) & 255) * inv;
    int b = (src & 255) * alpha + (dst & 255) * inv;
    return 0xFF000000u | (uint32_t)((r / 255) << 16) |
           (uint32_t)((g / 255) << 8) | (uint32_t)(b / 255);
}

void og_blit(uint32_t *fb, int fbw, int fbh, const OgImage *img,
             int x, int y, int w, int h, int flip, int alpha,
             int sx, int sy, int sw, int sh) {
    if (!img || !img->px || w <= 0 || h <= 0 || alpha <= 0) return;
    if (sw <= 0 || sh <= 0) { sx = 0; sy = 0; sw = img->w; sh = img->h; }
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx + sw > img->w) sw = img->w - sx;
    if (sy + sh > img->h) sh = img->h - sy;
    if (sw <= 0 || sh <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fbw ? fbw : x + w;
    int y1 = y + h > fbh ? fbh : y + h;
    for (int dy = y0; dy < y1; dy++) {
        int srcy = sy + (dy - y) * sh / h;
        if (srcy < sy || srcy >= sy + sh) continue;
        const uint32_t *src = img->px + srcy * img->w;
        uint32_t *dst = fb + dy * fbw;
        for (int dx = x0; dx < x1; dx++) {
            int srcx = sx + (dx - x) * sw / w;
            if (flip) srcx = sx + sw - 1 - (dx - x) * sw / w;
            if (srcx < sx || srcx >= sx + sw) continue;
            uint32_t c = src[srcx];
            int a = ((int)(c >> 24) * alpha) / 255;
            if (a == 255) dst[dx] = c;
            else if (a > 0) dst[dx] = blend_px(dst[dx], c, a);
        }
    }
}

void og_fill(uint32_t *fb, int fbw, int fbh,
             int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fbw ? fbw : x + w;
    int y1 = y + h > fbh ? fbh : y + h;
    int alpha = (int)(color >> 24);
    for (int dy = y0; dy < y1; dy++) {
        uint32_t *dst = fb + dy * fbw;
        if (alpha == 255) {
            for (int dx = x0; dx < x1; dx++) dst[dx] = color;
        } else if (alpha > 0) {
            for (int dx = x0; dx < x1; dx++) dst[dx] = blend_px(dst[dx], color, alpha);
        }
    }
}

void og_circle(uint32_t *fb, int fbw, int fbh,
               int cx, int cy, int r, uint32_t color, int outline_only) {
    if (r <= 0) return;
    int alpha = (int)(color >> 24);
    if (alpha <= 0) return;
    int y0 = cy - r < 0 ? 0 : cy - r;
    int y1 = cy + r > fbh ? fbh : cy + r + 1;
    int x0 = cx - r < 0 ? 0 : cx - r;
    int x1 = cx + r > fbw ? fbw : cx + r + 1;
    long r2 = (long)r * r;
    long inner2 = (long)(r - 2 > 0 ? r - 2 : 0) * (r - 2 > 0 ? r - 2 : 0);
    for (int y = y0; y < y1; y++) {
        uint32_t *dst = fb + y * fbw;
        long dy = y - cy;
        for (int x = x0; x < x1; x++) {
            long dx = x - cx;
            long d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            if (outline_only && d2 < inner2) continue;
            if (alpha == 255) dst[x] = color;
            else dst[x] = blend_px(dst[x], color, alpha);
        }
    }
}

int og_write_bmp(const char *path, const uint32_t *fb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int row = w * 3, imgsize = row * h, filesize = 54 + imgsize;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char)(filesize & 255);
    hdr[3] = (unsigned char)((filesize >> 8) & 255);
    hdr[4] = (unsigned char)((filesize >> 16) & 255);
    hdr[5] = (unsigned char)((filesize >> 24) & 255);
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = (unsigned char)(w & 255);
    hdr[19] = (unsigned char)((w >> 8) & 255);
    hdr[22] = (unsigned char)(h & 255);
    hdr[23] = (unsigned char)((h >> 8) & 255);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    unsigned char *buf = (unsigned char *)malloc((size_t)imgsize);
    if (!buf) { fclose(f); return 0; }
    for (int y = 0; y < h; y++) {
        const uint32_t *src = fb + (h - 1 - y) * w; /* BMP снизу вверх */
        unsigned char *dst = buf + (size_t)y * row;
        for (int x = 0; x < w; x++) {
            uint32_t p = src[x];
            dst[x * 3 + 0] = (unsigned char)((p >> 16) & 255);
            dst[x * 3 + 1] = (unsigned char)((p >> 8) & 255);
            dst[x * 3 + 2] = (unsigned char)(p & 255);
        }
    }
    fwrite(buf, 1, (size_t)imgsize, f);
    free(buf);
    fclose(f);
    return 1;
}

int og_write_bmp_scaled(const char *path, const uint32_t *fb,
                        int w, int h, int scale) {
    if (scale <= 1) return og_write_bmp(path, fb, w, h);
    int sw = w / scale, sh = h / scale;
    if (sw <= 0 || sh <= 0) return 0;
    uint32_t *small = (uint32_t *)malloc((size_t)sw * sh * sizeof(uint32_t));
    if (!small) return 0;
    for (int y = 0; y < sh; y++) {
        const uint32_t *src = fb + (size_t)(y * scale) * w;
        for (int x = 0; x < sw; x++) small[y * sw + x] = src[x * scale];
    }
    int ok = og_write_bmp(path, small, sw, sh);
    free(small);
    return ok;
}
