/* game.c — lane defence, platform-independent simulation and software renderer.
 * The original PNG artwork is packed into sprites_data.h at build time (see
 * tools/pack_sprites.py). All characters and plants use the author's drawings;
 * the PT Sans font, coin tokens and attack effects are drawn in code.
 * The Android host only blits our RGBA framebuffer.
 */

#include "game.h"
#include "font.h"

#include <math.h>
#include <stdio.h>
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
/* Real, anti-aliased PT Sans font (embedded OFL TrueType).             */
/* ------------------------------------------------------------------ */

static void draw_text(int x, int y, int s, uint32_t c, const char *str) {
    font_draw(FB, GAME_W, GAME_H, x, y, s, c, str);
}

static int text_w(int s, const char *str) { return font_width(s, str); }

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
_Static_assert(ROWS * COLS == GAME_GARDEN_CELLS, "garden save size mismatch");
#define LAWN_X 120
#define LAWN_Y 150
#define CELL_W 120
#define CELL_H 108
#define CARD_X 260
#define CARD_STEP 160
#define CARD_W 150
#define BOOK_ENTRY_Y 173
#define BOOK_ENTRY_STEP 121
#define BOOK_ENTRY_H 112

#define ZMAX 80
#define PEAMAX 200
#define COINMAX 80
#define PARTMAX 400
#define COIN_VALUE 25
#define COIN_INTERVAL 8.0f
#define MAX_LEVEL 10
#define JUMPER_PUSH (3.0f * CELL_W) /* contact sends any attacker three tiles back */

typedef enum { PH_MENU = GAME_MENU, PH_INTRO = GAME_INTRO, PH_PLAY = GAME_PLAY,
               PH_LEVEL_CLEAR = GAME_LEVEL_CLEAR, PH_WIN = GAME_WIN,
               PH_LOSE = GAME_LOSE, PH_GARDEN = GAME_GARDEN,
               PH_BOOK = GAME_BOOK, PH_SELECT = GAME_SELECT } Phase;

/* Only plants the author drew are selectable, on the lawn and in the garden.
 * Keep existing IDs stable: they are stored in the legacy Zen Garden file. */
enum { PT_NONE = -1, PT_PEA = 0, PT_WALL, PT_SUNFLOWER, PT_JUMPER, PT_COUNT };
_Static_assert(PT_JUMPER == 3, "existing garden plant IDs must not change");
enum { EN_DUCK = 0, EN_ROBOT };

typedef struct {
    int cost, hp;
    float recharge;
    uint32_t body;
    int sprite;
    const char *name, *short_name, *description, *detail;
} PlantDef;

static const PlantDef PDEF[PT_COUNT] = {
    [PT_PEA] = { 100, 300, 7.5f, COL(70, 170, 70), SPR_PEA,
                 "ГОРОХОСТРЕЛ", "ГОРОХОСТРЕЛ",
                 "СТРЕЛЯЕТ ПО УТКАМ В СВОЁМ РЯДУ.", "УРОН: 20 ЗА ПОПАДАНИЕ." },
    [PT_WALL] = { 50, 4000, 30.0f, COL(180, 120, 70), SPR_WALL,
                  "ОРЕХ", "ОРЕХ",
                  "ПРОЧНАЯ ПРЕГРАДА НА ПУТИ УТОК.", "ПОМОГАЕТ ДРУГИМ РАСТЕНИЯМ." },
    [PT_SUNFLOWER] = { 50, 300, 7.5f, COL(70, 95, 220), SPR_SUNFLOWER,
                       "ПОДСОЛНУХ-НАРКОМАН", "ПОДСОЛНУХ",
                       "СОЗДАЁТ МОНЕТЫ ДЛЯ ПОКУПКИ РАСТЕНИЙ.",
                       "НЕ СТРЕЛЯЕТ И НЕ ЗАМОРАЖИВАЕТ." },
    [PT_JUMPER] = { 250, 300, 20.0f, COL(190, 49, 214), SPR_JUMPER,
                    "ДЖАМПЕР-БОЕЦ", "ДЖАМПЕР",
                    "ПРИ УДАРЕ ИСЧЕЗАЕТ.",
                    "ОТБРАСЫВАЕТ УТКУ И РОБОТА НА 3 КЛЕТКИ." },
};

typedef struct { int type; float hp; float fire_t; float sway; } Plant;
typedef struct { int active; int row; float x; float hp; float maxhp; int type;
                 float speed; int eating; float anim; } Zombie;
typedef struct { int active; int row; float x; float y; int dmg; } Pea;
typedef struct { int active; float x; float y; float vy; float target_y; float life; float bob; } Coin;
typedef struct { int active; float x, y, vx, vy; float life; float maxlife; uint32_t col; } Part;
typedef struct { int used; int running; float x; } Mower;

static Phase phase;
static int level;                     /* 1..10 */
static unsigned completed_mask;       /* one bit per actually survived level */
static int resume_level;              /* Continue starts/resumes this level */
static int saved_battle;              /* 1 = continue exact in-progress board */
static int boss_phase;                /* queen arrives after all wave ducks die */
static int intro_replay;              /* level 0 returns to select without wiping progress */
static int intro_step;                /* crying, Dima speaks, Kirill speaks */
static float intro_t;
static int coin_balance;
static int selected;
static float cooldown[PT_COUNT];
static int garden[ROWS][COLS];
static int garden_selected;            /* -1 nothing, PT_COUNT eraser */
static int book_selected;
static Phase book_return;
static float spawn_t;
static int to_spawn;
static int total_zombies;
static float banner_t;
static float global_t;
static int boss_spawned;
static const char *banner_text;

static const char *LEVEL_NAMES[10] = {
    "ПЕРВАЯ ЗАЩИТА", "НОВАЯ ВОЛНА", "У ЗАБОРА", "НА ПОДСТУПАХ",
    "СЕРЕДИНА ПУТИ", "СЛОЖНЕЕ И СЛОЖНЕЕ", "ДЕРЖИ ОБОРОНУ",
    "ПОСЛЕДНИЙ РУБЕЖ", "ПЕРЕД БУРЕЙ", "КОРОЛЕВА БЛИЗКО"
};

static Plant grid[ROWS][COLS];
static Zombie zomb[ZMAX];
static Pea peas[PEAMAX];
static Coin coins[COINMAX];
static Part parts[PARTMAX];
static Mower mower[ROWS];

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

