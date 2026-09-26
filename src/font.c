/* Smooth UTF-8 text for both Android and desktop screenshots. PT Sans Regular
 * is OFL-1.1 (assets/fonts/OFL.txt); stb_truetype is public domain / MIT.
 * This file rasterizes each codepoint/size only once and alpha-blends glyphs
 * into the game's software framebuffer. No installed system font is needed.
 */
#include "font.h"

#include <stddef.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#include "font_data.h"

#define MAX_TEXT_SIZE 8
#define GLYPH_SLOTS 224          /* ASCII + Cyrillic U+0400..U+045F */

typedef struct {
    unsigned char *alpha;
    int w, h, xoff, yoff;
    int loaded;
} Glyph;

static stbtt_fontinfo face;
static float scales[MAX_TEXT_SIZE + 1];
static int baselines[MAX_TEXT_SIZE + 1];
static Glyph glyphs[MAX_TEXT_SIZE + 1][GLYPH_SLOTS];
static int initialized, valid;

int font_init(void) {
    if (initialized) return valid;
    initialized = 1;
    if (stbtt_GetFontOffsetForIndex(PT_SANS_TTF, 0) != 0 ||
        !stbtt_InitFont(&face, PT_SANS_TTF, 0)) return 0;
    for (int size = 1; size <= MAX_TEXT_SIZE; size++) {
        /* The cap height of PT Sans at 10*size is close to the old 7*size
         * layout, but with naturally proportioned, anti-aliased glyphs. */
        scales[size] = stbtt_ScaleForPixelHeight(&face, 10.0f * size);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&face, 'H', scales[size], scales[size],
                                    &x0, &y0, &x1, &y1);
        baselines[size] = -y0;
    }
    valid = 1;
    return 1;
}

int font_has_glyph(uint32_t codepoint) {
    return font_init() && stbtt_FindGlyphIndex(&face, (int)codepoint) != 0;
}

static uint32_t next_codepoint(const unsigned char **text) {
    unsigned char first = *(*text)++;
    if (first < 0x80) return first;
    int count = 0;
    uint32_t cp = 0;
    if ((first & 0xe0) == 0xc0) { count = 1; cp = first & 0x1f; }
    else if ((first & 0xf0) == 0xe0) { count = 2; cp = first & 0x0f; }
    else if ((first & 0xf8) == 0xf0) { count = 3; cp = first & 0x07; }
    else return '?';
    for (int i = 0; i < count; i++) {
        unsigned char b = **text;
        if ((b & 0xc0) != 0x80) return '?';
        (*text)++;
        cp = (cp << 6) | (b & 0x3f);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) ||
        (count == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff)) return '?';
    return cp;
}

static uint32_t supported_codepoint(uint32_t cp) {
    if (cp >= 32 && cp <= 126) return cp;
    if (cp >= 0x400 && cp <= 0x45f) return cp;
    return '?';
}

static int glyph_slot(uint32_t cp) {
    if (cp <= 127) return (int)cp;
    return 128 + (int)(cp - 0x400);
}

static int advance_px(int size, uint32_t cp) {
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&face, (int)cp, &advance, &lsb);
    int px = (int)(advance * scales[size] + 0.5f);
    return px > 0 ? px : 1;
}

int font_width(int size, const char *utf8) {
    if (!font_init() || !utf8 || size < 1 || size > MAX_TEXT_SIZE) return 0;
    const unsigned char *p = (const unsigned char *)utf8;
    int width = 0;
    while (*p) width += advance_px(size, supported_codepoint(next_codepoint(&p)));
    return width;
}

static Glyph *rasterize(int size, uint32_t cp) {
    Glyph *g = &glyphs[size][glyph_slot(cp)];
    if (!g->loaded) {
        g->alpha = stbtt_GetCodepointBitmap(&face, scales[size], scales[size],
                                             (int)cp, &g->w, &g->h,
                                             &g->xoff, &g->yoff);
        g->loaded = 1; /* spaces have no bitmap, but still have an advance */
    }
    return g;
}

static uint32_t alpha_blend(uint32_t dst, uint32_t color, int alpha) {
    if (alpha == 255) return color | 0xff000000u;
    int inv = 255 - alpha;
    int r = ((int)(dst & 255) * inv + (int)(color & 255) * alpha) / 255;
    int g = ((int)((dst >> 8) & 255) * inv + (int)((color >> 8) & 255) * alpha) / 255;
    int b = ((int)((dst >> 16) & 255) * inv + (int)((color >> 16) & 255) * alpha) / 255;
    return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

void font_draw(uint32_t *rgba, int width, int height,
               int x, int y, int size, uint32_t color, const char *utf8) {
    if (!font_init() || !rgba || !utf8 || size < 1 || size > MAX_TEXT_SIZE) return;
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        uint32_t cp = supported_codepoint(next_codepoint(&p));
        Glyph *g = rasterize(size, cp);
        int gy = y + baselines[size] + g->yoff;
        int gx = x + g->xoff;
        if (g->alpha) {
            for (int row = 0; row < g->h; row++) {
                int dy = gy + row;
                if ((unsigned)dy >= (unsigned)height) continue;
                for (int col = 0; col < g->w; col++) {
                    int dx = gx + col;
                    if ((unsigned)dx >= (unsigned)width) continue;
                    int a = g->alpha[row * g->w + col];
                    if (a) {
                        uint32_t *pixel = &rgba[dy * width + dx];
                        *pixel = alpha_blend(*pixel, color, a);
                    }
                }
            }
        }
        x += advance_px(size, cp);
    }
}
