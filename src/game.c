/* game.c — lane defence, platform-independent simulation and software renderer.
 * The original PNG artwork is packed into sprites_data.h at build time (see
 * tools/pack_sprites.py). The bitmap font and a few missing plants are drawn
 * in code; the Android host only blits our RGBA framebuffer.
 */

#include "game.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* framebuffer helpers                                                */
/* ------------------------------------------------------------------ */

static uint32_t *FB;

/* Constant-friendly color macro (usable in static initializers AND runtime). */
#define COL(r,g,b) (0xFF000000u | ((b)<<16) | ((g)<<8) | (r))

static inline void setpix(int x, int y, uint32_t c) {
    if ((unsigned)x < GAME_W && (unsigned)y < GAME_H)
        FB[y * GAME_W + x] = c;
}

static inline uint32_t blend(uint32_t d, uint32_t s, int a) {
    if (a >= 255) return s;
    if (a <= 0)   return d;
    int inv = 255 - a;
    int r = (int)((s & 255) * a + (d & 255) * inv) / 255;
    int g = (int)(((s >> 8) & 255) * a + ((d >> 8) & 255) * inv) / 255;
    int b = (int)(((s >> 16) & 255) * a + ((d >> 16) & 255) * inv) / 255;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

static inline void setpixA(int x, int y, uint32_t c, int a) {
    if ((unsigned)x < GAME_W && (unsigned)y < GAME_H)
        FB[y * GAME_W + x] = blend(FB[y * GAME_W + x], c, a);
}

static void rect(int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GAME_W) x1 = GAME_W - 1;
    if (y1 >= GAME_H) y1 = GAME_H - 1;
    for (int y = y0; y <= y1; y++) {
        uint32_t *row = FB + y * GAME_W;
        for (int x = x0; x <= x1; x++) row[x] = c;
    }
}

static void rect_blend(int x0, int y0, int x1, int y1, uint32_t c, int a) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GAME_W) x1 = GAME_W - 1;
    if (y1 >= GAME_H) y1 = GAME_H - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) setpixA(x, y, c, a);
}

static void disc(int cx, int cy, int r, uint32_t c) {
    if (r <= 0) { setpix(cx, cy, c); return; }
    int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    int r2 = r * r, ri = r - 1, ri2 = ri * ri;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if ((unsigned)x >= GAME_W || (unsigned)y >= GAME_H) continue;
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                if (r > 1 && d2 > ri2) {
                    int a = (int)(255.0f * ((float)r - sqrtf((float)d2)));
                    if (a < 0) a = 0;
                    if (a > 255) a = 255;
                    setpixA(x, y, c, a);
                } else setpix(x, y, c);
            }
        }
}

static void ellipse(int cx, int cy, int rx, int ry, uint32_t c) {
    if (rx <= 0 || ry <= 0) { setpix(cx, cy, c); return; }
    int x0 = cx - rx, x1 = cx + rx, y0 = cy - ry, y1 = cy + ry;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if ((unsigned)x >= GAME_W || (unsigned)y >= GAME_H) continue;
            float dx = (float)(x - cx) / rx, dy = (float)(y - cy) / ry;
            float d2 = dx * dx + dy * dy;
            if (d2 <= 1.0f) {
                if (d2 > 0.85f) {
                    int a = (int)(255.0f * (1.0f - d2) / 0.15f);
                    if (a < 0) a = 0;
                    if (a > 255) a = 255;
                    setpixA(x, y, c, a);
                } else setpix(x, y, c);
            }
        }
}

/* The PNGs are packed as consecutive (count, RGBA) runs at build time. Decode
 * only once, not on every frame; no Android file paths or third-party decoder. */
typedef struct { int w, h; const uint64_t *runs; unsigned nruns; } SpritePacked;
#include "sprites_data.h"
static uint32_t *sprite_pixels[SPR_COUNT];
static int sprites_initialized;

static void init_sprites(void) {
    if (sprites_initialized) return;
    sprites_initialized = 1;
    for (int i = 0; i < SPR_COUNT; i++) {
        const SpritePacked *sp = &SPRITE_DATA[i];
        size_t n = (size_t)sp->w * sp->h, at = 0;
        uint32_t *pixels = (uint32_t *)malloc(n * sizeof(uint32_t));
        if (!pixels) continue;                 /* omit this image on allocation failure */
        for (unsigned j = 0; j < sp->nruns; j++) {
            uint64_t run = sp->runs[j];
            size_t count = (size_t)(run >> 32);
            if (count > n - at) break;
            uint32_t color = (uint32_t)run;
            for (size_t k = 0; k < count; k++) pixels[at++] = color;
        }
        if (at == n) sprite_pixels[i] = pixels;
        else free(pixels);
    }
}

/* Scale (and optionally flip) a rectangular part of the author's image. All
 * destination coordinates are clipped BEFORE sampling, including off-screen
 * enemies, so neither map cropping nor sprite animation reads outside data. */
static void sprite_crop(int id, int x, int y, int w, int h,
                        int sx, int sy, int sw, int sh, int flip) {
    if (id < 0 || id >= SPR_COUNT || !sprite_pixels[id] || w <= 0 || h <= 0 ||
        sw <= 0 || sh <= 0) return;
    const SpritePacked *sp = &SPRITE_DATA[id];
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > GAME_W ? GAME_W : x + w;
    int y1 = y + h > GAME_H ? GAME_H : y + h;
    for (int dy = y0; dy < y1; dy++) {
        int src_y = sy + (dy - y) * sh / h;
        if ((unsigned)src_y >= (unsigned)sp->h) continue;
        const uint32_t *src = sprite_pixels[id] + src_y * sp->w;
        uint32_t *dst = FB + dy * GAME_W;
        for (int dx = x0; dx < x1; dx++) {
            int src_x = sx + (dx - x) * sw / w;
            if (flip) src_x = sx + sw - 1 - (dx - x) * sw / w;
            if ((unsigned)src_x >= (unsigned)sp->w) continue;
            uint32_t color = src[src_x];
            int alpha = color >> 24;
            if (alpha == 255) dst[dx] = color;
            else if (alpha) dst[dx] = blend(dst[dx], color, alpha);
        }
    }
}