/* A simple game token, not a new character or a substitute for any PNG. */
static void draw_coin_icon(int cx, int cy, int r) {
    disc(cx + 2, cy + 3, r + 1, COL(90, 61, 25));
    disc(cx, cy, r, COL(163, 102, 26));
    disc(cx, cy, r - 3, COL(247, 184, 48));
    disc(cx, cy, r - 7, COL(255, 224, 103));
    int size = r >= 24 ? 4 : 3;
    draw_text_c(cx, cy - 7 * size / 2, size, COL(144, 85, 23), "М");
}

static void draw_plant(int cx, int cy, int type, float sway) {
    if (type < 0 || type >= PT_COUNT) return;
    /* Each selectable plant is Kirill's own picture; no stand-ins. */
    ellipse(cx, cy + 32, 34, 7, COL(47, 112, 30));
    sprite_draw(PDEF[type].sprite, cx - 43, cy - 48 + (int)(sway * 2), 86, 86, 0);
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
            z->eating = 0; z->anim = rndf() * 6.28f;
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
            z->x = GAME_W - 140; /* visible at the right edge for the final fight */
            z->type = EN_ROBOT;
            z->hp = z->maxhp = 5500;
            z->speed = 10;             /* much slower than the ordinary ducks */
            z->eating = 0; z->anim = 0;
            return 1;
        }
    return 0;
}

/* Only the author's coin-making sunflower calls this; no sky sun/coins. */
static void spawn_coin(float x, float y) {
    for (int i = 0; i < COINMAX; i++)
        if (!coins[i].active) {
            Coin *coin = &coins[i];
            coin->active = 1; coin->x = x; coin->y = y;
            coin->bob = rndf() * 6.28f;
            coin->vy = 80; coin->target_y = y + 58;
            coin->life = 16.0f;
            return;
        }
}

static void spawn_pea(int row, int x, int dmg) {
    for (int i = 0; i < PEAMAX; i++)
        if (!peas[i].active) {
            peas[i].active = 1; peas[i].row = row;
            peas[i].x = (float)x; peas[i].y = CELL_CY(row) - 6;
            peas[i].dmg = dmg;
            return;
        }
}

/* ------------------------------------------------------------------ */
/* reset                                                              */
/* ------------------------------------------------------------------ */

static void start_level(int n) {
    level = n;
    resume_level = n;
    saved_battle = 1;
    boss_phase = 0;
    memset(grid, 0, sizeof(grid));
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) grid[r][c].type = PT_NONE;
    memset(zomb, 0, sizeof(zomb));
    memset(peas, 0, sizeof(peas));
    memset(coins, 0, sizeof(coins));
    memset(parts, 0, sizeof(parts));
    for (int r = 0; r < ROWS; r++) { mower[r].used = 0; mower[r].running = 0; mower[r].x = LAWN_X - 30; }
    for (int i = 0; i < PT_COUNT; i++) cooldown[i] = 0;
    /* Starter coins buy the first sunflower; after that it makes the income. */
    coin_balance = 250 + (n - 1) * 30;
    selected = -1;
    total_zombies = 5 + n * 3;
    to_spawn = total_zombies;
    spawn_t = 11.0f - n * 0.2f;
    banner_t = n == 1 ? 6 : 4;
    boss_spawned = 0;
    banner_text = n == 1 ? "ПОДСОЛНУХ-НАРКОМАН ДАЁТ МОНЕТЫ" : LEVEL_NAMES[n - 1];
    phase = PH_PLAY;
}

void game_init(void) {
    init_sprites();
    font_init();
    RNG = 0xC0FFEE11u;
    global_t = 0;
    intro_step = 0;
    intro_t = 0;
    intro_replay = 0;
    completed_mask = 0;
    resume_level = 1;
    start_level(1);
    saved_battle = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) garden[r][c] = PT_NONE;
    garden_selected = -1;
    book_selected = PT_PEA;
    book_return = PH_MENU;
    phase = PH_MENU;
}

void game_debug_snapshot(void) {
    game_init();
    start_level(10);
    /* A populated scene showing ONLY Kirill's plants, duck and robot. */
    coin_balance = 420;
    grid[2][1].type = PT_PEA;  grid[2][1].hp = PDEF[PT_PEA].hp;
    grid[2][2].type = PT_PEA;  grid[2][2].hp = PDEF[PT_PEA].hp;
    grid[1][2].type = PT_PEA;  grid[1][2].hp = PDEF[PT_PEA].hp;
    grid[3][2].type = PT_WALL; grid[3][2].hp = PDEF[PT_WALL].hp;
    grid[0][3].type = PT_SUNFLOWER; grid[0][3].hp = PDEF[PT_SUNFLOWER].hp;
    grid[0][3].fire_t = COIN_INTERVAL;
    grid[4][1].type = PT_SUNFLOWER; grid[4][1].hp = PDEF[PT_SUNFLOWER].hp;
    grid[4][1].fire_t = COIN_INTERVAL;
    grid[2][5].type = PT_JUMPER; grid[2][5].hp = PDEF[PT_JUMPER].hp;
    spawn_zombie(); zomb[0].x = 980; zomb[0].row = 4;
    spawn_zombie(); zomb[1].x = 1100; zomb[1].row = 0;
    spawn_boss();   zomb[2].x = 1020; boss_spawned = 1;
    boss_phase = 1; to_spawn = 0;
    banner_text = "КОРОЛЕВА В РОБОТЕ!"; banner_t = 5;
    spawn_pea(2, 400, 20);
    spawn_pea(1, 520, 20);
    spawn_coin(CELL_CX(1) + 26, CELL_CY(4) - 30); coins[0].y = coins[0].target_y;
}

/* ------------------------------------------------------------------ */
/* update                                                             */
/* ------------------------------------------------------------------ */

/* One contact consumes the author's Jumper Fighter. It does not invent a
 * projectile or a new enemy type: the duck (or the queen's robot) is shoved
 * three planting cells away from the house without being killed. */
static void jumper_hit(Zombie *z, int row, int col) {
    grid[row][col].type = PT_NONE;
    grid[row][col].hp = 0;
    z->x += JUMPER_PUSH;
    z->eating = 0;
    burst(CELL_CX(col), CELL_CY(row), 20, PDEF[PT_JUMPER].body, 160);
}

