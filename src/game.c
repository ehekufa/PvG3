/* game.c — Plants-vs-Zombies style tower defense, pure C.
 *
 * Platform independent: all logic + a software renderer that draws into a
 * single RGBA framebuffer. The host (Android) just blits that framebuffer.
 *
 * No external assets: every plant, zombie, projectile and UI element is drawn
 * procedurally from filled circles / ellipses / rects, and text uses a tiny
 * built-in 5x7 bitmap font.
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

static void line_thick(int x0, int y0, int x1, int y1, int t, uint32_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = dx < 0 ? -dx : dx;
    if (dy < 0 ? -dy : dy) steps = steps > (dy < 0 ? -dy : dy) ? steps : (dy < 0 ? -dy : dy);
    if (steps == 0) { disc(x0, y0, t / 2, c); return; }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        disc(x, y, t / 2, c);
    }
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
    ['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['/'] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    [' '] = {0,0,0,0,0,0,0},
    ['+'] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
};

static void draw_char(int x, int y, int s, uint32_t c, char ch) {
    if (ch < ' ' || ch > '~') ch = '?';
    const uint8_t *g = FNT[(int)ch];
    if (!g) return;
    for (int r = 0; r < 7; r++)
        for (int cc = 0; cc < 5; cc++)
            if (g[r] & (1 << (4 - cc)))
                rect(x + cc * s, y + r * s, x + cc * s + s - 1, y + r * s + s - 1, c);
}

static void draw_text(int x, int y, int s, uint32_t c, const char *str) {
    for (; *str; str++) { draw_char(x, y, s, c, *str); x += 6 * s; }
}

static int text_w(int s, const char *str) { return (int)strlen(str) * 6 * s; }

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

#define ZMAX 80
#define PEAMAX 200
#define SUNMAX 40
#define PARTMAX 400

typedef enum { PH_MENU, PH_PLAY, PH_WIN, PH_LOSE } Phase;

/* plant types */
enum { PT_NONE = -1, PT_SUN = 0, PT_PEA, PT_WALL, PT_SNOW, PT_CHERRY, PT_POTATO, PT_COUNT };

typedef struct { int cost; int hp; float recharge; uint32_t body, accent; const char *name; } PlantDef;

static const PlantDef PDEF[PT_COUNT] = {
    [PT_SUN]    = {  50, 300, 7.5f, COL(245,210,60),  COL(180,120,40), "SUN"    },
    [PT_PEA]    = { 100, 300, 7.5f, COL( 70,170,70),  COL( 40,110,40), "PEA"    },
    [PT_WALL]   = {  50,4000,30.0f, COL(180,120,70),  COL(120, 80,45), "NUT"    },
    [PT_SNOW]   = { 175, 300, 7.5f, COL(120,200,235), COL( 60,130,180),"SNOW"   },
    [PT_CHERRY] = { 150, 300,28.0f, COL(220, 40, 45), COL(255,120,110),"BOMB"   },
    [PT_POTATO] = {  25, 300,22.0f, COL(170,130, 80), COL( 90, 60,35), "MINE"   },
};

typedef struct { int type; float hp; float fire_t; float sun_t; float fuse; int armed; float sway; } Plant;
typedef struct { int active; int row; float x; float hp; float maxhp; int type;
                 float speed; int eating; int slow; float anim; } Zombie;
typedef struct { int active; int row; float x; float y; int dmg; int snow; } Pea;
typedef struct { int active; float x; float y; float vy; float target_y; float life; float bob; } Sun;
typedef struct { int active; float x, y, vx, vy; float life; float maxlife; uint32_t col; } Part;

static Phase phase;
static int sun_res;
static int selected;
static float cooldown[PT_COUNT];
static float sky_sun_t;
static float spawn_t;
static int to_spawn;
static int total_zombies;
static float final_t;
static float banner_t;
static float global_t;
static float flash_t;

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