static void sprite_draw(int id, int x, int y, int w, int h, int flip) {
    sprite_crop(id, x, y, w, h, 0, 0, SPRITE_DATA[id].w, SPRITE_DATA[id].h, flip);
}

/* ------------------------------------------------------------------ */
/* tiny 5x7 bitmap font                                               */
/* ------------------------------------------------------------------ */

static const uint8_t FNT[][7] = {
    ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D'] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G'] = {0x0F,0x10,0x10,0x13,0x11,0x11,0x0F},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J'] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N'] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W'] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    ['3'] = {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5'] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    [':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    [','] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x08},
    ['?'] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    ['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['/'] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    [' '] = {0,0,0,0,0,0,0},
    ['+'] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
};

/* Cyrillic: А-Я (U+0410..U+042F) + Ё (index 32). Rows top..bottom, MSB = left. */
static const uint8_t FNT_CYR[33][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* А */
    {0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E}, /* Б */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* В */
    {0x1F,0x10,0x10,0x10,0x10,0x10,0x10}, /* Г */
    {0x0E,0x0A,0x0A,0x0A,0x1F,0x15,0x11}, /* Д */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* Е */
    {0x15,0x15,0x0E,0x1F,0x0E,0x15,0x15}, /* Ж */
    {0x1E,0x11,0x01,0x0E,0x01,0x11,0x1E}, /* З */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* И */
    {0x0E,0x00,0x11,0x19,0x15,0x13,0x11}, /* Й */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* К */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x11}, /* Л */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* М */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* Н */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* О */
    {0x1F,0x11,0x11,0x11,0x11,0x11,0x11}, /* П */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* Р */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* С */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* Т */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* У */
    {0x04,0x0E,0x15,0x15,0x15,0x0E,0x04}, /* Ф */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* Х */
    {0x11,0x19,0x15,0x13,0x11,0x1F,0x01}, /* Ц */
    {0x11,0x11,0x11,0x1F,0x01,0x01,0x01}, /* Ч */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x1F}, /* Ш */
    {0x11,0x11,0x11,0x11,0x11,0x1F,0x01}, /* Щ */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* Ъ */
    {0x12,0x12,0x12,0x1E,0x12,0x12,0x1E}, /* Ы */
    {0x10,0x10,0x10,0x1E,0x11,0x11,0x1E}, /* Ь */
    {0x0E,0x11,0x01,0x07,0x01,0x11,0x0E}, /* Э */
    {0x16,0x19,0x19,0x19,0x19,0x19,0x16}, /* Ю */
    {0x0F,0x11,0x11,0x0F,0x01,0x01,0x01}, /* Я */
    {0x0A,0x00,0x1F,0x10,0x1E,0x10,0x1F}, /* Ё */
};

static const uint8_t *glyph_for(uint32_t cp) {
    if (cp == 0x451) cp = 0x401;                    /* ё -> Ё */
    if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;     /* а-я -> А-Я */
    if (cp == 0x401) return FNT_CYR[32];
    if (cp >= 0x410 && cp <= 0x42F) return FNT_CYR[cp - 0x410];
    if (cp >= ' ' && cp <= '~') return FNT[cp];
    return NULL;
}

static void draw_char(int x, int y, int s, uint32_t c, uint32_t cp) {
    const uint8_t *g = glyph_for(cp);
    if (!g) return;
    for (int r = 0; r < 7; r++)
        for (int cc = 0; cc < 5; cc++)
            if (g[r] & (1 << (4 - cc)))
                rect(x + cc * s, y + r * s, x + cc * s + s - 1, y + r * s + s - 1, c);
}

/* Decodes UTF-8 (ASCII + Cyrillic) so Russian story text renders. */
static void draw_text(int x, int y, int s, uint32_t c, const char *str) {
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xC0 && *p) {                     /* 2-byte UTF-8 */
            uint32_t b = *p++;
            cp = ((b & 0xC0) == 0x80) ? (((cp & 0x1F) << 6) | (b & 0x3F)) : '?';
        }
        draw_char(x, y, s, c, cp);
        x += 6 * s;
    }
}

static int text_w(int s, const char *str) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;               /* code points, not bytes */
    return n * 6 * s;
}

static void draw_text_c(int cx, int y, int s, uint32_t c, const char *str) {
    draw_text(cx - text_w(s, str) / 2, y, s, c, str);
}

static void draw_int(int x, int y, int s, uint32_t c, int v) {
    char b[16]; int i = 15; b[i--] = 0;
    if (v < 0) { draw_text(x, y, s, c, "-"); return; }
    if (v == 0) { draw_text(x, y, s, c, "0"); return; }
    while (v > 0 && i >= 0) { b[i--] = (char)('0' + v % 10); v /= 10; }
    draw_text(x, y, s, c, b + i + 1);
}

/* ------------------------------------------------------------------ */
/* RNG                                                                */
/* ------------------------------------------------------------------ */

static uint32_t RNG = 0xC0FFEE11u;
static uint32_t rnd(void) { RNG ^= RNG << 13; RNG ^= RNG >> 17; RNG ^= RNG << 5; return RNG; }
static float rndf(void) { return (float)(rnd() & 0xFFFFFF) / (float)0x1000000; }

/* ------------------------------------------------------------------ */
/* game constants                                                     */
/* ------------------------------------------------------------------ */

#define ROWS 5
#define COLS 9
#define LAWN_X 120
#define LAWN_Y 150
#define CELL_W 120
#define CELL_H 108
#define CARD_X 285
#define CARD_STEP 205
#define CARD_W 170

#define ZMAX 80
#define PEAMAX 200
#define SUNMAX 40
#define PARTMAX 400

typedef enum { PH_MENU = GAME_MENU, PH_INTRO = GAME_INTRO, PH_PLAY = GAME_PLAY,
               PH_LEVEL_CLEAR = GAME_LEVEL_CLEAR, PH_WIN = GAME_WIN,
               PH_LOSE = GAME_LOSE } Phase;

/* Only the three plants the author drew are playable. Other pictures are
 * story characters, the duck enemy, the queen's robot, map and mower. */
enum { PT_NONE = -1, PT_PEA = 0, PT_WALL, PT_SNOW, PT_COUNT };
enum { EN_DUCK = 0, EN_ROBOT };