static void update_play(float dt) {
    global_t += dt;
    /* Spawn cadence shapes the wave, but time never decides victory: all
     * ordinary enemies must be defeated, then the level-10 robot as well. */
    for (int i = 0; i < PT_COUNT; i++)
        if (cooldown[i] > 0) cooldown[i] -= dt;

    /* Only the author's duck spawns before the robot finale. */
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

    /* Only the peashooter attacks; Kirill's sunflower produces coins. */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Plant *p = &grid[r][c];
            if (p->type < 0) continue;
            if (p->hp <= 0) {
                p->type = PT_NONE;
                burst(CELL_CX(c), CELL_CY(r), 12, COL(80, 160, 70), 120);
                continue;
            }
            p->sway = sinf(global_t * 2 + r + c) * 1.2f;
            if (p->type == PT_PEA) {
                int target = 0;
                for (int i = 0; i < ZMAX; i++)
                    if (zomb[i].active && zomb[i].x > CELL_CX(c) &&
                        (zomb[i].row == r || (zomb[i].type == EN_ROBOT &&
                         abs(zomb[i].row - r) <= 1))) { target = 1; break; }
                p->fire_t -= dt;
                if (target && p->fire_t <= 0) {
                    spawn_pea(r, CELL_CX(c) + 16, 20);
                    p->fire_t = 1.4f;
                }
            } else if (p->type == PT_SUNFLOWER) {
                p->fire_t -= dt;
                if (p->fire_t <= 0) {
                    spawn_coin(CELL_CX(c) + 26, CELL_CY(r) - 30);
                    p->fire_t = COIN_INTERVAL;
                }
            }
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
                burst(pe->x, pe->y, 5, COL(120, 200, 90), 90);
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
        z->anim += dt * (z->type == EN_ROBOT ? 2 : 8);
        float spd = z->speed;

        if (z->type == EN_ROBOT) {
            z->x -= spd * dt;          /* never stops to eat: the mech TRAMPLES */
            int repelled = 0;
            for (int r = z->row - 1; r <= z->row + 1 && !repelled; r++) {
                if (r < 0 || r >= ROWS) continue;
                for (int c = 0; c < COLS; c++)
                    if (grid[r][c].type == PT_JUMPER && fabsf(z->x - CELL_CX(c)) < 78) {
                        jumper_hit(z, r, c);
                        repelled = 1;
                        break;
                    }
            }
            if (repelled) continue; /* stop the charge before it tramples */
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
            if (z->x < LAWN_X - 35) { saved_battle = 0; phase = PH_LOSE; return; }
            continue;
        }

        if (z->x <= LAWN_X) { /* reached the lawn edge -> mower / lose */
            if (!mower[z->row].used) {
                mower[z->row].used = 1; mower[z->row].running = 1;
            } else if (z->x < LAWN_X - 25) {
                saved_battle = 0;
                phase = PH_LOSE;
                burst(z->x, CELL_CY(z->row), 30, COL(200, 60, 60), 160);
                return;
            }
        }

        int col = (int)((z->x - LAWN_X) / CELL_W);
        if (col < 0) col = 0;
        if (col >= COLS) col = COLS - 1;
        Plant *p = &grid[z->row][col];
        if (p->type == PT_JUMPER && z->x <= CELL_CX(col) + 28) {
            jumper_hit(z, z->row, col);
            continue;
        }
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

    /* Coins created by Kirill's sunflowers settle beside their plant. */
    for (int i = 0; i < COINMAX; i++) {
        Coin *coin = &coins[i];
        if (!coin->active) continue;
        coin->life -= dt;
        if (coin->life <= 0) { coin->active = 0; continue; }
        coin->bob += dt * 3;
        if (coin->y < coin->target_y)
            coin->y = fminf(coin->target_y, coin->y + coin->vy * dt);
    }

    update_parts(dt);

    /* A wave ends only when every scheduled duck has spawned AND all of them
     * have been defeated (including mower kills in this frame). The robot is
     * revealed only after the entire tenth wave; defeating it wins the game. */
    int alive = 0;
    for (int i = 0; i < ZMAX; i++) if (zomb[i].active) alive++;
    if (to_spawn == 0 && alive == 0) {
        if (level == MAX_LEVEL && !boss_phase) {
            if (spawn_boss()) {
                boss_phase = 1;
                boss_spawned = 1;
                banner_text = "КОРОЛЕВА В РОБОТЕ!";
                banner_t = 5;
            }
        } else {
            completed_mask |= 1u << (level - 1);
            resume_level = level == MAX_LEVEL ? MAX_LEVEL : level + 1;
            saved_battle = 0;
            phase = level == MAX_LEVEL ? PH_WIN : PH_LEVEL_CLEAR;
        }
    }
}

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */

static int inside(int x, int y, int x0, int y0, int x1, int y1) {
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

/* Level 0 is repeatable. Rewatching from the selector must NOT discard a
 * saved battle or mark any level completed; the first story run leads to 1. */
static void finish_intro(void) {
    if (intro_replay) phase = PH_SELECT;
    else start_level(1);
}

static void start_intro(int replay) {
    intro_replay = replay;
    intro_step = 0;
    intro_t = 0;
    phase = PH_INTRO;
}

static void advance_intro(void) {
    intro_step++;
    intro_t = 0;
    if (intro_step >= 3) finish_intro();
}

static void open_book(void) {
    book_return = phase;
    phase = PH_BOOK; /* the level is paused while Kirill's book is open */
}

void game_input_press(int x, int y) {
    if (x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) return;
    if (phase == PH_MENU) {
        if (inside(x, y, 440, 490, 840, 600)) {
            if (saved_battle) phase = PH_PLAY;
            else if (completed_mask == 0 && resume_level == 1) {
                start_intro(0);
            } else start_level(resume_level);
        } else if (inside(x, y, 175, 606, 475, 672)) {
            phase = PH_SELECT;
        } else if (inside(x, y, 490, 606, 790, 672)) {
            garden_selected = -1;
            phase = PH_GARDEN;
        } else if (inside(x, y, 805, 606, 1105, 672)) open_book();
        return;
    }
    if (phase == PH_SELECT) {
        if (inside(x, y, 1045, 18, 1265, 85)) { phase = PH_MENU; return; }
        if (inside(x, y, 425, 567, 855, 641)) { start_intro(1); return; }
        for (int n = 1; n <= MAX_LEVEL; n++) {
            int col = (n - 1) % 5, row = (n - 1) / 5;
            if (inside(x, y, 100 + col * 220, 210 + row * 190,
                       280 + col * 220, 340 + row * 190)) {
                start_level(n);
                return;
            }
        }
        return;
    }
    if (phase == PH_INTRO) {
        if (inside(x, y, 1020, 10, 1270, 92)) finish_intro(); /* skip */
        else advance_intro();                                  /* next line */
        return;
    }
    if (phase == PH_BOOK) {
        if (inside(x, y, 1045, 18, 1265, 85)) { phase = book_return; return; }
        for (int i = 0; i < PT_COUNT; i++)
            if (inside(x, y, 160, BOOK_ENTRY_Y + i * BOOK_ENTRY_STEP,
                       540, BOOK_ENTRY_Y + i * BOOK_ENTRY_STEP + BOOK_ENTRY_H)) {
                book_selected = i;
                return;
            }
        return;
    }
    if (phase == PH_GARDEN) {
        if (inside(x, y, 915, 40, 1070, 87)) { open_book(); return; }
        if (inside(x, y, 1080, 40, 1265, 87)) { phase = PH_MENU; return; }
        if (inside(x, y, 915, 97, 1265, 137)) { garden_selected = PT_COUNT; return; }
        if (y >= 46 && y <= 132)
            for (int i = 0; i < PT_COUNT; i++) {
                int x0 = CARD_X + i * CARD_STEP;
                if (x >= x0 && x <= x0 + CARD_W) {
                    garden_selected = i;
                    return;
                }
            }
        if (x >= LAWN_X && x < LAWN_X + COLS * CELL_W &&
            y >= LAWN_Y && y < LAWN_Y + ROWS * CELL_H) {
            int c = (x - LAWN_X) / CELL_W, r = (y - LAWN_Y) / CELL_H;
            if (garden_selected == PT_COUNT) garden[r][c] = PT_NONE;
            else if (garden_selected >= 0) garden[r][c] = garden_selected;
        }
        return;
    }
    if (phase == PH_LEVEL_CLEAR) {
        if (inside(x, y, 390, 450, 890, 570)) start_level(level + 1);
        else if (inside(x, y, 480, 580, 800, 650)) phase = PH_MENU;
        return;
    }
    if (phase == PH_WIN) {
        if (inside(x, y, 425, 460, 855, 590)) phase = PH_MENU;
        return;
    }
    if (phase == PH_LOSE) {
        if (inside(x, y, 335, 460, 645, 570)) start_level(level); /* retry */
        else if (inside(x, y, 660, 460, 975, 570)) phase = PH_MENU;
        return;
    }
    if (inside(x, y, 1140, 95, 1270, 140)) { phase = PH_MENU; return; }
    if (inside(x, y, 900, 95, 1030, 140)) { open_book(); return; }

    /* Tapping a pile of sunflower coins collects all the overlapping tokens. */
    int collected = 0;
    for (int i = COINMAX - 1; i >= 0; i--) {
        Coin *coin = &coins[i];
        if (!coin->active) continue;
        int dx = x - (int)coin->x, dy = y - (int)coin->y;
        if (dx * dx + dy * dy < 40 * 40) {
            coin->active = 0;
            coin_balance += COIN_VALUE;
            burst(coin->x, coin->y, 8, COL(247, 190, 55), 100);
            collected = 1;
        }
    }
    if (collected) return;

    /* Only packets backed by the author's drawings can be selected. */
    if (y >= 16 && y <= 132) {
        for (int i = 0; i < PT_COUNT; i++) {
            int x0 = CARD_X + i * CARD_STEP;
            if (x >= x0 && x <= x0 + CARD_W) {
                selected = (coin_balance >= PDEF[i].cost && cooldown[i] <= 0) ? i : -1;
                return;
            }
        }
    }

    /* plant on the lawn */
    if (selected >= 0 && x >= LAWN_X && x < LAWN_X + COLS * CELL_W &&
        y >= LAWN_Y && y < LAWN_Y + ROWS * CELL_H) {
        int c = (x - LAWN_X) / CELL_W, r = (y - LAWN_Y) / CELL_H;
        if (grid[r][c].type < 0 && coin_balance >= PDEF[selected].cost && cooldown[selected] <= 0) {
            grid[r][c].type = selected;
            grid[r][c].hp = (float)PDEF[selected].hp;
            grid[r][c].fire_t = selected == PT_SUNFLOWER ? 5.0f : 0.4f;
            coin_balance -= PDEF[selected].cost;
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

int game_wave_remaining(void) {
    int remaining = to_spawn;
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].active && zomb[i].type == EN_DUCK) remaining++;
    return remaining;
}

int game_wave_total(void) { return total_zombies; }

static void draw_seed_bar(void) {
    if (sprite_pixels[SPR_MAP])
        sprite_crop(SPR_MAP, 0, 0, GAME_W, 140, 0, 0, 242, 135, 0);
    else rect(0, 0, GAME_W - 1, 140, COL(96, 64, 40));
    rect_blend(0, 0, GAME_W - 1, 140, COL(31, 25, 30), 105);
    rect(0, 140, GAME_W - 1, 146, COL(70, 46, 28));
    draw_coin_icon(64, 70, 26);
    draw_text(105, 25, 2, COL(255, 223, 131), "МОНЕТЫ");
    draw_int(105, 53, 5, COL(250, 250, 250), coin_balance);
    /* Four illustrated packets; the blue sunflower still makes coins. */
    for (int i = 0; i < PT_COUNT; i++) {
        int x0 = CARD_X + i * CARD_STEP;
        int affordable = coin_balance >= PDEF[i].cost && cooldown[i] <= 0;
        uint32_t border = selected == i ? COL(255, 234, 70) : COL(65, 44, 31);
        rect(x0 - 3, 15, x0 + CARD_W + 3, 135, border);
        rect(x0, 18, x0 + CARD_W, 132, COL(236, 224, 188));
        rect(x0, 18, x0 + CARD_W, 24, PDEF[i].body);
        sprite_draw(PDEF[i].sprite, x0 + (CARD_W - 68) / 2, 26, 68, 68, 0);
        draw_text_c(x0 + CARD_W / 2, 95, 2, COL(64, 42, 29), PDEF[i].short_name);
        draw_int(x0 + CARD_W / 2 - 13, 115, 2, COL(64, 42, 29), PDEF[i].cost);
        if (!affordable) rect_blend(x0, 18, x0 + CARD_W, 132, COL(10, 10, 25), 115);
        if (cooldown[i] > 0) {
            float frac = cooldown[i] / PDEF[i].recharge;
            rect_blend(x0, 18, x0 + CARD_W, 18 + (int)(114 * frac), COL(10, 10, 20), 115);
        }
    }
    /* The book pauses play without losing the current level. */
    rect(900, 95, 1030, 139, COL(92, 66, 44));
    rect(903, 98, 1027, 136, COL(176, 133, 82));
    draw_text_c(965, 107, 3, COL(255, 247, 217), "КНИГА");
    /* Defeated opponents advance the wave, not the clock. Robot health
     * replaces wave progress only after all of level ten's ducks are gone. */
    int defeated = total_zombies - game_wave_remaining();
    if (defeated < 0) defeated = 0;
    if (defeated > total_zombies) defeated = total_zombies;
    int bar = total_zombies ? defeated * 188 / total_zombies : 0;
    char progress[24];
    snprintf(progress, sizeof(progress), "%d/%d", defeated, total_zombies);
    if (boss_phase) {
        for (int i = 0; i < ZMAX; i++)
            if (zomb[i].active && zomb[i].type == EN_ROBOT) {
                float health = fmaxf(0, fminf(1, zomb[i].hp / zomb[i].maxhp));
                bar = (int)(188 * health);
                snprintf(progress, sizeof(progress), "%d%%", (int)ceilf(100 * health));
                break;
            }
    }
    draw_text(1055, 9, 3, COL(255, 230, 175), boss_phase ? "РОБОТ" : "ВОЛНА");
    draw_text(1190, 12, 2, COL(255, 245, 165), progress);
    rect(1055, 43, 1245, 58, COL(42, 29, 25));
    if (bar > 0) rect(1056, 44, 1056 + bar, 57,
                      boss_phase ? COL(210, 72, 76) : COL(244, 139, 62));
    draw_text(1055, 70, 3, COL(245, 240, 211), "УРОВЕНЬ");
    draw_int(1210, 70, 3, COL(255, 245, 165), level);
    rect(1140, 99, 1265, 137, COL(55, 49, 58));
    rect(1143, 102, 1262, 134, COL(108, 84, 64));
    draw_text_c(1202, 108, 3, COL(255, 242, 210), "МЕНЮ");
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
    if (saved_battle || completed_mask != 0) {
        char status[96];
        snprintf(status, sizeof(status), "ПРОЙДЕНО %d / 10. ИГРАТЬ: УРОВЕНЬ %d",
                 game_completed_level(), resume_level);
        rect_blend(430, 445, 850, 482, COL(17, 26, 34), 220);
        draw_text_c(640, 450, 2, COL(255, 231, 170), status);
    }
    draw_button(440, 490, 840, 600, "ИГРАТЬ", 8);
    draw_button(175, 606, 475, 672, "УРОВНИ", 4);
    draw_button(490, 606, 790, 672, "САД ДЗЕН", 4);
    draw_button(805, 606, 1105, 672, "КНИГА", 4);
    draw_text_c(640, 689, 2, COL(255, 242, 204),
                "АВТОСОХРАНЕНИЕ. ВСЕ РАСТЕНИЯ СОЗДАЛ КИРИЛЛ.");
}

/* Level 0 replays the story; any of the ten waves can also be replayed.
 * Completed stages are green; the last in-progress battle is gold. */
static void draw_level_select(void) {
    draw_background();
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(11, 20, 31), 200);
    draw_text_c(630, 32, 6, COL(255, 226, 159), "ВЫБОР УРОВНЯ");
    draw_button(1045, 18, 1265, 85, "НАЗАД", 4);
    char status[64];
    snprintf(status, sizeof(status), "ПРОЙДЕНО: %d / 10", game_completed_level());
    draw_text_c(640, 121, 3, COL(248, 236, 204), status);
    for (int n = 1; n <= MAX_LEVEL; n++) {
        int col = (n - 1) % 5, row = (n - 1) / 5;
        int x = 100 + col * 220, y = 210 + row * 190;
        uint32_t frame = saved_battle && resume_level == n ? COL(255, 226, 113) :
                         (completed_mask & (1u << (n - 1))) ? COL(96, 180, 102) : COL(158, 129, 96);
        rect(x, y + 5, x + 180, y + 135, COL(15, 26, 30));
        rect(x, y, x + 180, y + 130, frame);
        rect(x + 5, y + 5, x + 175, y + 125, COL(65, 52, 50));
        char label[32];
        snprintf(label, sizeof(label), "УРОВЕНЬ %d", n);
        draw_text_c(x + 90, y + 12, 3, COL(255, 239, 198), label);
        draw_text_c(x + 90, y + 56, 2, COL(249, 214, 154), LEVEL_NAMES[n - 1]);
        if (n == MAX_LEVEL) snprintf(label, sizeof(label), "%d + РОБОТ", 5 + n * 3);
        else snprintf(label, sizeof(label), "%d ВРАГОВ", 5 + n * 3);
        draw_text_c(x + 90, y + 97, 2, COL(238, 233, 212), label);
    }
    draw_text_c(640, 166, 2, COL(255, 231, 191),
                "ВОЛНА ЗАКОНЧИТСЯ, КОГДА ВСЕ ЕЁ ВРАГИ ПОБЕЖДЕНЫ");
    draw_button(425, 567, 855, 641, "УРОВЕНЬ 0: КАТ-СЦЕНА", 4);
    draw_text_c(640, 668, 2, COL(242, 231, 203),
                "ПОВТОР КАТ-СЦЕНЫ НЕ СБРАСЫВАЕТ СОХРАНЕНИЕ");
}

/* Free planting space for every plant Kirill has actually drawn. It is
 * separate from the campaign: garden planting never spends battle coins. */
static void draw_garden(void) {
    draw_background();
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (garden[r][c] >= 0)
                draw_plant(CELL_CX(c), CELL_CY(r), garden[r][c],
                           sinf(global_t * 2 + r + c));

    if (sprite_pixels[SPR_MAP])
        sprite_crop(SPR_MAP, 0, 0, GAME_W, 140, 0, 0, 242, 135, 0);
    else rect(0, 0, GAME_W - 1, 140, COL(96, 64, 40));
    rect_blend(0, 0, GAME_W - 1, 140, COL(31, 25, 30), 105);
    rect(0, 140, GAME_W - 1, 146, COL(70, 46, 28));
    draw_text(28, 12, 4, COL(255, 229, 157), "САД ДЗЕН");
    draw_text(28, 71, 2, COL(255, 241, 193), "СОЗДАНИЯ КИРИЛЛА");

    for (int i = 0; i < PT_COUNT; i++) {
        int x0 = CARD_X + i * CARD_STEP;
        uint32_t border = garden_selected == i ? COL(255, 225, 82) : COL(65, 44, 31);
        rect(x0 - 3, 43, x0 + CARD_W + 3, 135, border);
        rect(x0, 46, x0 + CARD_W, 132, COL(236, 224, 188));
        rect(x0, 46, x0 + CARD_W, 51, PDEF[i].body);
        sprite_draw(PDEF[i].sprite, x0 + (CARD_W - 62) / 2, 51, 62, 62, 0);
        draw_text_c(x0 + CARD_W / 2, 113, 2, COL(64, 42, 29), PDEF[i].short_name);
    }
    draw_button(915, 40, 1070, 87, "КНИГА", 3);
    draw_button(1080, 40, 1265, 87, "В МЕНЮ", 2);
    rect(915, 97, 1265, 137, garden_selected == PT_COUNT ?
         COL(255, 225, 82) : COL(66, 44, 31));
    rect(918, 100, 1262, 134, COL(158, 113, 66));
    draw_text_c(1090, 105, 3, COL(255, 240, 198), "УБРАТЬ");
    rect_blend(95, 679, 1185, 717, COL(13, 29, 22), 220);
    draw_text_c(640, 691, 2, COL(255, 244, 205),
                "ВЫБЕРИ РАСТЕНИЕ И КЛЕТКУ. В САДУ ВСЁ БЕСПЛАТНО.");
}

/* Interactive plant book: the list and details come from the SAME plant
 * definitions as the packets and Zen Garden, so it cannot list fake plants. */
static void draw_book(void) {
    draw_background();
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(13, 23, 34), 170);
    draw_text_c(620, 28, 5, COL(255, 224, 151), "УМНАЯ КНИГА КИРИЛЛА");
    draw_button(1045, 18, 1265, 85, "НАЗАД", 4);
    rect(91, 114, 1189, 687, COL(53, 38, 38));
    rect(99, 121, 1181, 680, COL(223, 199, 151));
    rect(108, 129, 1172, 671, COL(246, 232, 195));
    rect(550, 128, 560, 671, COL(144, 103, 75));
    draw_text(160, 135, 3, COL(93, 57, 39), "РАСТЕНИЯ КИРИЛЛА");

    for (int i = 0; i < PT_COUNT; i++) {
        int y0 = BOOK_ENTRY_Y + i * BOOK_ENTRY_STEP;
        rect(160, y0, 540, y0 + BOOK_ENTRY_H,
             book_selected == i ? COL(184, 119, 56) : COL(166, 143, 111));
        rect(165, y0 + 5, 535, y0 + BOOK_ENTRY_H - 5,
             book_selected == i ? COL(255, 233, 160) : COL(240, 222, 181));
        sprite_draw(PDEF[i].sprite, 180, y0 + 14, 80, 80, 0);
        draw_text(275, y0 + 13, 2, COL(75, 52, 33), PDEF[i].short_name);
        draw_text(275, y0 + 44, 2, COL(100, 70, 39), "ЦЕНА:");
        draw_int(362, y0 + 44, 2, COL(100, 70, 39), PDEF[i].cost);
        draw_text(275, y0 + 78, 2, COL(111, 75, 42),
                  i == PT_PEA ? "АТАКА" : i == PT_WALL ? "ЗАЩИТА" :
                  i == PT_JUMPER ? "ОТБРОС" : "МОНЕТЫ");
    }

    const PlantDef *p = &PDEF[book_selected];
    sprite_draw(p->sprite, 756, 148, 222, 222, 0);
    int title_size = 4;
    while (title_size > 2 && text_w(title_size, p->name) > 565) title_size--;
    draw_text_c(865, 380, title_size, COL(71, 47, 39), p->name);
    draw_text(600, 430, 3, COL(88, 57, 36), "ЦЕНА:");
    draw_int(715, 430, 3, COL(126, 76, 26), p->cost);
    draw_text(845, 430, 3, COL(88, 57, 36), "ЗДОРОВЬЕ:");
    draw_int(1050, 430, 3, COL(126, 76, 26), p->hp);
    draw_text(600, 469, 2, COL(88, 57, 36), "ПЕРЕЗАРЯДКА:");
    int whole = (int)p->recharge;
    draw_int(800, 469, 2, COL(126, 76, 26), whole);
    if (p->recharge > whole) {
        int x = 800 + (whole >= 10 ? 24 : 12);
        draw_text(x, 469, 2, COL(126, 76, 26), ".");
        draw_int(x + 12, 469, 2, COL(126, 76, 26),
                 (int)(p->recharge * 10 + 0.5f) % 10);
    }
    draw_text(865, 469, 2, COL(88, 57, 36), "СЕК.");
    if (book_selected == PT_SUNFLOWER) {
        draw_text(600, 495, 2, COL(88, 57, 36), "МОНЕТЫ: +");
        draw_int(745, 495, 2, COL(126, 76, 26), COIN_VALUE);
        draw_text(787, 495, 2, COL(88, 57, 36), "КАЖДЫЕ");
        draw_int(895, 495, 2, COL(126, 76, 26), (int)COIN_INTERVAL);
        draw_text(925, 495, 2, COL(88, 57, 36), "СЕК.");
    }
    rect(580, 524, 1150, 663, COL(218, 194, 150));
    draw_text(598, 541, 2, COL(78, 52, 33), p->description);
    draw_text(598, 573, 2, COL(78, 52, 33), p->detail);
    draw_text(598, 618, 3, COL(116, 65, 36), "СОЗДАТЕЛЬ: КИРИЛЛ");
}

