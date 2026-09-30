/* ОГОРОД: изображения. Переиспользует упакованные спрайты оригинальной
 * игры (src/sprites_data.h, формат RLE из tools/pack_sprites.py) и умеет
 * сохранять кадр в BMP. */
#ifndef OG_IMAGE_H
#define OG_IMAGE_H

#include <stdint.h>

typedef struct {
    const char *name;
    int w, h;
    const uint32_t *px; /* RGBA8, распаковано один раз */
} OgImage;

int og_image_count(void);
const OgImage *og_image_get(int id);
const OgImage *og_image_find(const char *name); /* NULL, если нет */
int og_image_index_by_name(const char *name);   /* -1, если нет */

/* Рисование с масштабированием (ближайший сосед), отражением и альфой.
 * sx, sy, sw, sh — исходная область картинки (sw/sh <= 0 — вся). */
void og_blit(uint32_t *fb, int fbw, int fbh, const OgImage *img,
             int x, int y, int w, int h, int flip, int alpha,
             int sx, int sy, int sw, int sh);

void og_fill(uint32_t *fb, int fbw, int fbh,
             int x, int y, int w, int h, uint32_t color);
void og_circle(uint32_t *fb, int fbw, int fbh,
               int cx, int cy, int r, uint32_t color, int outline_only);

int og_write_bmp(const char *path, const uint32_t *fb, int w, int h);
int og_write_bmp_scaled(const char *path, const uint32_t *fb,
                        int w, int h, int scale);

#endif /* OG_IMAGE_H */