typedef struct { int cost; int hp; float recharge; uint32_t body; } PlantDef;

static const PlantDef PDEF[PT_COUNT] = {
    [PT_PEA]  = { 100,  300,  7.5f, COL( 70, 170,  70) },
    [PT_WALL] = {  50, 4000, 30.0f, COL(180, 120,  70) },
    [PT_SNOW] = { 175,  300,  7.5f, COL(120, 200, 235) },
};

typedef struct { int type; float hp; float fire_t; float sway; } Plant;
typedef struct { int active; int row; float x; float hp; float maxhp; int type;
                 float speed; int eating; float slow; float anim; } Zombie;
typedef struct { int active; int row; float x; float y; int dmg; int snow; } Pea;
typedef struct { int active; float x; float y; float vy; float target_y; float life; float bob; } Sun;
typedef struct { int active; float x, y, vx, vy; float life; float maxlife; uint32_t col; } Part;

static Phase phase;
static int level;                     /* 1..10, advance only after clearing a wave */
static int intro_step;                /* crying, Dima speaks, Kirill speaks */
static float intro_t;
static int sun_res;
static int selected;
static float cooldown[PT_COUNT];
static float sky_sun_t;
static float spawn_t;
static int to_spawn;
static int total_zombies;
static float banner_t;
static float global_t;
static int boss_spawned;
static const char *banner_text;

static const char *LEVEL_NAMES[10] = {
    "ПЕРВАЯ ЗАЩИТА", "НОВАЯ ВОЛНА", "У ЗАБОРА", "ВСТРЕЧАЙ УТОК",
    "СЕРЕДИНА ПУТИ", "СЛОЖНЕЕ И СЛОЖНЕЕ", "ДЕРЖИ ОБОРОНУ",
    "ПОСЛЕДНИЙ РУБЕЖ", "ПЕРЕД БУРЕЙ", "КОРОЛЕВА БЛИЗКО"
};

static Plant grid[ROWS][COLS];
static Zombie zomb[ZMAX];
static Pea peas[PEAMAX];
static Sun suns[SUNMAX];
static Part parts[PARTMAX];
static struct { int used; int running; float x; } mower[ROWS];

#define CELL_CX(c) (LAWN_X + (c) * CELL_W + CELL_W / 2)
#define CELL_CY(r) (LAWN_Y + (r) * CELL_H + CELL_H / 2)

/* ------------------------------------------------------------------ */
/* particles                                                          */
/* ------------------------------------------------------------------ */

static void burst(float x, float y, int n, uint32_t col, float spd) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < PARTMAX; j++)
            if (!parts[j].active) {
                parts[j].active = 1;
                parts[j].x = x; parts[j].y = y;
                float a = rndf() * 6.283f, v = (0.4f + rndf()) * spd;
                parts[j].vx = cosf(a) * v; parts[j].vy = sinf(a) * v - 30;
                parts[j].life = parts[j].maxlife = 0.4f + rndf() * 0.4f;
                parts[j].col = col;
                break;
            }
}

static void update_parts(float dt) {
    for (int i = 0; i < PARTMAX; i++) {
        Part *p = &parts[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0) { p->active = 0; continue; }
        p->x += p->vx * dt; p->y += p->vy * dt; p->vy += 240 * dt;
    }
}

static void draw_parts(void) {
    for (int i = 0; i < PARTMAX; i++) {
        Part *p = &parts[i];
        if (!p->active) continue;
        int a = (int)(255 * p->life / p->maxlife);
        disc((int)p->x, (int)p->y, 3, p->col);
        (void)a;
    }
}

/* ------------------------------------------------------------------ */
/* drawing: entities                                                  */
/* ------------------------------------------------------------------ */

static void draw_sun_icon(int cx, int cy, int r) {
    for (int k = 4; k > 0; k--) ellipse(cx, cy, r + k * 3, r + k * 3, COL(255, 230, 80));
    disc(cx, cy, r, COL(255, 240, 120));
    disc(cx, cy, r - r / 4, COL(255, 250, 200));
}

static void draw_plant(int cx, int cy, int type, float sway) {
    if (type < 0 || type >= PT_COUNT) return;
    const int image[PT_COUNT] = { SPR_PEA, SPR_WALL, SPR_SNOW };
    /* Each selectable plant is the author's own picture; no substitutes. */
    ellipse(cx, cy + 32, 34, 7, COL(47, 112, 30));
    sprite_draw(image[type], cx - 43, cy - 48 + (int)(sway * 2), 86, 86, 0);
}

/* ------------------------------------------------------------------ */
/* author's duck-zombie and the Duck Queen's piloted robot            */
/* ------------------------------------------------------------------ */

static void draw_enemy(const Zombie *z) {
    int x = (int)z->x, y = CELL_CY(z->row);
    if (z->type == EN_ROBOT) {
        ellipse(x, y + 66, 75, 12, COL(38, 85, 28));
        sprite_draw(SPR_ROBOT, x - 95, y - 122 + (int)(sinf(z->anim) * 2), 190, 190, 0);
    } else {
        ellipse(x, y + 36, 40, 8, COL(48, 120, 34));
        /* The duck faces right in the PNG: turn it towards the house. */
        sprite_draw(SPR_DUCK, x - 46, y - 50 + (int)(sinf(z->anim) * 2), 92, 92, 1);
        if (z->slow > 0)
            rect_blend(x - 40, y - 40, x + 40, y + 30, COL(150, 215, 255), 50);
    }
}

/* ------------------------------------------------------------------ */
/* spawning                                                           */
/* ------------------------------------------------------------------ */

static int spawn_zombie(void) {
    for (int i = 0; i < ZMAX; i++)
        if (!zomb[i].active) {
            Zombie *z = &zomb[i];
            float prog = total_zombies ? 1.0f - (float)to_spawn / total_zombies : 0;
            z->active = 1;
            z->row = (int)(rndf() * ROWS);
            z->x = GAME_W + 30 + rndf() * 60;
            z->type = EN_DUCK; /* one enemy species, the author's yellow duck */
            z->hp = z->maxhp = (180 + 35 * (level - 1)) * (1.0f + 0.25f * prog);
            z->speed = 22 + level * 0.7f + rndf() * 5;
            z->eating = 0; z->slow = 0; z->anim = rndf() * 6.28f;
            return 1;
        }
    return 0;
}