static void draw_intro(void) {
    draw_background();
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(17, 24, 48), 105);
    rect_blend(0, 0, GAME_W - 1, 115, COL(17, 20, 35), 221);
    draw_text_c(535, 37, 6, COL(255, 224, 157), "УРОВЕНЬ 0: КАТ-СЦЕНА");
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
        if (peas[i].active)
            disc((int)peas[i].x, (int)peas[i].y, 9, COL(120, 210, 90));
    for (int i = 0; i < COINMAX; i++)
        if (coins[i].active &&
            (coins[i].life >= 2.0f || ((int)(coins[i].life * 4) & 1))) {
            int bob = (int)(sinf(coins[i].bob) * 3);
            draw_coin_icon((int)coins[i].x, (int)coins[i].y + bob, 22);
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
        draw_text_c(640, 246, 7, COL(255, 231, 163), "УРОВЕНЬ ПРОЙДЕН!");
        draw_text_c(640, 329, 3, COL(255, 228, 171), "ВСЯ ВОЛНА ПОБЕЖДЕНА!");
        draw_text_c(570, 375, 4, COL(248, 247, 231), "СЛЕДУЮЩИЙ УРОВЕНЬ:");
        draw_int(878, 375, 4, COL(255, 216, 119), level + 1);
        draw_button(390, 450, 890, 565, "ДАЛЬШЕ", 7);
        draw_button(480, 580, 800, 645, "В МЕНЮ", 3);
    } else if (phase == PH_LOSE) {
        draw_text_c(640, 254, 7, COL(255, 202, 161),
                    level == 10 && boss_phase ? "РОБОТ УНИЧТОЖИЛ ВСЕХ!" : "ЗАЩИТА ПРОРВАНА!");
        draw_text_c(640, 361, 4, COL(248, 247, 231), "ПОПРОБУЙ ЕЩЁ РАЗ");
        draw_button(335, 460, 645, 565, "ПОВТОРИТЬ", 4);
        draw_button(660, 460, 975, 565, "В МЕНЮ", 4);
    } else if (phase == PH_WIN) {
        draw_text_c(640, 237, 7, COL(255, 231, 163), "РОБОТ ОСТАНОВЛЕН!");
        draw_text_c(640, 340, 4, COL(248, 247, 231), "ФИНАЛ ПРОЙДЕН. ГУСИ СПАСЕНЫ!");
        draw_button(425, 460, 855, 580, "В МЕНЮ", 6);
    }
}