static void draw_plant(int cx, int cy, int type, float hpfrac, float sway, int extra) {
    if (type < 0) return;
    const PlantDef *d = &PDEF[type];
    float f = hpfrac < 0 ? 1 : (hpfrac < 0 ? 0 : hpfrac > 1 ? 1 : hpfrac);
    (void)f;
    int base = cy + 34;
    /* stem + leaves shared */
    line_thick(cx, base, cx + (int)(sway * 4), cy + 4, 8, COL(40, 100, 45));
    ellipse(cx - 20, base - 4, 18, 10, COL(50, 130, 55));
    ellipse(cx + 20, base - 4, 18, 10, COL(50, 130, 55));

    switch (type) {
    case PT_SUN: {
        int py = cy - (int)(sway * 3);
        for (int k = 0; k < 10; k++) {
            float a = k * (6.283f / 10) + sway * 0.5f;
            ellipse(cx + (int)(cosf(a) * 30), py + (int)(sinf(a) * 30), 14, 14, d->body);
        }
        disc(cx, py, 24, d->accent);
        disc(cx, py, 18, COL(80, 50, 25));
        disc(cx - 6, py - 3, 3, COL(20, 20, 20));
        disc(cx + 6, py - 3, 3, COL(20, 20, 20));
        break;
    }
    case PT_PEA:
    case PT_SNOW: {
        int hx = cx + (int)(sway * 2);
        disc(hx, cy, 22, d->body);
        disc(hx + 16, cy - 2, 13, d->accent);
        disc(hx + 20, cy - 2, 9, d->body);
        disc(hx - 6, cy - 6, 3, COL(20, 20, 20));
        if (type == PT_SNOW) {
            disc(hx + 20, cy - 2, 5, COL(220, 245, 255));
            for (int k = 0; k < 3; k++)
                disc(hx - 18 + k * 6, cy + 18 + (k % 2) * 4, 2, COL(220, 245, 255));
        }
        break;
    }
    case PT_CHERRY: {
        int bx = cx - 12, by = cy + 8;
        disc(bx, by, 16, d->body);
        disc(cx + 12, cy + 10, 16, d->body);
        disc(bx - 2, by - 4, 5, COL(255, 150, 140));
        disc(cx + 10, cy + 6, 5, COL(255, 150, 140));
        line_thick(cx, cy, cx + 6, cy - 18, 4, COL(90, 130, 45));
        line_thick(cx + 6, cy - 18, cx + 18, cy - 26, 3, COL(60, 110, 40));
        disc(cx + 18, cy - 26, 3, COL(255, 240, 120));
        if (extra) {                                    /* about to detonate */
            disc(cx, cy, 26, blend(d->body, COL(255, 255, 200), 120));
            disc(cx + 8, cy - 24, 4, COL(255, 240, 150));
        }
        break;
    }
    case PT_POTATO: {
        ellipse(cx, cy, 24, 18, d->body);
        ellipse(cx - 8, cy - 4, 4, 4, d->accent);
        ellipse(cx + 6, cy + 4, 4, 4, d->accent);
        ellipse(cx + 4, cy - 6, 4, 4, d->accent);
        if (extra) {                                    /* armed */
            disc(cx, cy - 16, 4, COL(255, 60, 50));
            for (int k = 0; k < 6; k++) {
                float a = k * 1.047f;
                line_thick(cx + (int)(cosf(a) * 22), cy + (int)(sinf(a) * 15),
                           cx + (int)(cosf(a) * 30), cy + (int)(sinf(a) * 21), 2, d->accent);
            }
        }
        break;
    }
    case PT_WALL: {
        ellipse(cx, cy, 30, 38, d->body);
        ellipse(cx - 10, cy - 6, 8, 8, COL(150, 95, 55));
        disc(cx - 8, cy - 4, 3, COL(20, 20, 20));
        disc(cx + 8, cy - 4, 3, COL(20, 20, 20));
        rect(cx - 10, cy + 8, cx + 10, cy + 14, COL(90, 55, 30));
        if (hpfrac < 0.66f) line_thick(cx - 18, cy - 10, cx - 4, cy + 6, 2, COL(120, 80, 45));
        if (hpfrac < 0.33f) line_thick(cx + 4, cy - 16, cx + 16, cy + 10, 2, COL(120, 80, 45));
        break;
    }
    }
}