/* Only spawned after the *whole* tenth wave is defeated. The queen rides the
 * author's giant robot and threatens its own lane and both adjacent lanes. */
static int spawn_boss(void) {
    for (int i = 0; i < ZMAX; i++)
        if (!zomb[i].active) {
            Zombie *z = &zomb[i];
            z->active = 1;
            z->row = 2;
            z->x = GAME_W + 90;
            z->type = EN_ROBOT;
            z->hp = z->maxhp = 5500;
            z->speed = 10;             /* much slower than the ordinary ducks */
            z->eating = 0; z->slow = 0; z->anim = 0;
            return 1;
        }
    return 0;
}

static void spawn_sun(float x, float y) {
    for (int i = 0; i < SUNMAX; i++)
        if (!suns[i].active) {
            Sun *s = &suns[i];
            s->active = 1; s->x = x; s->y = y; s->bob = rndf() * 6.28f;
            s->vy = 80; s->target_y = 220 + rndf() * 400;
            s->life = 16.0f; /* enough time to land and collect without a sunflower */
            return;
        }
}

static void spawn_pea(int row, int x, int dmg, int snow) {
    for (int i = 0; i < PEAMAX; i++)
        if (!peas[i].active) {
            peas[i].active = 1; peas[i].row = row;
            peas[i].x = (float)x; peas[i].y = CELL_CY(row) - 6;
            peas[i].dmg = dmg; peas[i].snow = snow;
            return;
        }
}

/* ------------------------------------------------------------------ */
/* reset                                                              */
/* ------------------------------------------------------------------ */

static void start_level(int n) {
    level = n;
    memset(grid, 0, sizeof(grid));
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) grid[r][c].type = PT_NONE;
    memset(zomb, 0, sizeof(zomb));
    memset(peas, 0, sizeof(peas));
    memset(suns, 0, sizeof(suns));
    memset(parts, 0, sizeof(parts));
    for (int r = 0; r < ROWS; r++) { mower[r].used = 0; mower[r].running = 0; mower[r].x = LAWN_X - 30; }
    for (int i = 0; i < PT_COUNT; i++) cooldown[i] = 0;
    /* No undrawn sunflower: starting sun plus regular sky drops fund seeds. */
    sun_res = 250 + (n - 1) * 30;
    selected = -1;
    sky_sun_t = 3.0f;
    total_zombies = 5 + n * 3;
    to_spawn = total_zombies;
    spawn_t = 11.0f - n * 0.2f;
    banner_t = 4;
    boss_spawned = 0;
    banner_text = LEVEL_NAMES[n - 1];
    phase = PH_PLAY;
}

void game_init(void) {
    init_sprites();
    RNG = 0xC0FFEE11u;
    global_t = 0;
    intro_step = 0;
    intro_t = 0;
    start_level(1);
    phase = PH_MENU;
}

void game_debug_snapshot(void) {
    game_init();
    start_level(10);
    /* A populated scene showing ONLY authored plants, duck and robot. */
    sun_res = 420;
    grid[2][1].type = PT_PEA;  grid[2][1].hp = PDEF[PT_PEA].hp;
    grid[2][2].type = PT_PEA;  grid[2][2].hp = PDEF[PT_PEA].hp;
    grid[1][2].type = PT_PEA;  grid[1][2].hp = PDEF[PT_PEA].hp;
    grid[3][2].type = PT_WALL; grid[3][2].hp = PDEF[PT_WALL].hp;
    grid[0][3].type = PT_SNOW; grid[0][3].hp = PDEF[PT_SNOW].hp;
    grid[4][1].type = PT_SNOW; grid[4][1].hp = PDEF[PT_SNOW].hp;
    spawn_zombie(); zomb[0].x = 980; zomb[0].row = 4;
    spawn_zombie(); zomb[1].x = 1100; zomb[1].row = 0;
    spawn_boss();   zomb[2].x = 1020; boss_spawned = 1;
    banner_text = "КОРОЛЕВА В РОБОТЕ!"; banner_t = 5;
    spawn_pea(2, 400, 20, 0);
    spawn_pea(1, 520, 20, 0);
    spawn_sun(700, 360); suns[0].target_y = 360; suns[0].y = 360;
}

/* ------------------------------------------------------------------ */
/* update                                                             */
/* ------------------------------------------------------------------ */