static void render(void) {
    if (phase == PH_MENU) { draw_menu(); return; }
    if (phase == PH_SELECT) { draw_level_select(); return; }
    if (phase == PH_INTRO) { draw_intro(); return; }
    if (phase == PH_GARDEN) { draw_garden(); return; }
    if (phase == PH_BOOK) { draw_book(); return; }
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
int game_level(void) { return phase == PH_INTRO ? 0 : level; }
int game_completed_level(void) {
    int count = 0;
    for (int n = 0; n < MAX_LEVEL; n++) count += (completed_mask >> n) & 1u;
    return count;
}
int game_resume_level(void) { return resume_level; }

void game_garden_export(uint8_t cells[GAME_GARDEN_CELLS]) {
    if (!cells) return;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            cells[r * COLS + c] = (uint8_t)(garden[r][c] + 1);
}

int game_garden_import(const uint8_t cells[GAME_GARDEN_CELLS]) {
    if (!cells) return 0;
    for (int i = 0; i < GAME_GARDEN_CELLS; i++)
        if (cells[i] > PT_COUNT) return 0; /* reject malformed saves atomically */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            garden[r][c] = (int)cells[r * COLS + c] - 1;
    return 1;
}

/* The garden remains in pvg3-garden.v1. Campaign V3 intentionally has the
 * EXACT SAME byte layout as V1/V2: fourth seed cooldown reuses the former
 * boss countdown field; the first three cooldown slots stay in place. This
 * keeps old in-progress boards and completion data readable when we switch
 * from a timer to a kill-completed wave. No pointers or random bytes persist.
 * All fields are 32-bit integers/floats on the supported little-endian ABIs. */
_Static_assert(sizeof(int) == 4 && sizeof(float) == 4, "save needs 32-bit fields");
typedef struct {
    uint32_t magic, version;
    int completed, resume, active, level;
    float old_level_left, jumper_cooldown; /* V1/V2: level_left, boss_left */
    int boss_phase, boss_spawned, coin_balance, selected;
    int to_spawn, total_zombies;
    float spawn_t, global_t, banner_t;
    uint32_t rng;
    float cooldown[PT_JUMPER]; /* original three plant slots; do not resize */
    Plant grid[ROWS][COLS];
    Zombie zomb[ZMAX];
    Pea peas[PEAMAX];
    Coin coins[COINMAX];
    Mower mower[ROWS];
    uint32_t checksum;
} SaveState;
_Static_assert(sizeof(SaveState) == 9988 &&
               offsetof(SaveState, checksum) == sizeof(SaveState) - 4,
               "Keep the campaign V1/V2 save layout readable on both Android ABIs");

#define SAVE_MAGIC 0x33477650u /* little-endian bytes 'P', 'v', 'G', '3' */
#define SAVE_VERSION 3u

static uint32_t save_checksum(const SaveState *s) {
    const uint8_t *p = (const uint8_t *)s;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(SaveState, checksum); i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

size_t game_save_size(void) { return sizeof(SaveState); }

int game_save_export(void *dst, size_t capacity) {
    if (!dst || capacity < sizeof(SaveState)) return 0;
    SaveState s;
    memset(&s, 0, sizeof(s));
    s.magic = SAVE_MAGIC;
    s.version = SAVE_VERSION;
    s.completed = (int)completed_mask;
    s.resume = resume_level;
    s.active = saved_battle;
    s.level = level;
    if (s.active) {
        s.jumper_cooldown = cooldown[PT_JUMPER];
        s.boss_phase = boss_phase;
        s.boss_spawned = boss_spawned;
        s.coin_balance = coin_balance;
        s.selected = selected;
        s.to_spawn = to_spawn;
        s.total_zombies = total_zombies;
        s.spawn_t = spawn_t;
        s.global_t = global_t;
        s.banner_t = banner_t;
        s.rng = RNG;
        memcpy(s.cooldown, cooldown, sizeof(s.cooldown));
        memcpy(s.grid, grid, sizeof(grid));
        memcpy(s.zomb, zomb, sizeof(zomb));
        memcpy(s.peas, peas, sizeof(peas));
        memcpy(s.coins, coins, sizeof(coins));
        memcpy(s.mower, mower, sizeof(mower));
    }
    s.checksum = save_checksum(&s);
    memcpy(dst, &s, sizeof(s));
    return 1;
}

int game_save_import(const void *src, size_t length) {
    if (!src || length != sizeof(SaveState)) return 0;
    SaveState s;
    memcpy(&s, src, sizeof(s)); /* src need not be suitably aligned */
    if (s.magic != SAVE_MAGIC ||
        (s.version != 1u && s.version != 2u && s.version != SAVE_VERSION) ||
        s.checksum != save_checksum(&s) ||
        s.completed < 0 ||
        (s.version == 1u && s.completed > MAX_LEVEL) ||
        (s.version >= 2u && s.completed >= (1 << MAX_LEVEL)) ||
        s.resume < 1 || s.resume > MAX_LEVEL ||
        (s.active != 0 && s.active != 1) ||
        s.level < 1 || s.level > MAX_LEVEL) return 0;
    if (s.active) {
        if (s.level != s.resume || (s.boss_phase != 0 && s.boss_phase != 1) ||
            (s.boss_spawned != 0 && s.boss_spawned != 1) ||
            (s.boss_phase && (s.level != MAX_LEVEL || !s.boss_spawned)) ||
            (s.version < SAVE_VERSION &&
             (!isfinite(s.old_level_left) || s.old_level_left < 0 ||
              s.old_level_left > 45 + s.level * 5 ||
              !isfinite(s.jumper_cooldown) || s.jumper_cooldown < 0 ||
              s.jumper_cooldown > 100)) ||
            (s.version == SAVE_VERSION &&
             (!isfinite(s.jumper_cooldown) ||
              s.jumper_cooldown > PDEF[PT_JUMPER].recharge + 1)) ||
            s.coin_balance < 0 || s.coin_balance > 1000000 ||
            s.selected < -1 || s.selected >= PT_COUNT ||
            s.total_zombies != 5 + s.level * 3 ||
            s.to_spawn < 0 || s.to_spawn > s.total_zombies ||
            !isfinite(s.spawn_t) || !isfinite(s.global_t) || !isfinite(s.banner_t))
            return 0;
        for (int i = 0; i < PT_JUMPER; i++)
            if (!isfinite(s.cooldown[i]) || s.cooldown[i] > PDEF[i].recharge + 1)
                return 0;
        for (int r = 0; r < ROWS; r++) {
            if ((s.mower[r].used != 0 && s.mower[r].used != 1) ||
                (s.mower[r].running != 0 && s.mower[r].running != 1) ||
                !isfinite(s.mower[r].x)) return 0;
            for (int c = 0; c < COLS; c++) {
                const Plant *p = &s.grid[r][c];
                if (p->type < PT_NONE || p->type >= PT_COUNT ||
                    (p->type != PT_NONE && (!isfinite(p->hp) ||
                      !isfinite(p->fire_t) || !isfinite(p->sway)))) return 0;
            }
        }
        for (int i = 0; i < ZMAX; i++) {
            const Zombie *z = &s.zomb[i];
            if (z->active != 0 && z->active != 1) return 0;
            if (z->active && (z->row < 0 || z->row >= ROWS ||
                z->type < EN_DUCK || z->type > EN_ROBOT ||
                !isfinite(z->x) || !isfinite(z->hp) || !isfinite(z->maxhp) ||
                !isfinite(z->speed) || !isfinite(z->anim))) return 0;
        }
        for (int i = 0; i < PEAMAX; i++) {
            const Pea *p = &s.peas[i];
            if (p->active != 0 && p->active != 1) return 0;
            if (p->active && (p->row < 0 || p->row >= ROWS ||
                !isfinite(p->x) || !isfinite(p->y))) return 0;
        }
        for (int i = 0; i < COINMAX; i++) {
            const Coin *c = &s.coins[i];
            if (c->active != 0 && c->active != 1) return 0;
            if (c->active && (!isfinite(c->x) || !isfinite(c->y) ||
                !isfinite(c->target_y) || !isfinite(c->life) ||
                !isfinite(c->bob) || !isfinite(c->vy))) return 0;
        }
    }
    completed_mask = s.version == 1u ? (1u << s.completed) - 1u : (unsigned)s.completed;
    resume_level = s.resume;
    saved_battle = s.active;
    level = s.active ? s.level : s.resume;
    if (s.active) {
        boss_phase = s.boss_phase;
        boss_spawned = s.boss_spawned;
        coin_balance = s.coin_balance;
        selected = s.selected;
        to_spawn = s.to_spawn;
        total_zombies = s.total_zombies;
        spawn_t = s.spawn_t;
        global_t = s.global_t;
        banner_t = s.banner_t;
        RNG = s.rng;
        memcpy(cooldown, s.cooldown, sizeof(s.cooldown));
        cooldown[PT_JUMPER] = s.version == SAVE_VERSION ? s.jumper_cooldown : 0;
        memcpy(grid, s.grid, sizeof(grid));
        memcpy(zomb, s.zomb, sizeof(zomb));
        memcpy(peas, s.peas, sizeof(peas));
        memcpy(coins, s.coins, sizeof(coins));
        memcpy(mower, s.mower, sizeof(mower));
        memset(parts, 0, sizeof(parts)); /* cosmetic particles do not persist */
        banner_text = boss_phase ? "КОРОЛЕВА В РОБОТЕ!" : LEVEL_NAMES[level - 1];
    }
    book_return = PH_MENU;
    phase = PH_MENU;
    return 1;
}

#ifdef GAME_TEST
void game_debug_finish_wave(void) {
    to_spawn = 0;
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].type != EN_ROBOT) zomb[i].active = 0;
}
void game_debug_spawn_duck(int row, float x) {
    if (row < 0 || row >= ROWS) return;
    for (int i = 0; i < ZMAX; i++)
        if (!zomb[i].active) {
            Zombie *z = &zomb[i];
            memset(z, 0, sizeof(*z));
            z->active = 1; z->type = EN_DUCK; z->row = row; z->x = x;
            z->hp = z->maxhp = 100; z->speed = 25;
            if (to_spawn > 0) to_spawn--;
            return;
        }
}
float game_debug_duck_x(int row) {
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].active && zomb[i].type == EN_DUCK && zomb[i].row == row)
            return zomb[i].x;
    return -1;
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
void game_debug_boss_set_x(float x) {
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].type == EN_ROBOT && zomb[i].active) zomb[i].x = x;
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
int game_debug_coin_balance(void) { return coin_balance; }
int game_debug_coin_count(void) {
    int count = 0;
    for (int i = 0; i < COINMAX; i++) if (coins[i].active) count++;
    return count;
}
float game_debug_cooldown(int plant) {
    return (unsigned)plant < PT_COUNT ? cooldown[plant] : -1;
}
int game_debug_garden_plant_type(int row, int col) {
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS) return PT_NONE;
    return garden[row][col];
}
int game_debug_book_plant(void) { return book_selected; }
int game_debug_level_completed(int n) {
    return n >= 1 && n <= MAX_LEVEL && !!(completed_mask & (1u << (n - 1)));
}
#endif