static void draw_zombie(const Zombie *z) {
    float fr = z->hp / z->maxhp; if (fr > 1) fr = 1; if (fr < 0) fr = 0;
    uint32_t body = z->slow ? COL(120, 160, 175) : COL(150, 170, 130);
    uint32_t dark = z->slow ? COL(70, 100, 120) : COL(80, 105, 70);
    body = blend(body, COL(40, 30, 25), (int)((1 - fr) * 90));
    int x = (int)z->x, y = CELL_CY(z->row) - 6;
    float w = sinf(z->anim) * 4;
    /* legs */
    rect(x - 10, y + 28, x - 2 + (int)w, y + 54, dark);
    rect(x + 2 - (int)w, y + 28, x + 10, y + 54, dark);
    /* torso */
    rect(x - 14, y - 6, x + 12, y + 32, body);
    /* arms reaching right */
    rect(x + 8, y - 4 + (int)(w * 0.5f), x + 30, y + 6, body);
    rect(x + 8, y + 8 - (int)(w * 0.5f), x + 30, y + 18, body);
    /* head */
    disc(x + 2, y - 16, 15, body);
    disc(x - 2, y - 18, 3, COL(230, 230, 230));
    disc(x + 9, y - 18, 3, COL(230, 230, 230));
    rect(x - 4, y - 8, x + 8, y - 4, dark);
    /* headgear */
    if (z->type == 1) { /* cone */
        int by = y - 28;
        line_thick(x + 2, by + 24, x + 2, by, 16, COL(225, 115, 40));
        rect(x - 5, by + 4, x + 9, by + 7, COL(255, 160, 80));
        rect(x - 3, by + 12, x + 7, by + 15, COL(255, 160, 80));
    } else if (z->type == 2) { /* bucket */
        rect(x - 12, y - 30, x + 16, y - 14, COL(115, 120, 130));
        rect(x - 12, y - 30, x + 16, y - 26, COL(150, 155, 165));
        rect(x - 14, y - 22, x + 18, y - 20, COL(80, 85, 95));
    }
    if (z->slow) disc(x + 2, y - 16, 16, blend(COL(150, 210, 255), FB[0], 60));
}

/* ------------------------------------------------------------------ */
/* spawning                                                           */
/* ------------------------------------------------------------------ */

static void spawn_zombie(void) {
    for (int i = 0; i < ZMAX; i++)
        if (!zomb[i].active) {
            Zombie *z = &zomb[i];
            float prog = total_zombies ? 1.0f - (float)to_spawn / total_zombies : 0;
            int t;
            float r = rndf();
            if (prog < 0.4f) t = 0;
            else if (prog < 0.75f) t = r < 0.7f ? 0 : 1;
            else t = r < 0.5f ? 0 : (r < 0.8f ? 1 : 2);
            z->active = 1;
            z->row = (int)(rndf() * ROWS);
            z->x = GAME_W + 30 + rndf() * 60;
            z->type = t;
            int hp[3] = {200, 560, 1100};
            z->hp = z->maxhp = (float)hp[t];
            z->speed = 22 + rndf() * 6;
            z->eating = 0; z->slow = 0; z->anim = rndf() * 6.28f;
            return;
        }
}