static void update_play(float dt) {
    global_t += dt;
    for (int i = 0; i < PT_COUNT; i++)
        if (cooldown[i] > 0) cooldown[i] -= dt;

    /* Sky sun is the only income; the author did not draw a sunflower. */
    sky_sun_t -= dt;
    if (sky_sun_t <= 0) {
        spawn_sun(LAWN_X + 100 + rndf() * (COLS * CELL_W - 200), -30);
        sky_sun_t = 6 + rndf() * 2.5f;
    }

    /* The author's duck is the only wave enemy. Health grows with the level;
     * the queen's robot still appears ONLY after level ten's duck wave. */
    if (to_spawn > 0) {
        spawn_t -= dt;
        if (spawn_t <= 0) {
            if (spawn_zombie()) {
                to_spawn--;
                float prog = 1.0f - (float)to_spawn / total_zombies;
                spawn_t = 5.1f - 2.0f * prog - 0.13f * level + rndf() * 0.8f;
                if (spawn_t < 1.8f) spawn_t = 1.8f;
            } else spawn_t = 0.5f;          /* full enemy pool: retry next time */
        }
    }
    if (banner_t > 0) banner_t -= dt;

    /* plants */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Plant *p = &grid[r][c];
            if (p->type < 0) continue;
            p->sway = sinf(global_t * 2 + r + c) * 1.2f;
            if (p->type == PT_PEA || p->type == PT_SNOW) {
                int target = 0;
                for (int i = 0; i < ZMAX; i++)
                    if (zomb[i].active && zomb[i].row == r && zomb[i].x > CELL_CX(c)) { target = 1; break; }
                p->fire_t -= dt;
                if (target && p->fire_t <= 0) {
                    spawn_pea(r, CELL_CX(c) + 16, 20, p->type == PT_SNOW);
                    p->fire_t = (p->type == PT_SNOW) ? 1.5f : 1.4f;
                }
            }
            if (p->hp <= 0) { p->type = PT_NONE; burst(CELL_CX(c), CELL_CY(r), 12, COL(80, 160, 70), 120); }
        }

    /* peas */
    for (int i = 0; i < PEAMAX; i++) {
        Pea *pe = &peas[i];
        if (!pe->active) continue;
        pe->x += 540 * dt;
        if (pe->x > GAME_W + 20) { pe->active = 0; continue; }
        for (int j = 0; j < ZMAX; j++) {
            Zombie *z = &zomb[j];
            if (!z->active) continue;
            int robot = z->type == EN_ROBOT;
            if (abs(z->row - pe->row) > (robot ? 1 : 0)) continue;
            if (pe->x >= z->x - (robot ? 65 : 24) && pe->x <= z->x + (robot ? 55 : 20)) {
                z->hp -= pe->dmg;
                if (pe->snow && !robot) z->slow = 5.0f; /* robot ignores frost */
                burst(pe->x, pe->y, 5, pe->snow ? COL(180, 230, 255) : COL(120, 200, 90), 90);
                pe->active = 0;
                if (z->hp <= 0) {
                    z->active = 0;
                    burst(z->x, CELL_CY(z->row), 16, COL(150, 170, 130), 140);
                }
                break;
            }
        }
    }

    /* ducks and the final robot */
    for (int i = 0; i < ZMAX; i++) {
        Zombie *z = &zomb[i];
        if (!z->active) continue;
        if (z->slow > 0) z->slow -= dt;
        z->anim += dt * (z->type == EN_ROBOT ? 2 : 8);
        float spd = z->speed * (z->slow > 0 ? 0.5f : 1.0f);

        if (z->type == EN_ROBOT) {
            z->x -= spd * dt;          /* never stops to eat: the mech TRAMPLES */
            for (int r = z->row - 1; r <= z->row + 1; r++) {
                if (r < 0 || r >= ROWS) continue;
                for (int c = 0; c < COLS; c++)
                    if (grid[r][c].type >= 0 && fabsf(z->x - CELL_CX(c)) < 78) {
                        grid[r][c].type = PT_NONE;
                        burst(CELL_CX(c), CELL_CY(r), 16, COL(220, 70, 55), 140);
                    }
                if (!mower[r].used && z->x <= LAWN_X + 75) {
                    mower[r].used = 1;
                    mower[r].running = 0;
                    burst(mower[r].x, CELL_CY(r), 20, COL(220, 70, 60), 180);
                }
            }
            if (z->x < LAWN_X - 35) { phase = PH_LOSE; return; }
            continue;
        }

        if (z->x <= LAWN_X) { /* reached the lawn edge -> mower / lose */
            if (!mower[z->row].used) {
                mower[z->row].used = 1; mower[z->row].running = 1;
            } else if (z->x < LAWN_X - 25) {
                phase = PH_LOSE;
                burst(z->x, CELL_CY(z->row), 30, COL(200, 60, 60), 160);
                return;
            }
        }

        int col = (int)((z->x - LAWN_X) / CELL_W);
        if (col < 0) col = 0;
        if (col >= COLS) col = COLS - 1;
        Plant *p = &grid[z->row][col];
        if (p->type >= 0 && z->x <= CELL_CX(col) + 28) {
            z->eating = 1;
            p->hp -= 100.0f * dt;
        } else {
            z->eating = 0;
            z->x -= spd * dt;
        }
    }

    /* mowers */
    for (int r = 0; r < ROWS; r++) {
        if (!mower[r].running) continue;
        mower[r].x += 520 * dt;
        for (int i = 0; i < ZMAX; i++) {
            Zombie *z = &zomb[i];
            if (!z->active || abs(z->row - r) > (z->type == EN_ROBOT ? 1 : 0)) continue;
            if (z->type == EN_ROBOT) {                        /* robot smashes mowers */
                if (z->x < mower[r].x + 80 && z->x > mower[r].x - 100) {
                    mower[r].running = 0;
                    mower[r].used = 1;
                    burst(mower[r].x, CELL_CY(r), 26, COL(220, 70, 60), 220);
                }
                continue;
            }
            if (z->x < mower[r].x + 40 && z->x > mower[r].x - 20) {
                z->active = 0;
                burst(z->x, CELL_CY(r), 20, COL(200, 60, 60), 200);
            }
        }
        if (mower[r].x > GAME_W + 40) mower[r].running = 0;
    }

    /* suns */
    for (int i = 0; i < SUNMAX; i++) {
        Sun *s = &suns[i];
        if (!s->active) continue;
        s->life -= dt;
        if (s->life <= 0) { s->active = 0; continue; }
        s->bob += dt * 3;
        if (s->y < s->target_y) s->y += s->vy * dt;
    }

    update_parts(dt);

    /* A wave ends only after all its ducks actually die (including mower
     * kills this frame). The queen and her robot arrive ONLY at the very end
     * of level ten; destroying the robot then finishes the whole game. */
    int remaining = 0;
    for (int i = 0; i < ZMAX; i++) if (zomb[i].active) remaining++;
    if (to_spawn == 0 && remaining == 0) {
        if (level == 10 && !boss_spawned) {
            if (spawn_boss()) {
                boss_spawned = 1;
                banner_text = "КОРОЛЕВА В РОБОТЕ!";
                banner_t = 5;
            }
        } else phase = level == 10 ? PH_WIN : PH_LEVEL_CLEAR;
    }

}

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */

static int inside(int x, int y, int x0, int y0, int x1, int y1) {
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

static void advance_intro(void) {
    intro_step++;
    intro_t = 0;
    if (intro_step >= 3) start_level(1);
}

void game_input_press(int x, int y) {
    if (x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) return;
    if (phase == PH_MENU) {
        if (inside(x, y, 440, 490, 840, 600)) {
            intro_step = 0; intro_t = 0; phase = PH_INTRO;
        }
        return;
    }
    if (phase == PH_INTRO) {
        if (inside(x, y, 1020, 10, 1270, 92)) start_level(1); /* skip */
        else advance_intro();                                  /* next line */
        return;
    }
    if (phase == PH_LEVEL_CLEAR) {
        if (inside(x, y, 390, 450, 890, 570)) start_level(level + 1);
        return;
    }
    if (phase == PH_WIN) {
        if (inside(x, y, 425, 460, 855, 590)) { start_level(1); phase = PH_MENU; }
        return;
    }
    if (phase == PH_LOSE) {
        if (inside(x, y, 335, 460, 645, 570)) start_level(level); /* retry */
        else if (inside(x, y, 660, 460, 975, 570)) { start_level(1); phase = PH_MENU; }
        return;
    }
    if (inside(x, y, 1140, 95, 1270, 140)) { start_level(1); phase = PH_MENU; return; }

    /* collect a sun first */
    for (int i = SUNMAX - 1; i >= 0; i--) {
        Sun *s = &suns[i];
        if (!s->active) continue;
        int dx = x - (int)s->x, dy = y - (int)s->y;
        if (dx * dx + dy * dy < 44 * 44) {
            s->active = 0; sun_res += 25;
            burst(s->x, s->y, 10, COL(255, 230, 80), 110);
            return;
        }
    }

    /* Only the three packets with drawings can be selected. */
    if (y >= 16 && y <= 132) {
        for (int i = 0; i < PT_COUNT; i++) {
            int x0 = CARD_X + i * CARD_STEP;
            if (x >= x0 && x <= x0 + CARD_W) {
                selected = (sun_res >= PDEF[i].cost && cooldown[i] <= 0) ? i : -1;
                return;
            }
        }
    }

    /* plant on the lawn */
    if (selected >= 0 && x >= LAWN_X && x < LAWN_X + COLS * CELL_W &&
        y >= LAWN_Y && y < LAWN_Y + ROWS * CELL_H) {
        int c = (x - LAWN_X) / CELL_W, r = (y - LAWN_Y) / CELL_H;
        if (grid[r][c].type < 0 && sun_res >= PDEF[selected].cost && cooldown[selected] <= 0) {
            grid[r][c].type = selected;
            grid[r][c].hp = (float)PDEF[selected].hp;
            grid[r][c].fire_t = 0.4f;
            sun_res -= PDEF[selected].cost;
            cooldown[selected] = PDEF[selected].recharge;
            burst(CELL_CX(c), CELL_CY(r), 8, PDEF[selected].body, 90);
        }
        selected = -1;
        return;
    }
    selected = -1;
}

void game_input_release(int x, int y) { (void)x; (void)y; }

/* ------------------------------------------------------------------ */
/* render                                                             */
/* ------------------------------------------------------------------ */

static void draw_background(void) {
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(37, 68, 42));
    if (sprite_pixels[SPR_MAP]) {
        /* The uploaded map has wood on its left and lawn on its right. Crop
         * those separately so its path lines up with the game's nine cells. */
        sprite_crop(SPR_MAP, 0, LAWN_Y, LAWN_X, ROWS * CELL_H,
                    0, 0, 246, 500, 0);
        sprite_crop(SPR_MAP, LAWN_X, LAWN_Y, COLS * CELL_W, ROWS * CELL_H,
                    274, 0, 226, 500, 0);
        rect_blend(LAWN_X, LAWN_Y, GAME_W - 1, LAWN_Y + ROWS * CELL_H - 1,
                   COL(35, 82, 30), 55); /* preserve the drawing, soften neon */
    } else {
        rect(LAWN_X, LAWN_Y, GAME_W - 1, LAWN_Y + ROWS * CELL_H - 1, COL(108, 171, 73));
        rect(0, LAWN_Y, LAWN_X - 1, LAWN_Y + ROWS * CELL_H - 1, COL(139, 84, 39));
    }
    /* The same new map gets a different time-of-day tint as the levels grow. */
    if (level >= 4 && level <= 6)
        rect_blend(0, LAWN_Y, GAME_W - 1, LAWN_Y + ROWS * CELL_H - 1, COL(156, 94, 31), 32);
    if (level >= 7)
        rect_blend(0, LAWN_Y, GAME_W - 1, LAWN_Y + ROWS * CELL_H - 1,
                   COL(27, 44, 90), level == 10 ? 75 : 42);
    /* Subtle checker and cell borders so planting on the painted map is clear. */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int x = LAWN_X + c * CELL_W, y = LAWN_Y + r * CELL_H;
            if ((r + c) & 1)
                rect_blend(x, y, x + CELL_W - 1, y + CELL_H - 1, COL(20, 69, 17), 23);
            rect_blend(x, y, x + CELL_W - 1, y + 1, COL(28, 86, 20), 55);
            rect_blend(x, y, x + 1, y + CELL_H - 1, COL(28, 86, 20), 55);
        }
}

static void draw_mowers(void) {
    for (int r = 0; r < ROWS; r++) {
        if (mower[r].used && !mower[r].running) continue;
        int x = (int)mower[r].x, y = CELL_CY(r) + 18;
        sprite_draw(SPR_MOWER, x - 44, y - 43, 88, 86, 0);
    }
}

static int alive_count(void);

static void draw_seed_bar(void) {
    if (sprite_pixels[SPR_MAP])
        sprite_crop(SPR_MAP, 0, 0, GAME_W, 140, 0, 0, 242, 135, 0);
    else rect(0, 0, GAME_W - 1, 140, COL(96, 64, 40));
    rect_blend(0, 0, GAME_W - 1, 140, COL(31, 25, 30), 105);
    rect(0, 140, GAME_W - 1, 146, COL(70, 46, 28));
    draw_sun_icon(64, 70, 26);
    draw_int(104, 50, 6, COL(250, 250, 250), sun_res);
    /* Only the author's peashooter, walnut and snow-pea have packets. */
    for (int i = 0; i < PT_COUNT; i++) {
        int x0 = CARD_X + i * CARD_STEP;
        int affordable = sun_res >= PDEF[i].cost && cooldown[i] <= 0;
        uint32_t border = selected == i ? COL(255, 234, 70) : COL(65, 44, 31);
        rect(x0 - 3, 15, x0 + CARD_W + 3, 135, border);
        rect(x0, 18, x0 + CARD_W, 132, COL(236, 224, 188));
        rect(x0, 18, x0 + CARD_W, 24, PDEF[i].body);
        draw_plant(x0 + CARD_W / 2, 70, i, 0);
        if (!affordable) rect_blend(x0, 18, x0 + CARD_W, 132, COL(10, 10, 25), 115);
        draw_int(x0 + 12, 108, 3, affordable ? COL(63, 39, 22) : COL(250, 228, 198), PDEF[i].cost);
        if (cooldown[i] > 0) {
            float frac = cooldown[i] / PDEF[i].recharge;
            rect_blend(x0, 18, x0 + CARD_W, 18 + (int)(114 * frac), COL(10, 10, 20), 115);
        }
    }
    /* level, wave progress and the way back to the menu */
    int spawned = total_zombies - to_spawn;
    draw_text(1055, 12, 3, COL(255, 230, 175), "ВОЛНА");
    rect(1055, 39, 1245, 54, COL(42, 29, 25));
    rect(1056, 40, 1056 + spawned * 188 / total_zombies, 53, COL(244, 139, 62));
    draw_text(1055, 70, 3, COL(245, 240, 211), "УРОВЕНЬ");
    draw_int(1210, 70, 3, COL(255, 245, 165), level);
    draw_text(1028, 112, 2, COL(255, 236, 202), "УТКИ");
    draw_int(1086, 112, 2, COL(255, 236, 202), alive_count());
    rect(1140, 99, 1265, 137, COL(55, 49, 58));
    rect(1143, 102, 1262, 134, COL(108, 84, 64));
    draw_text_c(1202, 108, 3, COL(255, 242, 210), "МЕНЮ");
}

static int alive_count(void) {
    int n = 0; for (int i = 0; i < ZMAX; i++) if (zomb[i].active) n++; return n;
}

/* UI: all buttons have real hit rectangles handled in game_input_press. */
static void draw_button(int x0, int y0, int x1, int y1, const char *label, int size) {
    rect(x0, y0 + 5, x1, y1 + 5, COL(32, 28, 32));
    rect(x0, y0, x1, y1, COL(255, 208, 92));
    rect(x0 + 5, y0 + 5, x1 - 5, y1 - 5, COL(113, 67, 42));
    rect(x0 + 9, y0 + 8, x1 - 9, y0 + 12, COL(166, 107, 61));
    while (size > 1 && text_w(size, label) > x1 - x0 - 22) size--;
    draw_text_c((x0 + x1) / 2, (y0 + y1 - size * 7) / 2,
                size, COL(255, 244, 211), label);
}

static void draw_dima(int cx, int feet_y, int size) {
    /* Show the author's masked Dima as-is, without an invented body. */
    sprite_draw(SPR_MASK, cx - size / 2, feet_y - size, size, size, 0);
    draw_text_c(cx, feet_y + 8, 3, COL(255, 239, 197), "ДИМА");
}

static void draw_khlebushek(int cx, int feet_y, int size, int crying) {
    /* The author's white bird (680), not the walnut (685). */
    sprite_draw(SPR_KHLEBUSHEK, cx - size / 2, feet_y - size, size, size, 0);
    if (crying) {
        int drop = (int)(global_t * 32) % 24;
        int eye_x = cx - size / 2 + 49 * size / 100;
        int eye_y = feet_y - size + 17 * size / 100;
        ellipse(eye_x, eye_y + 11 + drop, 4, 10, COL(105, 205, 255));
        ellipse(eye_x - size / 20, eye_y + 19 + drop, 3, 8, COL(146, 221, 255));
    }
    draw_text_c(cx, feet_y + 8, 3, COL(255, 239, 197), "ХЛЕБУШЕК");
}

static void draw_kirill(int cx, int feet_y, int size) {
    /* This is Без названия681_20260926093232.png, identified by the author. */
    sprite_draw(SPR_KIRILL, cx - size / 2, feet_y - size, size, size, 0);
    draw_text_c(cx, feet_y + 8, 3, COL(255, 239, 197), "КИРИЛЛ");
}

static void draw_menu(void) {
    draw_background();
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(14, 23, 31), 105);
    rect_blend(36, 35, 1244, 211, COL(18, 25, 36), 217);
    draw_text_c(640, 60, 7, COL(255, 229, 157), "РАСТЕНИЯ ПРОТИВ ГУСЕЙ");
    draw_text_c(640, 146, 4, COL(239, 240, 224), "ИСТОРИЯ ХЛЕБУШКА - 10 УРОВНЕЙ");

    draw_khlebushek(160, 466, 170, 0);
    draw_dima(345, 460, 190);
    draw_kirill(939, 463, 190);
    ellipse(1091, 473, 116, 13, COL(29, 53, 35));
    sprite_draw(SPR_ROBOT, 980, 226, 240, 240, 0);
    draw_text_c(1100, 473, 3, COL(255, 220, 168), "КОРОЛЕВА");
    draw_button(440, 490, 840, 600, "ИГРАТЬ", 8);
    draw_text_c(640, 640, 3, COL(255, 242, 204),
                "ПЕРВЫЙ УРОВЕНЬ НАЧНЁТСЯ ПОСЛЕ КАТ-СЦЕНЫ");
}

static void draw_intro(void) {
    draw_background();
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(17, 24, 48), 105);
    rect_blend(0, 0, GAME_W - 1, 115, COL(17, 20, 35), 221);
    draw_text_c(535, 37, 6, COL(255, 224, 157), "НАЧАЛО ИСТОРИИ");
    draw_button(1020, 10, 1270, 89, "ПРОПУСТИТЬ", 3);
    draw_khlebushek(245, 492, 246, intro_step == 0);
    /* Dima really walks toward Khlebushek in the first shot. */
    float approach = intro_t / 2.8f;
    if (approach > 1 || intro_step > 0) approach = 1;
    int dx = 935 - (int)(430 * approach);
    draw_dima(dx, 492, 240);
    if (intro_step >= 2) draw_kirill(977, 492, 248);

    rect(95, 533, 1185, 679, COL(29, 33, 47));
    rect(102, 540, 1178, 672, COL(238, 215, 166));
    rect(111, 548, 1169, 665, COL(54, 52, 56));
    const char *speaker = intro_step == 0 ? "ХЛЕБУШЕК" :
                          intro_step == 1 ? "ДИМА" : "КИРИЛЛ";
    const char *line = intro_step == 0 ? "ХЛЕБУШЕК ПЛАЧЕТ..." :
                       intro_step == 1 ? "НЕ ПЛАЧЬ, МЫ НОВОГО СДЕЛАЕМ." :
                       "МОЖЕТ, КТО-ТО ПОМОГАТЬ МНЕ БУДЕТ?";
    draw_text(145, 554, 3, COL(255, 218, 129), speaker);
    int size = 5;
    while (size > 2 && text_w(size, line) > 990) size--;
    draw_text_c(640, 602, size, COL(255, 251, 228), line);
    draw_text_c(1085, 690, 2, COL(255, 242, 196), "КОСНИСЬ: ДАЛЬШЕ");
}