static void spawn_sun(float x, float y, int from_sky) {
    for (int i = 0; i < SUNMAX; i++)
        if (!suns[i].active) {
            Sun *s = &suns[i];
            s->active = 1; s->x = x; s->y = y; s->bob = rndf() * 6.28f;
            if (from_sky) { s->vy = 50; s->target_y = 220 + rndf() * 400; }
            else { s->vy = -40; s->target_y = y; }
            s->life = 9.0f;
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

static void reset_play(void) {
    memset(grid, 0, sizeof(grid));
    memset(zomb, 0, sizeof(zomb));
    memset(peas, 0, sizeof(peas));
    memset(suns, 0, sizeof(suns));
    memset(parts, 0, sizeof(parts));
    for (int r = 0; r < ROWS; r++) { mower[r].used = 0; mower[r].running = 0; mower[r].x = LAWN_X - 30; }
    for (int i = 0; i < PT_COUNT; i++) cooldown[i] = 0;
    sun_res = 150;
    selected = -1;
    sky_sun_t = 8.0f;
    total_zombies = 26;
    to_spawn = total_zombies;
    spawn_t = 18.0f;
    final_t = 0; banner_t = 0; flash_t = 0;
}

void game_init(void) {
    RNG = 0xC0FFEE11u;
    reset_play();
    phase = PH_MENU;
    global_t = 0;
}

void game_debug_snapshot(void) {
    game_init();
    phase = PH_PLAY;
    /* a populated scene for screenshots */
    sun_res = 300;
    grid[2][1].type = PT_SUN; grid[2][1].hp = PDEF[PT_SUN].hp;
    grid[2][2].type = PT_PEA; grid[2][2].hp = PDEF[PT_PEA].hp;
    grid[1][2].type = PT_PEA; grid[1][2].hp = PDEF[PT_PEA].hp;
    grid[3][2].type = PT_WALL; grid[3][2].hp = PDEF[PT_WALL].hp;
    grid[0][3].type = PT_SNOW; grid[0][3].hp = PDEF[PT_SNOW].hp;
    grid[4][1].type = PT_SUN; grid[4][1].hp = PDEF[PT_SUN].hp;
    grid[2][3].type = PT_CHERRY; grid[2][3].hp = PDEF[PT_CHERRY].hp; grid[2][3].fuse = 0.5f;
    grid[4][4].type = PT_POTATO; grid[4][4].hp = PDEF[PT_POTATO].hp; grid[4][4].fuse = 0; grid[4][4].armed = 1;
    spawn_zombie(); zomb[0].x = 1080; zomb[0].row = 2; zomb[0].type = 1;
    spawn_zombie(); zomb[1].x = 1160; zomb[1].row = 1; zomb[0].type = 0;
    spawn_pea(2, 400, 20, 0);
    spawn_pea(1, 520, 20, 0);
    spawn_sun(700, 360, 1); suns[0].target_y = 360; suns[0].y = 360;
}

/* ------------------------------------------------------------------ */
/* update                                                             */
/* ------------------------------------------------------------------ */

static void explode_cherry(int r, int c) {
    int cx = CELL_CX(c), cy = CELL_CY(r);
    flash_t = 0.28f;
    burst(cx, cy, 60, COL(255, 120, 60), 280);
    burst(cx, cy, 40, COL(255, 230, 120), 200);
    for (int i = 0; i < ZMAX; i++) {
        Zombie *z = &zomb[i];
        if (!z->active) continue;
        if (abs(z->row - r) <= 1 && fabsf(z->x - cx) < CELL_W * 1.7f) {
            z->hp -= 3000;
            if (z->hp <= 0) { z->active = 0; burst(z->x, CELL_CY(z->row), 14, COL(150,170,130), 160); }
        }
    }
}

static void explode_potato(int r, int c) {
    int cx = CELL_CX(c), cy = CELL_CY(r);
    flash_t = 0.12f;
    burst(cx, cy, 24, COL(220, 160, 80), 200);
    for (int i = 0; i < ZMAX; i++) {
        Zombie *z = &zomb[i];
        if (!z->active || z->row != r) continue;
        if (fabsf(z->x - cx) < CELL_W * 0.8f) {
            z->hp -= 1800;
            if (z->hp <= 0) { z->active = 0; burst(z->x, CELL_CY(z->row), 12, COL(150,170,130), 150); }
        }
    }
}

static void update_play(float dt) {
    global_t += dt;
    for (int i = 0; i < PT_COUNT; i++)
        if (cooldown[i] > 0) cooldown[i] -= dt;

    /* sky sun */
    sky_sun_t -= dt;
    if (sky_sun_t <= 0) { spawn_sun(LAWN_X + 100 + rndf() * (COLS * CELL_W - 200), -30, 1); sky_sun_t = 9 + rndf() * 4; }

    /* spawn director */
    if (to_spawn > 0) {
        spawn_t -= dt;
        if (spawn_t <= 0) {
            spawn_zombie();
            to_spawn--;
            float prog = 1.0f - (float)to_spawn / total_zombies;
            spawn_t = (6.0f - 3.6f * prog) + rndf() * 1.5f;
            if (to_spawn == 6 && final_t == 0) { final_t = 4.0f; spawn_t = 1.0f; }
        }
    }
    if (final_t > 0) final_t -= dt;

    /* plants */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Plant *p = &grid[r][c];
            if (p->type < 0) continue;
            p->sway = sinf(global_t * 2 + r + c) * 1.2f;
            if (p->type == PT_SUN) {
                p->sun_t -= dt;
                if (p->sun_t <= 0) {
                    spawn_sun(CELL_CX(c) + (rndf() - 0.5f) * 30, CELL_CY(r) - 30, 0);
                    p->sun_t = 9.0f;
                }
            } else if (p->type == PT_PEA || p->type == PT_SNOW) {
                int target = 0;
                for (int i = 0; i < ZMAX; i++)
                    if (zomb[i].active && zomb[i].row == r && zomb[i].x > CELL_CX(c)) { target = 1; break; }
                p->fire_t -= dt;
                if (target && p->fire_t <= 0) {
                    spawn_pea(r, CELL_CX(c) + 16, 20, p->type == PT_SNOW);
                    p->fire_t = (p->type == PT_SNOW) ? 1.5f : 1.4f;
                }
            } else if (p->type == PT_CHERRY) {
                p->fuse -= dt;
                if (p->fuse <= 0.0f) {
                    explode_cherry(r, c);
                    p->type = PT_NONE;
                    continue;
                }
            } else if (p->type == PT_POTATO) {
                if (!p->armed) {
                    p->fuse -= dt;
                    if (p->fuse <= 0.0f) p->armed = 1;
                } else {
                    int hit = 0;
                    for (int i = 0; i < ZMAX; i++)
                        if (zomb[i].active && zomb[i].row == r &&
                            fabsf(zomb[i].x - CELL_CX(c)) < CELL_W * 0.5f) { hit = 1; break; }
                    if (hit) {
                        explode_potato(r, c);
                        p->type = PT_NONE;
                        continue;
                    }
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
            if (!z->active || z->row != pe->row) continue;
            if (pe->x >= z->x - 24 && pe->x <= z->x + 20) {
                z->hp -= pe->dmg;
                if (pe->snow) z->slow = 5;
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

    /* zombies */
    int alive = 0;
    for (int i = 0; i < ZMAX; i++) {
        Zombie *z = &zomb[i];
        if (!z->active) continue;
        alive++;
        if (z->slow > 0) z->slow -= dt;
        z->anim += dt * 8;
        float spd = z->speed * (z->slow > 0 ? 0.5f : 1.0f);

        if (z->x <= LAWN_X) { /* reached the lawn edge -> mower / lose */
            if (!mower[z->row].used) {
                mower[z->row].used = 1; mower[z->row].running = 1;
            } else if (z->x < LAWN_X - 25) {
                phase = PH_LOSE;
                burst(z->x, CELL_CY(z->row), 30, COL(200, 60, 60), 160);
                return;
            }
        }

        /* eating */
        int col = (int)((z->x - LAWN_X) / CELL_W);
        if (col < 0) col = 0; if (col >= COLS) col = COLS - 1;
        Plant *p = &grid[z->row][col];
        if (p->type >= 0 && z->x <= CELL_CX(col) + 28) {
            z->eating = 1;
            p->hp -= 100 * dt;
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
            if (z->active && z->row == r && z->x < mower[r].x + 40 && z->x > mower[r].x - 20) {
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

    /* win */
    if (to_spawn == 0 && alive == 0) phase = PH_WIN;

    if (flash_t > 0) flash_t -= dt;
}

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */

void game_input_press(int x, int y) {
    (void)x; (void)y;
    if (phase == PH_MENU)   { reset_play(); phase = PH_PLAY; return; }
    if (phase == PH_WIN || phase == PH_LOSE) { reset_play(); phase = PH_PLAY; return; }

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

    /* seed bar */
    if (y >= 16 && y <= 132) {
        int sx = 250;
        for (int i = 0; i < PT_COUNT; i++) {
            int x0 = sx + i * 122;
            if (x >= x0 && x <= x0 + 112) {
                if (sun_res >= PDEF[i].cost && cooldown[i] <= 0) selected = i;
                else selected = -1;
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
            grid[r][c].sun_t = 4.0f;
            grid[r][c].fuse = (selected == PT_CHERRY) ? 1.1f : (selected == PT_POTATO ? 1.0f : 0.0f);
            grid[r][c].armed = 0;
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
    /* sky / top strip is covered by the wood bar; fill whole thing grass-dark */
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(70, 110, 55));
    /* lawn checker */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            rect(LAWN_X + c * CELL_W, LAWN_Y + r * CELL_H,
                 LAWN_X + (c + 1) * CELL_W - 1, LAWN_Y + (r + 1) * CELL_H - 1,
                 ((r + c) & 1) ? COL(110, 160, 72) : COL(122, 172, 84));
    /* house path on the left */
    rect(0, LAWN_Y, LAWN_X - 1, LAWN_Y + ROWS * CELL_H - 1, COL(150, 120, 80));
    for (int r = 0; r < ROWS; r++) {
        rect(8, CELL_CY(r) - 24, LAWN_X - 8, CELL_CY(r) + 24, COL(170, 140, 95));
    }
}

static void draw_mowers(void) {
    for (int r = 0; r < ROWS; r++) {
        int x = (int)mower[r].x, y = CELL_CY(r) + 18;
        if (mower[r].used && !mower[r].running) continue;
        rect(x - 22, y - 14, x + 22, y + 14, COL(190, 70, 60));
        rect(x - 18, y - 10, x + 6, y + 10, COL(230, 230, 220));
        disc(x - 16, y + 14, 8, COL(40, 40, 40));
        disc(x + 14, y + 14, 8, COL(40, 40, 40));
    }
}

static int alive_count(void);

static void draw_seed_bar(void) {
    rect(0, 0, GAME_W - 1, 140, COL(96, 64, 40));
    rect(0, 140, GAME_W - 1, 146, COL(70, 46, 28));
    /* sun counter */
    draw_sun_icon(64, 70, 26);
    draw_int(104, 50, 6, COL(250, 250, 250), sun_res);
    /* packets */
    int sx = 250;
    for (int i = 0; i < PT_COUNT; i++) {
        int x0 = sx + i * 122;
        int affordable = sun_res >= PDEF[i].cost && cooldown[i] <= 0;
        uint32_t card = affordable ? COL(236, 224, 188) : COL(150, 140, 115);
        rect(x0, 18, x0 + 112, 132, card);
        rect(x0, 18, x0 + 112, 24, PDEF[i].body);
        if (!affordable) rect_blend(x0, 18, x0 + 112, 132, COL(10, 10, 25), 130);
        draw_plant(x0 + 56, 76, i, 1, 0, 0);
        draw_int(x0 + 12, 108, 3, COL(60, 40, 20), PDEF[i].cost);
        /* recharge overlay */
        if (cooldown[i] > 0) {
            float frac = cooldown[i] / PDEF[i].recharge;
            rect_blend(x0, 18, x0 + 112, 18 + (int)(114 * frac), COL(10, 10, 20), 185);
        }
        if (selected == i) rect(x0 - 3, 15, x0 + 115, 135, COL(255, 240, 120));
    }
    /* wave progress */
    int spawned = total_zombies - to_spawn;
    draw_text(1060, 18, 3, COL(230, 220, 190), "WAVE");
    rect(1060, 40, 1240, 52, COL(60, 40, 25));
    rect(1060, 40, 1060 + spawned * 180 / total_zombies, 52, COL(220, 90, 70));
    draw_int(1080, 60, 3, COL(240, 240, 240), alive_count());
}

static int alive_count(void) {
    int n = 0; for (int i = 0; i < ZMAX; i++) if (zomb[i].active) n++; return n;
}

static void draw_overlay_msg(const char *title, const char *sub, int ts) {
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(5, 5, 15), 165);
    int s = ts;
    while (s > 3 && text_w(s, title) > GAME_W - 80) s--;   /* shrink to fit */
    draw_text_c(GAME_W / 2, 290, s, COL(250, 240, 200), title);
    if (((int)(global_t * 2) & 1))
        draw_text_c(GAME_W / 2, 450, 5, COL(255, 220, 120), sub);
}

static void render(void) {
    draw_background();
    draw_mowers();
    /* plants */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Plant *p = &grid[r][c];
            if (p->type < 0) continue;
            int ex = (p->type == PT_POTATO && p->armed) || (p->type == PT_CHERRY && p->fuse < 0.4f);
            draw_plant(CELL_CX(c), CELL_CY(r), p->type, p->hp / PDEF[p->type].hp, p->sway, ex);
        }
    /* zombies, sorted by row so closer rows draw on top */
    for (int r = 0; r < ROWS; r++)
        for (int i = 0; i < ZMAX; i++)
            if (zomb[i].active && zomb[i].row == r) draw_zombie(&zomb[i]);
    /* peas */
    for (int i = 0; i < PEAMAX; i++)
        if (peas[i].active) disc((int)peas[i].x, (int)peas[i].y, 9,
                                  peas[i].snow ? COL(180, 230, 255) : COL(120, 210, 90));
    /* suns */
    for (int i = 0; i < SUNMAX; i++)
        if (suns[i].active) {
            int bob = (int)(sinf(suns[i].bob) * 3);
            draw_sun_icon((int)suns[i].x, (int)suns[i].y + bob, 22);
            if (suns[i].life < 2.0f && ((int)(suns[i].life * 4) & 1))
                disc((int)suns[i].x, (int)suns[i].y + bob, 22, blend(COL(0, 0, 0), COL(255, 240, 120), 80));
        }
    draw_parts();

    if (flash_t > 0) {                                  /* explosion flash */
        int a = (int)(flash_t * 700);
        if (a > 200) a = 200;
        rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(255, 255, 255), a);
    }

    draw_seed_bar();

    if (final_t > 0)
        draw_text_c(GAME_W / 2, 160, 6, COL(255, 90, 70), "FINAL WAVE!");

    if (phase == PH_MENU)
        draw_overlay_msg("PLANTS VS ZOMBIES", "TAP TO START", 8);
    else if (phase == PH_LOSE)
        draw_overlay_msg("THE ZOMBIES ATE YOUR BRAINS!", "TAP TO RETRY", 8);
    else if (phase == PH_WIN)
        draw_overlay_msg("YOU SURVIVED!", "TAP TO PLAY AGAIN", 8);
}

void game_tick(float dt, uint32_t *fb) {
    FB = fb;
    if (phase == PH_PLAY) update_play(dt);
    else { global_t += dt; update_parts(dt); }
    render();
}

int game_phase(void) { return (int)phase; }