static void draw_play_scene(void) {
    draw_background();
    draw_mowers();
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Plant *p = &grid[r][c];
            if (p->type < 0) continue;
            draw_plant(CELL_CX(c), CELL_CY(r), p->type, p->sway);
        }
    for (int r = 0; r < ROWS; r++)
        for (int i = 0; i < ZMAX; i++)
            if (zomb[i].active && zomb[i].row == r) draw_enemy(&zomb[i]);
    for (int i = 0; i < PEAMAX; i++)
        if (peas[i].active) disc((int)peas[i].x, (int)peas[i].y, 9,
                                 peas[i].snow ? COL(180, 230, 255) : COL(120, 210, 90));
    for (int i = 0; i < SUNMAX; i++)
        if (suns[i].active) {
            int bob = (int)(sinf(suns[i].bob) * 3);
            draw_sun_icon((int)suns[i].x, (int)suns[i].y + bob, 22);
            if (suns[i].life < 2.0f && ((int)(suns[i].life * 4) & 1))
                disc((int)suns[i].x, (int)suns[i].y + bob, 22,
                     blend(COL(0, 0, 0), COL(255, 240, 120), 80));
        }
    draw_parts();
    draw_seed_bar();
    if (banner_t > 0 && banner_text && phase == PH_PLAY) {
        int s = 3;
        while (s > 1 && text_w(s, banner_text) > GAME_W - 120) s--;
        int bw = text_w(s, banner_text) + 44;
        rect_blend(640 - bw / 2, 156, 640 + bw / 2, 172 + 7 * s,
                   COL(12, 12, 26), 205);
        draw_text_c(640, 165, s, COL(255, 240, 180), banner_text);
    }
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].active && zomb[i].type == EN_ROBOT) {
            float bf = zomb[i].hp / zomb[i].maxhp;
            if (bf < 0) bf = 0;
            if (bf > 1) bf = 1;
            rect(395, 684, 885, 717, COL(22, 22, 32));
            rect(401, 690, 401 + (int)(478 * bf), 711, COL(225, 60, 75));
            draw_text_c(640, 697, 2, COL(255, 236, 165), "КОРОЛЕВА В РОБОТЕ");
            break;
        }
}

static void draw_result(void) {
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(11, 17, 31), 199);
    if (phase == PH_LEVEL_CLEAR) {
        draw_text_c(640, 252, 7, COL(255, 231, 163), "УРОВЕНЬ ПРОЙДЕН!");
        draw_text_c(570, 354, 4, COL(248, 247, 231), "СЛЕДУЮЩИЙ УРОВЕНЬ:");
        draw_int(878, 354, 4, COL(255, 216, 119), level + 1);
        draw_button(390, 450, 890, 565, "ДАЛЬШЕ", 7);
    } else if (phase == PH_LOSE) {
        draw_text_c(640, 254, 7, COL(255, 202, 161),
                    level == 10 && boss_spawned ? "РОБОТ УНИЧТОЖИЛ ВСЕХ!" : "УТКИ ПРОРВАЛИСЬ!");
        draw_text_c(640, 361, 4, COL(248, 247, 231), "ПОПРОБУЙ ЕЩЁ РАЗ");
        draw_button(335, 460, 645, 565, "ПОВТОРИТЬ", 4);
        draw_button(660, 460, 975, 565, "В МЕНЮ", 4);
    } else if (phase == PH_WIN) {
        draw_text_c(640, 237, 7, COL(255, 231, 163), "КОРОЛЕВА ПОБЕЖДЕНА!");
        draw_text_c(640, 340, 4, COL(248, 247, 231), "10 УРОВНЕЙ ПРОЙДЕНО. ГУСИ СПАСЕНЫ!");
        draw_button(425, 460, 855, 580, "В МЕНЮ", 6);
    }
}

static void render(void) {
    if (phase == PH_MENU) { draw_menu(); return; }
    if (phase == PH_INTRO) { draw_intro(); return; }
    draw_play_scene();
    if (phase != PH_PLAY) draw_result();
}

void game_tick(float dt, uint32_t *fb) {
    if (dt < 0) dt = 0;
    if (phase == PH_PLAY) update_play(dt);
    else {
        global_t += dt;
        if (phase == PH_INTRO) {
            intro_t += dt;
            const float length[3] = { 3.5f, 4.2f, 4.2f };
            if (intro_t >= length[intro_step]) advance_intro();
        }
    }
    if (fb) { FB = fb; render(); }
}

int game_phase(void) { return (int)phase; }
int game_level(void) { return level; }

#ifdef GAME_TEST
void game_debug_finish_wave(void) {
    to_spawn = 0;
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].type != EN_ROBOT) zomb[i].active = 0;
}
void game_debug_defeat_boss(void) {
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].type == EN_ROBOT) zomb[i].active = 0;
}
int game_debug_boss_alive(void) {
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].type == EN_ROBOT && zomb[i].active) return 1;
    return 0;
}
float game_debug_boss_x(void) {
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].type == EN_ROBOT && zomb[i].active) return zomb[i].x;
    return -1;
}
int game_debug_plant_type(int row, int col) {
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS) return PT_NONE;
    return grid[row][col].type;
}
int game_debug_seed_count(void) { return PT_COUNT; }
int game_debug_first_enemy_type(void) {
    for (int i = 0; i < ZMAX; i++) if (zomb[i].active) return zomb[i].type;
    return -1;
}
int game_debug_mower_used(int row) {
    if ((unsigned)row >= ROWS) return 0;
    return mower[row].used;
}
#endif
