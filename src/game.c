/* game.c — lane defence, platform-independent simulation and software renderer.
 * The original PNG artwork is packed into sprites_data.h at build time (see
 * tools/pack_sprites.py). All characters and plants use the author's drawings;
 * the PT Sans font and attack effects are rendered in code; a shared gold
 * coin illustration is used for both the HUD and collectible currency.
 * The Android host only blits our RGBA framebuffer.
 */

#include "game.h"
#include "game_view.h"
#include "font.h"
#include "preferences.h"
#include "online_net.h"
#include "online_particle.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* framebuffer helpers                                                */
/* ------------------------------------------------------------------ */

static uint32_t *FB;
static int use_lvgl_ui;
/* Per-object transparency is scoped around platformer sprite draws. */
static int sprite_alpha_multiplier = 255;

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
_Static_assert((int)PV_ART_BREAD == (int)SPR_KHLEBUSHEK &&
               (int)PV_ART_DUCK_CONE == (int)SPR_DUCK_CONE &&
               (int)PV_ART_DUCK_BUCKET == (int)SPR_DUCK_BUCKET &&
               (int)PV_ART_PEA == (int)SPR_PEA &&
               (int)PV_ART_MOWER == (int)SPR_MOWER &&
               (int)PV_ART_COIN == (int)SPR_COIN &&
               (int)PV_ART_LEVEL_BLOCK == (int)SPR_LEVEL_BLOCK &&
               (int)PV_ART_LEVEL_PLATFORM == (int)SPR_LEVEL_PLATFORM &&
               (int)PV_ART_LEVEL_TRIGGER == (int)SPR_LEVEL_TRIGGER &&
               (int)PV_ART_LEVEL_TRIGGER_ROTATE == (int)SPR_LEVEL_TRIGGER_ROTATE &&
               (int)PV_ART_LEVEL_TRIGGER_FOREVER == (int)SPR_LEVEL_TRIGGER_FOREVER &&
               (int)PV_ART_LEVEL_TRIGGER_INVISIBILITY == (int)SPR_LEVEL_TRIGGER_INVISIBILITY &&
               (int)PV_ART_LEVEL_TRIGGER_NO_COLLISION == (int)SPR_LEVEL_TRIGGER_NO_COLLISION &&
               (int)PV_ART_LEVEL_FLAG == (int)SPR_LEVEL_FLAG &&
               (int)PV_ART_LEVEL_SPIKE == (int)SPR_LEVEL_SPIKE &&
               (int)PV_ART_LEVEL_SLOPE == (int)SPR_LEVEL_SLOPE &&
               (int)PV_ART_LEVEL_TRIGGER_GRAVITY == (int)SPR_LEVEL_TRIGGER_GRAVITY &&
               (int)PV_ART_LEVEL_ORB_ORANGE == (int)SPR_LEVEL_ORB_ORANGE &&
               (int)PV_ART_LEVEL_ORB_YELLOW == (int)SPR_LEVEL_ORB_YELLOW &&
               (int)PV_ART_LEVEL_CHECKPOINT_INACTIVE == (int)SPR_LEVEL_CHECKPOINT_INACTIVE &&
               (int)PV_ART_LEVEL_CHECKPOINT_ACTIVE == (int)SPR_LEVEL_CHECKPOINT_ACTIVE &&
               (int)PV_ART_LEVEL_PORTAL_NORMAL == (int)SPR_LEVEL_PORTAL_NORMAL &&
               (int)PV_ART_LEVEL_PORTAL_JETPACK == (int)SPR_LEVEL_PORTAL_JETPACK &&
               (int)PV_ART_JETPACK_ACTIVE == (int)SPR_JETPACK_ACTIVE &&
               (int)PV_ART_JETPACK_INACTIVE == (int)SPR_JETPACK_INACTIVE &&
               (int)PV_ART_LEVEL_TRIGGER_COLOR == (int)SPR_LEVEL_TRIGGER_COLOR &&
               (int)PV_ART_WORKSHOP_ROTATE == (int)SPR_WORKSHOP_ROTATE &&
               (int)PV_ART_COLOR_WHEEL == (int)SPR_COLOR_WHEEL &&
               (int)PV_ART_COUNT == (int)SPR_COUNT,
               "LVGL art IDs must match the PNG packer");
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
const uint32_t *game_art_rgba(int id, int *width, int *height) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (id < 0 || id >= SPR_COUNT) return NULL;
    init_sprites();
    if (width) *width = SPRITE_DATA[id].w;
    if (height) *height = SPRITE_DATA[id].h;
    return sprite_pixels[id];
}

static uint32_t sprite_tinted_pixel(uint32_t source, uint32_t tint, int opacity) {
    if (opacity <= 0) return source;
    if (opacity > 255) opacity = 255;
    int inverse = 255 - opacity;
    int r = ((source & 255u) * inverse + (tint & 255u) * opacity + 127) / 255;
    int g = (((source >> 8) & 255u) * inverse +
             ((tint >> 8) & 255u) * opacity + 127) / 255;
    int b = (((source >> 16) & 255u) * inverse +
             ((tint >> 16) & 255u) * opacity + 127) / 255;
    return (source & 0xff000000u) | ((uint32_t)b << 16) |
           ((uint32_t)g << 8) | (uint32_t)r;
}
static void sprite_crop_flipped_tinted(int id, int x, int y, int w, int h,
                                       int sx, int sy, int sw, int sh,
                                       int flip_x, int flip_y,
                                       uint32_t tint, int tint_opacity) {
    if (id < 0 || id >= SPR_COUNT || !sprite_pixels[id] || w <= 0 || h <= 0 ||
        sw <= 0 || sh <= 0) return;
    const SpritePacked *sp = &SPRITE_DATA[id];
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > GAME_W ? GAME_W : x + w;
    int y1 = y + h > GAME_H ? GAME_H : y + h;
    for (int dy = y0; dy < y1; dy++) {
        int sample_y = (dy - y) * sh / h;
        int src_y = sy + (flip_y ? sh - 1 - sample_y : sample_y);
        if ((unsigned)src_y >= (unsigned)sp->h) continue;
        const uint32_t *src = sprite_pixels[id] + src_y * sp->w;
        uint32_t *dst = FB + dy * GAME_W;
        for (int dx = x0; dx < x1; dx++) {
            int sample_x = (dx - x) * sw / w;
            int src_x = sx + (flip_x ? sw - 1 - sample_x : sample_x);
            if ((unsigned)src_x >= (unsigned)sp->w) continue;
            uint32_t color = sprite_tinted_pixel(src[src_x], tint, tint_opacity);
            int alpha = (int)(color >> 24);
            alpha = (alpha * sprite_alpha_multiplier + 127) / 255;
            if (alpha == 255) dst[dx] = color;
            else if (alpha) dst[dx] = blend(dst[dx], color, alpha);
        }
    }
}
static void sprite_crop_flipped(int id, int x, int y, int w, int h,
                                int sx, int sy, int sw, int sh,
                                int flip_x, int flip_y) {
    sprite_crop_flipped_tinted(id, x, y, w, h, sx, sy, sw, sh,
                               flip_x, flip_y, 0, 0);
}
static void sprite_crop(int id, int x, int y, int w, int h,
                        int sx, int sy, int sw, int sh, int flip) {
    sprite_crop_flipped(id, x, y, w, h, sx, sy, sw, sh, flip, 0);
}
static void sprite_draw(int id, int x, int y, int w, int h, int flip) {
    sprite_crop(id, x, y, w, h, 0, 0, SPRITE_DATA[id].w,
                SPRITE_DATA[id].h, flip);
}
static void sprite_draw_tinted_flipped(int id, int x, int y, int w, int h,
                                       int flip_x, int flip_y,
                                       uint32_t tint, int tint_opacity) {
    if (id < 0 || id >= SPR_COUNT) return;
    sprite_crop_flipped_tinted(id, x, y, w, h, 0, 0,
        SPRITE_DATA[id].w, SPRITE_DATA[id].h, flip_x, flip_y,
        tint, tint_opacity);
}
static void sprite_draw_rotated_tinted_flipped(int id, int x, int y, int w, int h,
                                               float degrees, int flip_x, int flip_y,
                                               uint32_t tint, int tint_opacity) {
    if (fabsf(degrees) < .01f) {
        sprite_draw_tinted_flipped(id, x, y, w, h, flip_x, flip_y,
                                  tint, tint_opacity);return;
    }
    if (id < 0 || id >= SPR_COUNT || !sprite_pixels[id] || w <= 0 || h <= 0) return;
    const SpritePacked *sp = &SPRITE_DATA[id];
    float radians = degrees * 0.01745329251994329577f;
    float c = cosf(radians), s = sinf(radians);
    float half_w = w * .5f, half_h = h * .5f;
    float cx = x + half_w, cy = y + half_h;
    int bound_w = (int)ceilf(fabsf(w * c) + fabsf(h * s));
    int bound_h = (int)ceilf(fabsf(h * c) + fabsf(w * s));
    int left = (int)floorf(cx - bound_w * .5f);
    int top = (int)floorf(cy - bound_h * .5f);
    int x0 = left < 0 ? 0 : left, y0 = top < 0 ? 0 : top;
    int x1 = left + bound_w > GAME_W ? GAME_W : left + bound_w;
    int y1 = top + bound_h > GAME_H ? GAME_H : top + bound_h;
    for (int py = y0; py < y1; ++py) for (int px = x0; px < x1; ++px) {
        float dx = px + .5f - cx, dy = py + .5f - cy;
        float local_x = c * dx + s * dy;
        float local_y = -s * dx + c * dy;
        if (flip_x) local_x = -local_x;
        if (flip_y) local_y = -local_y;
        if (local_x < -half_w || local_x >= half_w ||
            local_y < -half_h || local_y >= half_h) continue;
        int sx = (int)((local_x + half_w) * sp->w / w);
        int sy = (int)((local_y + half_h) * sp->h / h);
        if ((unsigned)sx >= (unsigned)sp->w || (unsigned)sy >= (unsigned)sp->h) continue;
        uint32_t color = sprite_tinted_pixel(
            sprite_pixels[id][sy * sp->w + sx], tint, tint_opacity);
        int alpha = (int)(color >> 24);
        alpha = (alpha * sprite_alpha_multiplier + 127) / 255;
        uint32_t *dst = FB + py * GAME_W + px;
        if (alpha == 255) *dst = color;
        else if (alpha) *dst = blend(*dst, color, alpha);
    }
}
static void sprite_draw_flipped(int id, int x, int y, int w, int h,
                                int flip_x, int flip_y) {
    if (id < 0 || id >= SPR_COUNT) return;
    sprite_crop_flipped(id, x, y, w, h, 0, 0,
        SPRITE_DATA[id].w, SPRITE_DATA[id].h, flip_x, flip_y);
}
static void sprite_draw_rotated_flipped(int id, int x, int y, int w, int h,
                                        float degrees, int flip_x, int flip_y) {
    sprite_draw_rotated_tinted_flipped(id, x, y, w, h, degrees,
                                       flip_x, flip_y, 0, 0);
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
/* The lawn leaves room for the reference's vertical packet rack at left.
 * Cell counts stay 9x5; older saves are converted when they are imported. */
#define LAWN_X 250
#define LAWN_Y 120
#define CELL_W 114
#define CELL_H 112
#define CARD_X 260             /* five horizontal packets in the Zen Garden */
#define CARD_STEP 130
#define CARD_W 120
#define BATTLE_CARD_X 20
#define BATTLE_CARD_Y 137
#define BATTLE_CARD_STEP 107
#define BATTLE_CARD_W 210
#define BATTLE_CARD_H 100
#define BOOK_ENTRY_Y 160
#define BOOK_ENTRY_STEP 99
#define BOOK_ENTRY_H 91
#define WATER_LEVEL 5
#define WATER_FIRST_ROW 1
#define WATER_LAST_ROW 2

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
               PH_BOOK = GAME_BOOK, PH_SELECT = GAME_SELECT,
               PH_ONLINE_ROOMS = GAME_ONLINE_ROOMS,
               PH_ONLINE_LOBBY = GAME_ONLINE_LOBBY,
               PH_ONLINE_MATCH = GAME_ONLINE_MATCH,
               PH_CUSTOM_LEVELS = GAME_CUSTOM_LEVELS,
               PH_CUSTOM_PLAY = GAME_CUSTOM_PLAY,
               PH_WORKSHOP = GAME_WORKSHOP,
               PH_WORKSHOP_DETAILS = GAME_WORKSHOP_DETAILS,
               PH_WORKSHOP_EDIT = GAME_WORKSHOP_EDIT } Phase;

/* Only plants the author drew are selectable, on the lawn and in the garden.
 * Keep existing IDs stable: they are stored in the legacy Zen Garden file. */
enum { PT_NONE = -1, PT_PEA = 0, PT_WALL, PT_SUNFLOWER, PT_JUMPER,
       PT_LILY, PT_COUNT };
_Static_assert(PT_JUMPER == 3 && PT_LILY == 4,
               "existing garden plant IDs must not change");
/* Save IDs 0/1 remain the original duck and final robot; the cone and
 * bucket variants keep stable IDs 2/3 so existing campaign saves still load. */
enum { EN_DUCK = 0, EN_ROBOT = 1, EN_CONE = 2, EN_BUCKET = 3, EN_COUNT };
static const int EN_BASE_HP[EN_COUNT] = { 180, 5500, 420, 750 };
static const int EN_ONLINE_COST[EN_COUNT] = { 50, 0, 100, 175 };
static const char *const EN_NAMES[EN_COUNT] = {
    "УТКА-ЗОМБИ", "РОБОТ КОРОЛЕВЫ", "УТКА С КОНУСОМ", "УТКА С ВЕДРОМ"
};
static const int BOOK_ENEMIES[] = { EN_DUCK, EN_CONE, EN_BUCKET, EN_ROBOT };
#define BOOK_ENEMY_COUNT 4
#define GARDEN_DUCK_COUNT 3
static const int GARDEN_DUCKS[GARDEN_DUCK_COUNT] = {
    EN_DUCK, EN_CONE, EN_BUCKET
};
static const char *const GARDEN_DUCK_NAMES[GARDEN_DUCK_COUNT] = {
    "ГУСЬ", "КОНУС", "ВЕДРО"
};

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
    [PT_LILY] = { 25, 300, 7.5f, COL(80, 192, 23), SPR_LILY,
                  "КУВШИНКА", "КУВШИНКА",
                  "РАСТЁТ В ВОДЕ: ОСНОВА ДЛЯ ПОСАДКИ.",
                  "ДРУГИЕ РАСТЕНИЯ СТАВЯТСЯ ПОВЕРХ НЕЁ." },
};

int game_book_entry(int enemy_tab, int index, GameBookEntry *out) {
    if (!out || index < 0 || index >= (enemy_tab ? BOOK_ENEMY_COUNT : PT_COUNT))
        return 0;
    memset(out, 0, sizeof *out);
    if (!enemy_tab) {
        const PlantDef *p = &PDEF[index];
        out->name = p->name;out->short_name = p->short_name;
        out->description = p->description;out->detail = p->detail;
        out->art_id = p->sprite;out->cost = p->cost;
        out->hp = p->hp;out->recharge = p->recharge;
    } else {
        int type = BOOK_ENEMIES[index];
        out->name = out->short_name = EN_NAMES[type];
        out->art_id = type == EN_ROBOT ? SPR_ROBOT : SPR_DUCK;
        out->cost = EN_ONLINE_COST[type];out->hp = EN_BASE_HP[type];
        out->enemy_variant = type;
        out->description = type == EN_DUCK ? "НАРИСОВАННАЯ АВТОРОМ УТКА-ЗОМБИ." :
                           type == EN_CONE ? "КОНУС ЗАЩИЩАЕТ ТУ ЖЕ УТКУ." :
                           type == EN_BUCKET ? "ВЕДРО ЗАЩИЩАЕТ УТКУ ОТ УДАРОВ." :
                                              "КОРОЛЕВА УПРАВЛЯЕТ БОЛЬШИМ РОБОТОМ.";
        out->detail = type == EN_ROBOT ? "ПОЯВЛЯЕТСЯ В КОНЦЕ УРОВНЯ 10. ДВИЖЕТСЯ МЕДЛЕННО." :
                                         "ЧЕМ ДАЛЬШЕ УРОВЕНЬ, ТЕМ БОЛЬШЕ ЗДОРОВЬЯ.";
    }
    return 1;
}

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
static int garden_selected;            /* -1 none, PT_COUNT eraser, else deck index */
static int garden_mode;                /* 0 = plants, 1 = the three goose variants */
static int garden_map = 1;              /* 1 = lawn, WATER_LEVEL = water */
static int book_selected;
static int book_enemy_selected;         /* 0..3 in BOOK_ENEMIES */
static int book_enemy_tab;              /* the five plants or illustrated foes */
/* Online lives outside the campaign save; snapshots below never touch it. */
static OnMatch online_match;
static OnNetView online_view;
static OnPublishedLevel custom_level;
#define CUSTOM_BACKGROUND_DEFAULT 0x32465au
#define CUSTOM_FALL_MARGIN_TILES 3.0f
static int custom_object_count, custom_level_active, custom_level_won, custom_level_coins;
static int custom_level_attempts, custom_level_total_coins;
static float custom_start_player_x, custom_start_player_y;
static float custom_goal_center_x, custom_goal_center_y;
static uint32_t custom_background_color = CUSTOM_BACKGROUND_DEFAULT;
static float custom_fall_plane_y;
static int custom_player_index = -1;
#define CUSTOM_ID_MAP_CAP 65536
static int custom_id_map[CUSTOM_ID_MAP_CAP];
static float custom_player_x, custom_player_y, custom_player_w, custom_player_h;
static float custom_player_vx, custom_player_vy;
static float custom_respawn_x, custom_respawn_y;
static float custom_dash_remaining, custom_dash_cooldown;
static int custom_dash_direction, custom_dash_request, custom_dash_held;
static int custom_jump_count, custom_player_wall_contact;
static int custom_checkpoint_id;
static float custom_gravity = 1450.0f;
static float custom_elapsed_time;
static int custom_player_grounded, custom_control_axis, custom_control_vertical;
static int custom_player_facing_left;
static int custom_jump_request, custom_jump_held, custom_trigger_request, custom_trigger_held;
static int custom_jetpack_mode, custom_jetpack_active;
static uint8_t custom_portal_inside[ON_LEVEL_OBJECT_CAP];
static uint8_t custom_trigger_fired[ON_LEVEL_OBJECT_CAP];
static uint8_t custom_trigger_active[ON_LEVEL_OBJECT_CAP];
static uint8_t custom_trigger_touch_inside[ON_LEVEL_OBJECT_CAP];
static uint16_t custom_trigger_counts[ON_LEVEL_OBJECT_CAP];
static float custom_trigger_timers[ON_LEVEL_OBJECT_CAP];
static float custom_trigger_touch_timers[ON_LEVEL_OBJECT_CAP];
static uint8_t custom_collision_disabled[ON_LEVEL_OBJECT_CAP];
static uint8_t custom_invisible[ON_LEVEL_OBJECT_CAP];
static int custom_player_collision_enabled;
static int custom_draw_order[ON_LEVEL_OBJECT_CAP];
static const OnPublishedLevel *custom_draw_sort_level;
/* A timed rotation completes one full turn per second. */
#define CUSTOM_GROUP_ROTATION_DEGREES_PER_SECOND 360.0f
typedef struct {int group_id;float remaining, frame_step;} CustomGroupRotation;
static CustomGroupRotation custom_group_rotations[ON_LEVEL_OBJECT_CAP];
static int custom_group_rotation_slots[10000];
static int custom_group_rotation_count;
static float custom_camera_x, custom_camera_y;
static int online_has_match;
static int online_selected, online_map, online_search, online_page;
static char online_code[ON_ROOM_ID_SIZE];
static char online_hint[110];
static float online_hint_time;
static Phase book_return;
static Phase custom_levels_return;
static Phase custom_levels_parent_return;
static int custom_levels_nested_from_workshop;
static Phase custom_play_return;
static Phase workshop_return;

static int garden_card_x(int index) {
    /* Keep the three-goose palette centered in the same five-card rail. */
    return CARD_X + (garden_mode ? CARD_STEP : 0) + index * CARD_STEP;
}

void game_set_lvgl_ui(int enabled) {
    enabled = !!enabled;
    if (enabled && !use_lvgl_ui) {
        /* A saved two-tap selection from an older APK must not cause a tap
         * on the lawn to plant: LVGL packets now require a drag-and-drop. */
        selected = -1;
        garden_selected = -1;
        online_selected = -1;
    }
    use_lvgl_ui = enabled;
}

static int custom_platformer_start(const OnPublishedLevel *level);
static void custom_platformer_stop(void);
static void custom_fire_triggers(int event);
static void custom_platformer_update(float dt);
static void custom_platformer_draw(void);

void game_custom_levels_open(void) {
    if (phase == PH_WORKSHOP) {
        custom_levels_parent_return = workshop_return == PH_CUSTOM_LEVELS
            ? custom_levels_return : workshop_return;
        custom_levels_nested_from_workshop = 1;
        custom_levels_return = PH_WORKSHOP;
    } else {
        custom_levels_nested_from_workshop = 0;
        custom_levels_return = phase == PH_SELECT ? PH_SELECT : PH_MENU;
    }
    on_net_open();
    on_net_levels_refresh();
    phase = PH_CUSTOM_LEVELS;
}
void game_custom_levels_refresh(void) { on_net_levels_refresh(); }

void game_workshop_open(void) {
    if (phase != PH_WORKSHOP && phase != PH_WORKSHOP_DETAILS && phase != PH_WORKSHOP_EDIT)
        workshop_return = phase;
    on_net_account_restore();
    phase = PH_WORKSHOP;
}
void game_workshop_open_details(void) {
    if (phase == PH_WORKSHOP) phase = PH_WORKSHOP_DETAILS;
}
void game_workshop_open_editor(void) {
    if (phase == PH_WORKSHOP || phase == PH_WORKSHOP_DETAILS)
        phase = PH_WORKSHOP_EDIT;
}
void game_workshop_back(void) {
    if (phase == PH_WORKSHOP_EDIT) phase = PH_WORKSHOP_DETAILS;
    else if (phase == PH_WORKSHOP_DETAILS) phase = PH_WORKSHOP;
    else if (phase == PH_WORKSHOP) phase = workshop_return;
}
void game_custom_level_request(const char *id) { on_net_level_fetch(id); }
void game_custom_level_exit(void) {
    if (phase == PH_CUSTOM_PLAY) {
        custom_platformer_stop();
        phase = custom_play_return;
    } else if (phase == PH_CUSTOM_LEVELS) {
        on_net_level_cancel();
        phase = custom_levels_return;
        if (custom_levels_nested_from_workshop) {
            custom_levels_return = custom_levels_parent_return;
            custom_levels_nested_from_workshop = 0;
        }
    }
}
void game_custom_control(int horizontal, int jump, int trigger) {
    if (horizontal < -1) horizontal = -1;
    if (horizontal > 1) horizontal = 1;
    custom_control_axis = horizontal;
    if (jump && !custom_jump_held) custom_jump_request = 1;
    if (trigger && !custom_trigger_held) custom_trigger_request = 1;
    custom_jump_held = !!jump;
    custom_trigger_held = !!trigger;
}
void game_custom_vertical_control(int vertical) {
    if (vertical < -1) vertical = -1;
    if (vertical > 1) vertical = 1;
    custom_control_vertical = vertical;
}
void game_custom_dash_control(int dash) {
    dash = !!dash;
    if (dash && !custom_dash_held) custom_dash_request = 1;
    custom_dash_held = dash;
}
int game_custom_jetpack_mode(void) {
    return custom_level_active && custom_jetpack_mode;
}
void game_custom_hud_snapshot(GameCustomHudSnapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->active = !!custom_level_active;
    if (!custom_level_active) return;
    out->attempts = custom_level_attempts > 0 ? custom_level_attempts : 1;
    out->elapsed_seconds = custom_elapsed_time > 0 ? custom_elapsed_time : 0;
    out->coins = custom_level_coins;
    out->total_coins = custom_level_total_coins;
    out->won = !!custom_level_won;
    out->movement_abilities = custom_level.movement_abilities;
    if (custom_level_won) {
        out->progress_percent = 100;return;
    }
    float start_dx = custom_goal_center_x -
                     (custom_start_player_x + custom_player_w * .5f);
    float start_dy = custom_goal_center_y -
                     (custom_start_player_y + custom_player_h * .5f);
    float now_dx = custom_goal_center_x -
                   (custom_player_x + custom_player_w * .5f);
    float now_dy = custom_goal_center_y -
                   (custom_player_y + custom_player_h * .5f);
    float start_distance = hypotf(start_dx, start_dy);
    if (start_distance > 1.0f) {
        float progress = (1.0f - hypotf(now_dx, now_dy) / start_distance) * 100.0f;
        if (progress < 0) progress = 0;
        if (progress > 100) progress = 100;
        out->progress_percent = (int)lrintf(progress);
    }
}
static float spawn_t;
static int to_spawn;
static int total_zombies;
static float banner_t;
static float global_t;
static int boss_spawned;
static const char *banner_text;

static const char *LEVEL_NAMES[10] = {
    "ПЕРВАЯ ЗАЩИТА", "НОВАЯ ВОЛНА", "У ЗАБОРА", "НА ПОДСТУПАХ",
    "ВОДЯНОЙ УРОВЕНЬ", "СЛОЖНЕЕ И СЛОЖНЕЕ", "ДЕРЖИ ОБОРОНУ",
    "ПОСЛЕДНИЙ РУБЕЖ", "ПЕРЕД БУРЕЙ", "КОРОЛЕВА БЛИЗКО"
};

static Plant grid[ROWS][COLS];
/* A lily supports another plant in the same water cell; it is a second layer,
 * not an extra plant type in the old fixed-size Plant save record. */
static uint8_t lily[ROWS][COLS];
static Zombie zomb[ZMAX];
static Pea peas[PEAMAX];
static Coin coins[COINMAX];
static Part parts[PARTMAX];
static Mower mower[ROWS];

#define CELL_CX(c) (LAWN_X + (c) * CELL_W + CELL_W / 2)
#define CELL_CY(r) (LAWN_Y + (r) * CELL_H + CELL_H / 2)

static int is_water_row(int row) {
    return level == WATER_LEVEL && row >= WATER_FIRST_ROW &&
           row <= WATER_LAST_ROW;
}

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

/* One polished coin token is shared by the HUD, packets and collectible drops. */
static void draw_coin_icon(int cx, int cy, int r) {
    if (r <= 0) return;
    sprite_draw(SPR_COIN, cx - r, cy - r, 2 * r, 2 * r, 0);
}

static void draw_lily(int cx, int cy) {
    /* The author's flat, two-eyed green lily floats beneath its passenger. */
    ellipse(cx, cy + 35, 43, 7, COL(24, 117, 131));
    sprite_draw(SPR_LILY, cx - 51, cy - 1, 102, 54, 0);
}

static void draw_plant(int cx, int cy, int type, float sway) {
    if (type < 0 || type >= PT_COUNT) return;
    if (type == PT_LILY) { draw_lily(cx, cy); return; }
    /* Each selectable plant is Kirill's own picture; no stand-ins. */
    ellipse(cx, cy + 32, 34, 7, COL(47, 112, 30));
    sprite_draw(PDEF[type].sprite, cx - 43, cy - 48 + (int)(sway * 2), 86, 86, 0);
}

/* ------------------------------------------------------------------ */
/* author's duck-zombie and the Duck Queen's piloted robot            */
/* ------------------------------------------------------------------ */

/* Use the author's complete drawing for each duck. In particular, do not
 * stack a procedural hat on top of the cone/bucket illustrations. */
static void draw_duck_variant(int x, int y, int size, int type, int flip) {
    int sprite = type == EN_CONE ? SPR_DUCK_CONE :
                 type == EN_BUCKET ? SPR_DUCK_BUCKET : SPR_DUCK;
    sprite_draw(sprite, x, y, size, size, flip);
}

static void draw_enemy(const Zombie *z) {
    int x = (int)z->x, y = CELL_CY(z->row);
    if (z->type == EN_ROBOT) {
        ellipse(x, y + 66, 75, 12, COL(38, 85, 28));
        sprite_draw(SPR_ROBOT, x - 95, y - 122 + (int)(sinf(z->anim) * 2), 190, 190, 0);
    } else {
        ellipse(x, y + 36, 40, 8, COL(48, 120, 34));
        /* The duck faces right in the PNG: turn it towards the house. */
        draw_duck_variant(x - 46, y - 50 + (int)(sinf(z->anim) * 2),
                          92, z->type, 1);
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
            float roll = rndf();
            z->type = level >= 6 && roll < 0.12f ? EN_BUCKET :
                      level >= 3 && roll < 0.38f ? EN_CONE : EN_DUCK;
            /* The same duck gains real HP from its gear; not merely a hat. */
            float base = (180 + 35 * (level - 1)) *
                         (float)EN_BASE_HP[z->type] / EN_BASE_HP[EN_DUCK];
            z->hp = z->maxhp = base * (1.0f + 0.25f * prog);
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
            z->x = CELL_CX(COLS - 1); /* visible at the right edge, aligned with the last cell */
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
    memset(lily, 0, sizeof(lily));
    memset(grid, 0, sizeof(grid));
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) grid[r][c].type = PT_NONE;
    memset(zomb, 0, sizeof(zomb));
    memset(peas, 0, sizeof(peas));
    memset(coins, 0, sizeof(coins));
    memset(parts, 0, sizeof(parts));
    for (int r = 0; r < ROWS; r++) { mower[r].used = 0; mower[r].running = 0; mower[r].x = LAWN_X + 30; }
    for (int i = 0; i < PT_COUNT; i++) cooldown[i] = 0;
    /* Starter coins buy the first sunflower; after that it makes the income. */
    coin_balance = 250 + (n - 1) * 30;
    selected = -1;
    total_zombies = 5 + n * 3;
    to_spawn = total_zombies;
    spawn_t = 11.0f - n * 0.2f;
    banner_t = n == 1 ? 6 : 4;
    boss_spawned = 0;
    banner_text = n == 1 ? "ПОДСОЛНУХ-НАРКОМАН ДАЁТ МОНЕТЫ" :
                  n == WATER_LEVEL ? "В ВОДЕ СНАЧАЛА ПОСАДИ КУВШИНКУ!" :
                  LEVEL_NAMES[n - 1];
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
    garden_mode = 0;
    garden_map = 1;
    book_selected = PT_PEA;
    book_enemy_selected = 0;
    book_enemy_tab = 0;
    on_net_shutdown(); /* safe even when no room was ever opened */
    custom_platformer_stop();
    memset(&custom_level, 0, sizeof custom_level);
    custom_object_count = custom_level_won = custom_level_coins = 0;
    memset(&online_match, 0, sizeof(online_match));
    memset(&online_view, 0, sizeof(online_view));
    online_has_match = 0;
    online_selected = -1;
    online_map = 1;
    online_search = 0;
    online_page = 0;
    online_code[0] = online_hint[0] = 0;
    online_hint_time = 0;
    book_return = PH_MENU;
    custom_levels_return = PH_MENU;
    custom_levels_parent_return = PH_MENU;
    custom_levels_nested_from_workshop = 0;
    custom_play_return = PH_CUSTOM_LEVELS;
    workshop_return = PH_MENU;
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
    spawn_zombie(); zomb[0].x = 980; zomb[0].row = 4; zomb[0].type = EN_DUCK;
    spawn_zombie(); zomb[1].x = 1100; zomb[1].row = 0; zomb[1].type = EN_DUCK;
    spawn_boss();   zomb[2].x = 1020; boss_spawned = 1;
    boss_phase = 1; to_spawn = 0;
    banner_text = "КОРОЛЕВА В РОБОТЕ!"; banner_t = 5;
    spawn_pea(2, 400, 20);
    spawn_pea(1, 520, 20);
    spawn_coin(CELL_CX(1) + 26, CELL_CY(4) - 30); coins[0].y = coins[0].target_y;
}

void game_debug_armored_snapshot(void) {
    game_debug_snapshot();
    zomb[0].type = EN_CONE;
    zomb[0].hp = zomb[0].maxhp = EN_BASE_HP[EN_CONE];
    zomb[1].type = EN_BUCKET;
    zomb[1].hp = zomb[1].maxhp = EN_BASE_HP[EN_BUCKET];
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
                    if ((grid[r][c].type >= 0 || lily[r][c]) &&
                        fabsf(z->x - CELL_CX(c)) < 78) {
                        grid[r][c].type = PT_NONE;
                        lily[r][c] = 0; /* the robot crushes both layers */
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

        if (z->x <= LAWN_X + 70) { /* reached the mower by the lawn edge */
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
        } else if (lily[z->row][col] && z->x <= CELL_CX(col) + 28) {
            /* With nothing on top, a duck can knock the small pad away. */
            lily[z->row][col] = 0;
            burst(CELL_CX(col), CELL_CY(z->row), 8, PDEF[PT_LILY].body, 70);
            z->eating = 0;
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

static void online_say(const char *text) {
    snprintf(online_hint, sizeof online_hint, "%s", text);
    online_hint_time = 3;
}
static int online_role(void) {
    return online_view.slot == ON_SLOT_HOST ? online_view.host_role :
           online_view.slot == ON_SLOT_GUEST ? online_view.guest_role : ON_NO_ROLE;
}

void game_online_ui_snapshot(OnMatch *match, int *role, int *selection,
                             char *hint, size_t hint_size, float *hint_seconds) {
    if (match) *match = online_match;
    if (role) *role = online_role();
    if (selection) *selection = online_selected;
    if (hint && hint_size) snprintf(hint, hint_size, "%s", online_hint);
    if (hint_seconds) *hint_seconds = online_hint_time;
}
/* Indexes into the live room list; no extra connections or Firebase reads. */
static int online_filtered(int indexes[ON_ROOM_LIST_CAP]) {
    int n = 0, prefix = (int)strlen(online_code);
    for (int i = 0; i < online_view.room_count; i++)
        if (!prefix || !strncmp(online_view.rooms[i].id, online_code, (size_t)prefix))
            indexes[n++] = i;
    return n;
}
static void online_input(int x, int y) {
    if (phase == PH_ONLINE_ROOMS) {
        if (!online_search && inside(x, y, 1070, 144, 1223, 222)) {
            game_workshop_open();return;
        }
        if (online_search) {
            if (inside(x, y, 902, 109, 1009, 159)) { online_search = 0;return; }
            if (inside(x, y, 847, 171, 993, 236)) {
                size_t len = strlen(online_code);
                if (len) online_code[len - 1] = 0;
                online_page = 0;return;
            }
            if (inside(x, y, 395, 568, 880, 644)) {
                if (strlen(online_code) == 6) {
                    on_net_join(online_code);
                    online_search = 0;
                } else online_say("ВВЕДИ ВСЕ ШЕСТЬ СИМВОЛОВ КОДА");
                return;
            }
            const char *keys = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            int col = (x - 291) / 78, row = (y - 270) / 70;
            int ix = row * 9 + col;
            if (x >= 291 && y >= 270 && col >= 0 && col < 9 && row >= 0 &&
                row < 4 && x - 291 - col * 78 < 68 &&
                y - 270 - row * 70 < 58 && ix < 32 && keys[ix]) {
                size_t len = strlen(online_code);
                if (len < 6) {online_code[len] = keys[ix];online_code[len + 1] = 0;}
                online_page = 0;
            }
            return;
        }
        if (inside(x, y, 1070, 17, 1255, 87)) {
            on_net_close();phase = PH_MENU;return;
        }
        if (inside(x, y, 720, 144, 884, 222)) {online_search = 1;return;}
        if (inside(x, y, 904, 144, 1065, 222)) {on_net_create(online_map);return;}
        if (inside(x, y, 70, 144, 303, 222)) {online_map = 1;return;}
        if (inside(x, y, 313, 144, 550, 222)) {online_map = 5;return;}
        if (inside(x, y, 1019, 248, 1225, 308)) {on_net_refresh();return;}
        int indexes[ON_ROOM_LIST_CAP];
        int count = online_filtered(indexes);
        if (online_page * 8 >= count) online_page = 0;
        if (inside(x, y, 873, 654, 1220, 705)) {
            if ((online_page + 1) * 8 < count) online_page++;
            else online_page = 0;
            return;
        }
        if (inside(x, y, 62, 654, 374, 705)) {
            if (online_page > 0) online_page--;
            return;
        }
        for (int i = 0; i < 8; i++) {
            int col = i % 2, row = i / 2, x0 = 69 + col * 582;
            int y0 = 321 + row * 80, pos = online_page * 8 + i;
            if (pos < count && inside(x, y, x0, y0, x0 + 557, y0 + 70)) {
                on_net_join(online_view.rooms[indexes[pos]].id);
                return;
            }
        }
        return;
    }
    if (inside(x, y, 1093, 17, 1265, 91) ||
        (phase == PH_ONLINE_MATCH && online_match.winner &&
         inside(x, y, 411, 459, 869, 560))) {
        on_net_leave();online_has_match = 0;
        online_selected = -1;online_code[0] = 0;online_page = 0;
        phase = PH_ONLINE_ROOMS;return;
    }
    if (phase == PH_ONLINE_LOBBY) {
        if (inside(x, y, 160, 243, 611, 575)) on_net_choose(ON_ROLE_PLANTS);
        else if (inside(x, y, 663, 243, 1119, 575)) on_net_choose(ON_ROLE_ZOMBIES);
        return;
    }
    if (phase != PH_ONLINE_MATCH || !online_has_match) return;
    if (inside(x, y, 917, 20, 1088, 91)) {open_book();return;}
    if (online_match.winner) return;
    int role = online_role(), got = 0;
    OnCommand cmd = {0};
    if (role == ON_ROLE_PLANTS) {
        for (int i = online_match.coin_count - 1; i >= 0; i--) {
            const OnCoin *coin = &online_match.coins[i];
            int dx = x - (int)coin->x, dy = y - (int)coin->y;
            if (dx * dx + dy * dy < 40 * 40) {
                cmd.kind = ON_CMD_COIN;cmd.id = coin->id;got = 1;break;
            }
        }
        if (!got && x < ON_BOARD_X)
            for (int i = 0; i < PT_COUNT; i++) {
                int top = 125 + i * 104;
                if (inside(x, y, 12, top, 235, top + 98)) {
                    online_selected = online_match.plant_cash >= on_plant_cost[i] &&
                        online_match.plant_cooldown[i] <= 0 &&
                        (i != ON_LILY || online_match.map == 5) ? i : -1;
                    if (online_selected == -1) online_say("МОНЕТ ИЛИ ПЕРЕЗАРЯДКИ НЕ ХВАТАЕТ");
                    return;
                }
            }
        if (!got && online_selected >= 0 &&
            inside(x, y, ON_BOARD_X, ON_BOARD_Y,
                   ON_BOARD_X + ON_COLS * ON_CELL_W - 1,
                   ON_BOARD_Y + ON_ROWS * ON_CELL_H - 1)) {
            cmd.kind = ON_CMD_PLANT;cmd.type = online_selected;
            cmd.col = (x - ON_BOARD_X) / ON_CELL_W;
            cmd.row = (y - ON_BOARD_Y) / ON_CELL_H;
            got = 1;online_selected = -1;
        }
    } else if (role == ON_ROLE_ZOMBIES) {
        if (inside(x, y, 15, 615, 232, 702)) {
            cmd.kind = ON_CMD_FINISH;got = 1;
        } else if (x < ON_BOARD_X) {
            for (int i = 0; i < 3; i++) {
                int top = 151 + i * 113;
                if (inside(x, y, 12, top, 235, top + 98)) {
                    online_selected = online_match.zombie_cash >= on_duck_cost[i] &&
                        online_match.duck_cooldown[i] <= 0 && online_match.left > 0 ? i : -1;
                    if (online_selected == -1) online_say("МОНЕТ ИЛИ УТОК НЕ ХВАТАЕТ");
                    return;
                }
            }
        } else if (online_selected >= 0 &&
                   y >= ON_BOARD_Y && y < ON_BOARD_Y + ON_ROWS * ON_CELL_H) {
            cmd.kind = ON_CMD_SPAWN;cmd.type = on_duck_type[online_selected];
            cmd.row = (y - ON_BOARD_Y) / ON_CELL_H;
            got = 1;online_selected = -1;
        }
    }
    if (!got) return;
    /* Validate on a scratch copy: an invalid gesture costs neither coins nor
     * a network write. The host alone applies authoritative commands. */
    OnMatch test = online_match;
    if (!on_match_apply(&test, role, &cmd)) {
        online_say("НЕВОЗМОЖНЫЙ ХОД: ПРОВЕРЬ КЛЕТКУ И МОНЕТЫ");return;
    }
    if (online_view.slot == ON_SLOT_HOST) {
        online_match = test;
        on_net_publish(&online_match);
    } else if (!on_net_send(cmd)) online_say("ДОЖДИСЬ ПОДТВЕРЖДЕНИЯ ПРЕДЫДУЩЕГО ХОДА");
}

void game_input_press(int x, int y) {
    if (x < 0 || x >= GAME_W || y < 0 || y >= GAME_H) return;
    if (phase == PH_ONLINE_ROOMS || phase == PH_ONLINE_LOBBY ||
        phase == PH_ONLINE_MATCH) {online_input(x, y);return;}
    if (phase == PH_WORKSHOP) {
        if (inside(x, y, 1040, 20, 1265, 110)) game_workshop_back();
        else if (inside(x, y, 80, 570, 430, 690) ||
                 inside(x, y, 800, 265, 1220, 380))
            game_workshop_open_details();
        else if (inside(x, y, 865, 570, 1230, 690))
            game_custom_levels_open();
        return;
    }
    if (phase == PH_WORKSHOP_DETAILS) {
        if (inside(x, y, 1040, 20, 1265, 110)) game_workshop_back();
        else if (inside(x, y, 740, 565, 1215, 690))
            game_workshop_open_editor();
        return;
    }
    if (phase == PH_WORKSHOP_EDIT) {
        if (inside(x, y, 1040, 20, 1265, 110)) game_workshop_back();
        return;
    }
    if (phase == PH_CUSTOM_LEVELS) {
        if (inside(x, y, 1040, 20, 1265, 105)) game_custom_level_exit();
        return;
    }
    if (phase == PH_CUSTOM_PLAY) {
        if (inside(x, y, 1090, 12, 1275, 110)) game_custom_level_exit();
        return;
    }
    if (phase == PH_MENU) {
        if (inside(x, y, 52, 31, 421, 124)) {
            game_custom_levels_open();
        } else if (inside(x, y, 440, 548, 840, 674)) {
            if (saved_battle) phase = PH_PLAY;
            else if (completed_mask == 0 && resume_level == 1) {
                start_intro(0);
            } else start_level(resume_level);
        } else if (inside(x, y, 475, 38, 785, 127)) {
            phase = PH_SELECT;
        } else if (inside(x, y, 850, 38, 1240, 132)) {
            garden_selected = -1;
            phase = PH_GARDEN;
        } else if (inside(x, y, 98, 568, 392, 665)) open_book();
        else if (inside(x, y, 893, 569, 1223, 661)) {
            online_code[0] = 0;online_page = online_search = 0;
            online_has_match = 0;online_selected = -1;
            on_net_open();phase = PH_ONLINE_ROOMS;
        }
        return;
    }
    if (phase == PH_SELECT) {
        if (inside(x, y, 1045, 18, 1265, 85)) { phase = PH_MENU; return; }
        if (inside(x, y, 880, 567, 1240, 641)) {
            game_custom_levels_open();
            return;
        }
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
        if (inside(x, y, 160, 121, 344, 159)) { book_enemy_tab = 0; return; }
        if (inside(x, y, 355, 121, 540, 159)) { book_enemy_tab = 1; return; }
        int count = book_enemy_tab ? BOOK_ENEMY_COUNT : PT_COUNT;
        for (int i = 0; i < count; i++)
            if (inside(x, y, 160, BOOK_ENTRY_Y + i * BOOK_ENTRY_STEP,
                       540, BOOK_ENTRY_Y + i * BOOK_ENTRY_STEP + BOOK_ENTRY_H)) {
                if (book_enemy_tab) book_enemy_selected = i;
                else book_selected = i;
                return;
            }
        return;
    }
    if (phase == PH_GARDEN) {
        if (inside(x, y, 915, 16, 1070, 63)) { open_book(); return; }
        if (inside(x, y, 1080, 16, 1265, 63)) { phase = PH_MENU; return; }
        if (inside(x, y, 6, 42, 118, 73)) {
            garden_mode = 0;garden_selected = -1;return;
        }
        if (inside(x, y, 128, 42, 240, 73)) {
            garden_mode = 1;garden_selected = -1;return;
        }
        if (inside(x, y, 6, 78, 118, 109)) {
            garden_map = 1;garden_selected = -1;return;
        }
        if (inside(x, y, 128, 78, 240, 109)) {
            garden_map = WATER_LEVEL;garden_selected = -1;return;
        }
        if (inside(x, y, 915, 73, 1265, 116)) { garden_selected = PT_COUNT; return; }
        if (y >= 12 && y <= 112) {
            int count = garden_mode ? GARDEN_DUCK_COUNT : PT_COUNT;
            for (int i = 0; i < count; i++) {
                int x0 = garden_card_x(i);
                if (x >= x0 && x <= x0 + CARD_W) {
                    garden_selected = i;
                    return;
                }
            }
        }
        if (x >= LAWN_X && x < LAWN_X + COLS * CELL_W &&
            y >= LAWN_Y && y < LAWN_Y + ROWS * CELL_H) {
            int c = (x - LAWN_X) / CELL_W, r = (y - LAWN_Y) / CELL_H;
            if (garden_selected == PT_COUNT) garden[r][c] = PT_NONE;
            else if (garden_selected >= 0) {
                garden[r][c] = garden_mode ? PT_COUNT + garden_selected : garden_selected;
                if (use_lvgl_ui) garden_selected = -1;
            }
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
    if (inside(x, y, 1100, 28, 1260, 93)) { phase = PH_MENU; return; }
    if (inside(x, y, 925, 28, 1080, 93)) { open_book(); return; }

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

    /* Five illustrated packets; the pad is useful only on the water level. */
    if (x >= BATTLE_CARD_X && x <= BATTLE_CARD_X + BATTLE_CARD_W) {
        for (int i = 0; i < PT_COUNT; i++) {
            int y0 = BATTLE_CARD_Y + i * BATTLE_CARD_STEP;
            if (y >= y0 && y <= y0 + BATTLE_CARD_H) {
                selected = (coin_balance >= PDEF[i].cost && cooldown[i] <= 0 &&
                            (i != PT_LILY || level == WATER_LEVEL)) ? i : -1;
                return;
            }
        }
    }

    /* Land accepts regular plants; water accepts a lily first, then a plant
     * on top. A rejected placement never spends coins or starts a cooldown. */
    if (selected >= 0 && x >= LAWN_X && x < LAWN_X + COLS * CELL_W &&
        y >= LAWN_Y && y < LAWN_Y + ROWS * CELL_H) {
        int c = (x - LAWN_X) / CELL_W, r = (y - LAWN_Y) / CELL_H;
        int water = is_water_row(r);
        if (grid[r][c].type < 0 && coin_balance >= PDEF[selected].cost &&
            cooldown[selected] <= 0 &&
            (selected == PT_LILY ? (water && !lily[r][c]) :
                                   (!water || lily[r][c]))) {
            if (selected == PT_LILY) {
                lily[r][c] = 1;
            } else {
                grid[r][c].type = selected;
                grid[r][c].hp = (float)PDEF[selected].hp;
                grid[r][c].fire_t = selected == PT_SUNFLOWER ? 5.0f : 0.4f;
            }
            coin_balance -= PDEF[selected].cost;
            cooldown[selected] = PDEF[selected].recharge;
            burst(CELL_CX(c), CELL_CY(r), 8, PDEF[selected].body, 90);
        }
        selected = -1;
        return;
    }
    selected = -1;
}

void game_input_release(int x, int y) {
    (void)x;(void)y;
    if (phase == PH_CUSTOM_PLAY) {
        game_custom_control(0, 0, 0);
        game_custom_vertical_control(0);
    }
}

int game_legacy_plant_drag(int screen_phase, int from_x, int from_y,
                           int to_x, int to_y) {
    if (screen_phase != (int)phase ||
        (phase != PH_PLAY && phase != PH_GARDEN)) return 0;
    int source = 0;
    if (phase == PH_PLAY && from_x >= BATTLE_CARD_X &&
        from_x <= BATTLE_CARD_X + BATTLE_CARD_W) {
        for (int i = 0; i < PT_COUNT; i++) {
            int y0 = BATTLE_CARD_Y + i * BATTLE_CARD_STEP;
            if (from_y >= y0 && from_y <= y0 + BATTLE_CARD_H) {
                source = 1;
                break;
            }
        }
    } else if (phase == PH_GARDEN && from_y >= 12 && from_y <= 112) {
        int count = garden_mode ? GARDEN_DUCK_COUNT : PT_COUNT;
        for (int i = 0; i < count; i++) {
            int x0 = garden_card_x(i);
            if (from_x >= x0 && from_x <= x0 + CARD_W) {
                source = 1;
                break;
            }
        }
    }
    int target = to_x >= LAWN_X && to_x < LAWN_X + COLS * CELL_W &&
                 to_y >= LAWN_Y && to_y < LAWN_Y + ROWS * CELL_H;
    return source && target;
}

/* ------------------------------------------------------------------ */
/* render                                                             */
/* ------------------------------------------------------------------ */

/* Show every part of the author's 500x500 map. A previous crop kept only
 * x=274..500 (featureless lime green), then hid the entire wooden path under
 * an opaque HUD and painted a checkerboard over the grass. Map the left half
 * to the packet rail and the right half to the 9x5 field instead. The split
 * keeps the hand-drawn, irregular wood/grass boundary at the edge of play. */
static void draw_authored_map_band(int id, int y, int h, int sy, int sh) {
    const SpritePacked *map = &SPRITE_DATA[id];
    int split = map->w / 2;
    sprite_crop(id, 0, y, LAWN_X, h, 0, sy, split, sh, 0);
    sprite_crop(id, LAWN_X, y, GAME_W - LAWN_X, h,
                split, sy, map->w - split, sh, 0);
}

static void draw_background(void) {
    int scene_level = phase == PH_ONLINE_MATCH && online_has_match ?
                      online_match.map : level;
    int water_scene = phase == PH_GARDEN ? garden_map == WATER_LEVEL :
        scene_level == WATER_LEVEL &&
        (phase == PH_PLAY || phase == PH_LEVEL_CLEAR ||
         phase == PH_LOSE || phase == PH_WIN || phase == PH_ONLINE_MATCH);
    if (preferences_neutral_background_enabled()) {
        /* A muted blue board keeps the original garden artwork distinct. */
        rect(0, 0, GAME_W - 1, GAME_H - 1, COL(45, 65, 85));
        rect(LAWN_X, LAWN_Y, GAME_W - 1,
             LAWN_Y + ROWS * CELL_H - 1, COL(79, 111, 137));
        if (water_scene)
            rect(LAWN_X, LAWN_Y + CELL_H, GAME_W - 1,
                 LAWN_Y + 3 * CELL_H - 1, COL(72, 141, 162));
        return;
    }
    int id = water_scene && sprite_pixels[SPR_WATER_MAP] ? SPR_WATER_MAP : SPR_MAP;
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(108, 171, 73));
    if (sprite_pixels[id]) {
        int source_h = SPRITE_DATA[id].h;
        if (water_scene && id == SPR_WATER_MAP) {
            /* The original canal runs from about y=100..270 of the image.
             * Stretch just these three artwork bands so the painted water
             * coincides with the two rows that need a lily pad. */
            int canal_y = source_h / 5, canal_end = source_h * 27 / 50;
            int water_y = LAWN_Y + CELL_H, water_end = water_y + 2 * CELL_H;
            draw_authored_map_band(id, 0, water_y, 0, canal_y);
            draw_authored_map_band(id, water_y, water_end - water_y,
                                   canal_y, canal_end - canal_y);
            draw_authored_map_band(id, water_end, GAME_H - water_end,
                                   canal_end, source_h - canal_end);
        } else draw_authored_map_band(id, 0, GAME_H, 0, source_h);
    } else if (water_scene) {
        rect(LAWN_X, LAWN_Y + CELL_H, GAME_W - 1,
             LAWN_Y + 3 * CELL_H - 1, COL(26, 165, 193));
    }
    /* No generated bright-green tiles, square outlines, colour wash or
     * divider: the original PNG is the map in offline, online and garden. */
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
        if (zomb[i].active && zomb[i].type != EN_ROBOT) remaining++;
    return remaining;
}

int game_wave_total(void) { return total_zombies; }

void game_offline_ui_snapshot(GameOfflineUIState *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->level = level;out->coins = coin_balance;
    out->selection = selected;out->garden_selection = garden_selected;
    out->garden_mode = garden_mode;out->garden_map = garden_map;
    out->wave_remaining = game_wave_remaining();out->wave_total = total_zombies;
    out->intro_step = intro_step;
    out->book_enemy_tab = book_enemy_tab;
    out->book_selection = book_enemy_tab ? book_enemy_selected : book_selected;
    memcpy(out->cooldown, cooldown, sizeof out->cooldown);
    if (boss_phase)
        for (int i = 0; i < ZMAX; i++)
            if (zomb[i].active && zomb[i].type == EN_ROBOT) {
                out->boss_health_percent = (int)ceilf(100 * zomb[i].hp / zomb[i].maxhp);
                if (out->boss_health_percent < 0) out->boss_health_percent = 0;
                break;
            }
}

/* Calm blue/ivory controls with a warm accent for the main action. */
enum { BUTTON_TONE_GRAY, BUTTON_TONE_WHITE, BUTTON_TONE_BLACK, BUTTON_TONE_ACCENT };

/* Rounded software-rendered surfaces keep the fallback UI in step with LVGL. */
static void round_rect(int x0, int y0, int x1, int y1, int radius, uint32_t color) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    int width = x1 - x0 + 1, height = y1 - y0 + 1;
    int max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) radius = max_radius;
    if (radius < 1) { rect(x0, y0, x1, y1, color); return; }
    rect(x0 + radius, y0, x1 - radius, y1, color);
    rect(x0, y0 + radius, x1, y1 - radius, color);
    disc(x0 + radius, y0 + radius, radius, color);
    disc(x1 - radius, y0 + radius, radius, color);
    disc(x0 + radius, y1 - radius, radius, color);
    disc(x1 - radius, y1 - radius, radius, color);
}

static void draw_button_tone(int x0, int y0, int x1, int y1,
                             const char *label, int size, int active, int tone);
static void draw_button(int x0, int y0, int x1, int y1, const char *label, int size);
static void draw_button_white(int x0, int y0, int x1, int y1,
                              const char *label, int size);
static void draw_button_black(int x0, int y0, int x1, int y1,
                              const char *label, int size);

static void draw_seed_bar(void) {
    /* A light blue-gray HUD keeps contrast without pairing navy with the lawn. */
    rect(0, 0, GAME_W - 1, LAWN_Y - 1, COL(220, 232, 239));
    rect(0, 118, GAME_W - 1, LAWN_Y - 1, COL(103, 180, 200));
    rect(0, LAWN_Y + 1, LAWN_X - 8, GAME_H - 1, COL(239, 241, 234));
    rect(LAWN_X - 7, LAWN_Y + 1, LAWN_X - 1, GAME_H - 1, COL(103, 180, 200));
    rect(12, 12, LAWN_X - 19, 131, COL(93, 114, 135));
    rect(17, 17, LAWN_X - 24, 126, COL(245, 242, 232));
    draw_coin_icon(60, 71, 26);
    draw_text(101, 37, 2, COL(36, 59, 82), "МОНЕТЫ");
    draw_int(101, 67, 5, COL(36, 59, 82), coin_balance);
    for (int i = 0; i < PT_COUNT; i++) {
        int x0 = BATTLE_CARD_X, y0 = BATTLE_CARD_Y + i * BATTLE_CARD_STEP;
        int affordable = coin_balance >= PDEF[i].cost && cooldown[i] <= 0 &&
                         (i != PT_LILY || level == WATER_LEVEL);
        int inset = selected == i ? 5 : 3;
        rect(x0 - 3, y0 - 3, x0 + BATTLE_CARD_W + 3,
             y0 + BATTLE_CARD_H + 3, COL(36, 59, 82));
        rect(x0 - 3 + inset, y0 - 3 + inset,
             x0 + BATTLE_CARD_W + 3 - inset,
             y0 + BATTLE_CARD_H + 3 - inset,
             selected == i ? COL(241, 197, 110) : COL(255, 253, 248));
        sprite_draw(PDEF[i].sprite, x0 + 78, y0 + 2, 55, 55, 0);
        draw_text_c(x0 + BATTLE_CARD_W / 2, y0 + 59, 2,
                    COL(36, 59, 82), PDEF[i].short_name);
        draw_coin_icon(x0 + 81, y0 + 88, 8);
        draw_int(x0 + 102, y0 + 77, 2, COL(36, 59, 82), PDEF[i].cost);
        if (!affordable) rect_blend(x0, y0, x0 + BATTLE_CARD_W,
                                     y0 + BATTLE_CARD_H, COL(80, 105, 126), 95);
        if (cooldown[i] > 0) {
            float frac = cooldown[i] / PDEF[i].recharge;
            rect_blend(x0, y0, x0 + BATTLE_CARD_W,
                       y0 + (int)(BATTLE_CARD_H * frac), COL(32, 53, 75), 95);
        }
    }

    rect(LAWN_X, 0, GAME_W - 1, LAWN_Y - 1, COL(220, 232, 239));
    rect(LAWN_X, LAWN_Y - 5, GAME_W - 1, LAWN_Y - 1, COL(103, 180, 200));
    /* Defeated enemies fill the wave meter; then the boss meter shows HP. */
    int defeated = total_zombies - game_wave_remaining();
    if (defeated < 0) defeated = 0;
    if (defeated > total_zombies) defeated = total_zombies;
    int bar = total_zombies ? defeated * 300 / total_zombies : 0;
    char progress[24];
    snprintf(progress, sizeof(progress), "%d/%d", defeated, total_zombies);
    if (boss_phase) {
        for (int i = 0; i < ZMAX; i++)
            if (zomb[i].active && zomb[i].type == EN_ROBOT) {
                float health = fmaxf(0, fminf(1, zomb[i].hp / zomb[i].maxhp));
                bar = (int)(300 * health);
                snprintf(progress, sizeof(progress), "%d%%", (int)ceilf(100 * health));
                break;
            }
    }
    draw_text(282, 20, 3, COL(36, 59, 82), boss_phase ? "РОБОТ" : "ВОЛНА");
    draw_text(462, 24, 2, COL(83, 106, 128), progress);
    rect(281, 65, 585, 89, COL(103, 180, 200));
    if (bar > 0) rect(283, 67, 283 + bar, 87, COL(241, 197, 110));
    draw_text(638, 20, 3, COL(36, 59, 82), "УРОВЕНЬ");
    draw_int(829, 20, 3, COL(36, 59, 82), level);
    draw_text(641, 69, 2, COL(83, 106, 128), LEVEL_NAMES[level - 1]);
    draw_button_white(925, 28, 1080, 88, "КНИГА", 3);
    draw_button_white(1100, 28, 1260, 88, "МЕНЮ", 3);
}

/* UI: all buttons have real hit rectangles handled in game_input_press. */
static void draw_button_tone(int x0, int y0, int x1, int y1,
                             const char *label, int size, int active, int tone) {
    int inset = active ? 5 : 3;
    int height = y1 - y0 + 1;
    int radius = height / 4;
    if (radius > 18) radius = 18;
    if (radius < 7) radius = 7;
    int black = tone == BUTTON_TONE_BLACK;
    /* The «black» tone is a true black: navy read as bluish on phones. */
    uint32_t frame = black ? COL(0, 0, 0) : COL(36, 59, 82);
    uint32_t face = black ? COL(0, 0, 0) :
                    tone == BUTTON_TONE_WHITE ? COL(255, 253, 248) :
                    tone == BUTTON_TONE_ACCENT ? COL(230, 142, 112) :
                                                 COL(220, 232, 239);
    uint32_t text = black ? COL(255, 253, 248) : COL(36, 59, 82);
    if (active && !black) face = COL(241, 197, 110);
    round_rect(x0 + 1, y0 + 5, x1 - 1, y1 + 5, radius, COL(190, 201, 207));
    round_rect(x0, y0, x1, y1, radius, frame);
    round_rect(x0 + inset, y0 + inset, x1 - inset, y1 - inset,
               radius > inset ? radius - inset : 1, face);
    while (size > 1 && text_w(size, label) > x1 - x0 - 22) size--;
    draw_text_c((x0 + x1) / 2, (y0 + y1 - size * 7) / 2,
                size, text, label);
}

static void draw_button(int x0, int y0, int x1, int y1,
                        const char *label, int size) {
    draw_button_tone(x0, y0, x1, y1, label, size, 0, BUTTON_TONE_GRAY);
}

static void draw_button_white(int x0, int y0, int x1, int y1,
                              const char *label, int size) {
    draw_button_tone(x0, y0, x1, y1, label, size, 0, BUTTON_TONE_WHITE);
}

static void draw_button_black(int x0, int y0, int x1, int y1,
                              const char *label, int size) {
    draw_button_tone(x0, y0, x1, y1, label, size, 0, BUTTON_TONE_BLACK);
}

static void draw_dima(int cx, int feet_y, int size) {
    /* Show the author's masked Dima as-is, without an invented body. */
    sprite_draw(SPR_MASK, cx - size / 2, feet_y - size, size, size, 0);
    draw_text_c(cx, feet_y + 8, 3, COL(255, 253, 248), "ДИМА");
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
    draw_text_c(cx, feet_y + 8, 3, COL(255, 253, 248), "ХЛЕБУШЕК");
}

static void draw_kirill(int cx, int feet_y, int size) {
    /* The author's updated Kirill drawing, assets/art/kirill.png. */
    sprite_draw(SPR_KIRILL, cx - size / 2, feet_y - size, size, size, 0);
    draw_text_c(cx, feet_y + 8, 3, COL(255, 253, 248), "КИРИЛЛ");
}

static void draw_menu_hero(int image, int cx, int feet_y, int size,
                           const char *name) {
    ellipse(cx, feet_y - 2, size / 2 - 20, 13, COL(124, 145, 159));
    sprite_draw(image, cx - size / 2, feet_y - size, size, size, 0);
    draw_text_c(cx, feet_y + 26, 3, COL(36, 59, 82), name);
}

static void draw_menu(void) {
    /* Full-colour character drawings sit on calm, raised interface surfaces. */
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(233, 239, 241));
    rect(0, 510, GAME_W - 1, GAME_H - 1, COL(244, 240, 230));
    round_rect(30, 18, 1250, 151, 22, COL(32, 53, 75));
    draw_button_white(52, 31, 421, 124, "Уровни игроков", 3);
    draw_button_white(475, 38, 785, 127, "УРОВНИ 1-10", 4);
    draw_button_white(850, 38, 1240, 132, "Сад Дзен", 4);

    round_rect(44, 160, 1236, 536, 26, COL(255, 253, 248));
    draw_text_c(640, 176, 5, COL(36, 59, 82), "Растения против гусей 3");
    ellipse(256, 359, 119, 107, COL(217, 237, 242));
    ellipse(640, 359, 119, 107, COL(249, 229, 219));
    ellipse(1024, 359, 119, 107, COL(244, 236, 214));
    draw_menu_hero(SPR_KHLEBUSHEK, 256, 461, 190, "Хлебушек");
    draw_menu_hero(SPR_MASK, 640, 461, 205, "Дима в маске");
    draw_menu_hero(SPR_KIRILL, 1024, 461, 190, "Кирилл");

    round_rect(44, 545, 1236, 695, 24, COL(255, 253, 248));
    draw_button_white(98, 568, 392, 665, "КНИГА", 5);
    draw_button_tone(440, 548, 840, 674, "СТАРТ", 8, 0, BUTTON_TONE_ACCENT);
    draw_button_white(893, 569, 1223, 661, "ОНЛАЙН", 6);
}

/* Level 0 replays the story; any wave can be replayed. */
static void draw_level_select(void) {
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(233, 239, 241));
    round_rect(32, 18, 1248, 118, 22, COL(255, 253, 248));
    draw_text_c(630, 32, 6, COL(36, 59, 82), "ВЫБОР УРОВНЯ");
    draw_button_black(1045, 18, 1265, 85, "НАЗАД", 4);
    for (int n = 1; n <= MAX_LEVEL; n++) {
        int col = (n - 1) % 5, row = (n - 1) / 5;
        int x = 100 + col * 220, y = 155 + row * 190;
        int active = resume_level == n;
        int inset = active ? 5 : 3;
        round_rect(x, y, x + 180, y + 130, 15, COL(36, 59, 82));
        round_rect(x + inset, y + inset, x + 180 - inset, y + 130 - inset,
                   12, active ? COL(241, 197, 110) : COL(255, 253, 248));
        int water_art = !preferences_neutral_background_enabled() &&
                        n == WATER_LEVEL && sprite_pixels[SPR_WATER_MAP];
        if (water_art) {
            sprite_crop(SPR_WATER_MAP, x + inset, y + inset,
                        180 - 2 * inset, 130 - 2 * inset,
                        0, 0, 500, 500, 0);
            rect_blend(x + inset, y + inset, x + 180 - inset,
                       y + 130 - inset, COL(36, 59, 82), 145);
        }
        char label[32];
        uint32_t text_color = water_art ? COL(255, 253, 248) : COL(36, 59, 82);
        snprintf(label, sizeof(label), "УРОВЕНЬ %d", n);
        draw_text_c(x + 90, y + 12, 3, text_color, label);
        draw_text_c(x + 90, y + 56, 2, text_color, LEVEL_NAMES[n - 1]);
        if (n == MAX_LEVEL) snprintf(label, sizeof(label), "%d + РОБОТ", 5 + n * 3);
        else snprintf(label, sizeof(label), "%d ВРАГОВ", 5 + n * 3);
        draw_text_c(x + 90, y + 97, 2, text_color, label);
    }
    draw_button_white(425, 567, 855, 641, "УРОВЕНЬ 0: КАТ-СЦЕНА", 4);
    draw_button_white(880, 567, 1240, 641, "КАТАЛОГ УРОВНЕЙ", 3);
}

/* The fallback garden uses the same flat button style as every other screen. */
static void draw_garden_button(int x0, int y0, int x1, int y1,
                               const char *text, int size, int active, int tone) {
    draw_button_tone(x0, y0, x1, y1, text, size, active, tone);
}

/* Plants and geese share a free-placement garden; campaign rules and currency
 * are not involved. The map choice only changes the artwork beneath them. */
static void draw_garden(void) {
    draw_background();
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int item = garden[r][c];
            if (item >= 0 && item < PT_COUNT) {
                draw_plant(CELL_CX(c), CELL_CY(r), item,
                           sinf(global_t * 2 + r + c));
            } else if (item >= PT_COUNT && item < PT_COUNT + GARDEN_DUCK_COUNT) {
                ellipse(CELL_CX(c), CELL_CY(r) + 36, 41, 8, COL(48, 120, 34));
                draw_duck_variant(CELL_CX(c) - 46, CELL_CY(r) - 49, 92,
                                  GARDEN_DUCKS[item - PT_COUNT], 0);
            }
        }

    if (use_lvgl_ui) return; /* LVGL paints the packets and garden actions. */
    if (sprite_pixels[SPR_MAP])
        sprite_crop(SPR_MAP, LAWN_X, 0, GAME_W - LAWN_X, LAWN_Y,
                    0, 0, 242, 135, 0);
    else rect(LAWN_X, 0, GAME_W - 1, LAWN_Y - 1, COL(96, 64, 40));
    rect_blend(0, 0, GAME_W - 1, LAWN_Y - 1, COL(32, 53, 75), 148);
    rect(LAWN_X, LAWN_Y - 5, GAME_W - 1, LAWN_Y - 1, COL(70, 46, 28));
    rect(0, 0, GAME_W - 1, 124, COL(232, 228, 212));
    draw_text(28, 13, 4, COL(36, 59, 82), "САД ДЗЕН");
    draw_garden_button(8, 43, 116, 78, "РАСТЕНИЯ", 1, !garden_mode,
                       BUTTON_TONE_WHITE);
    draw_garden_button(124, 43, 232, 78, "ГУСИ", 1, garden_mode,
                       BUTTON_TONE_WHITE);
    draw_garden_button(8, 81, 116, 116, "ГАЗОН", 1, garden_map == 1,
                       BUTTON_TONE_WHITE);
    draw_garden_button(124, 81, 232, 116, "ВОДА", 1,
                       garden_map == WATER_LEVEL, BUTTON_TONE_WHITE);

    int count = garden_mode ? GARDEN_DUCK_COUNT : PT_COUNT;
    for (int i = 0; i < count; i++) {
        int x0 = garden_card_x(i);
        int active = garden_selected == i;
        int inset = active ? 5 : 3;
        rect(x0 - 3, 9, x0 + CARD_W + 3, 115, COL(36, 59, 82));
        rect(x0 - 3 + inset, 9 + inset,
             x0 + CARD_W + 3 - inset, 115 - inset,
             active ? COL(241, 197, 110) : COL(255, 253, 248));
        if (garden_mode) {
            draw_duck_variant(x0 + (CARD_W - 60) / 2, 19, 60,
                              GARDEN_DUCKS[i], 0);
            draw_text_c(x0 + CARD_W / 2, 84, 2, COL(36, 59, 82),
                        GARDEN_DUCK_NAMES[i]);
        } else {
            sprite_draw(PDEF[i].sprite, x0 + (CARD_W - 60) / 2, 19, 60, 60, 0);
            int size = text_w(2, PDEF[i].short_name) <= CARD_W - 8 ? 2 : 1;
            draw_text_c(x0 + CARD_W / 2, 84, size,
                        COL(36, 59, 82), PDEF[i].short_name);
        }
    }
    draw_garden_button(915, 16, 1070, 58, "КНИГА", 2, 0,
                       BUTTON_TONE_WHITE);
    draw_garden_button(1080, 16, 1265, 58, "В МЕНЮ", 2, 0,
                       BUTTON_TONE_BLACK);
    draw_garden_button(915, 73, 1265, 116, "УБРАТЬ", 2,
                       garden_selected == PT_COUNT, BUTTON_TONE_GRAY);
    rect_blend(95, 679, 1185, 717, COL(32, 53, 75), 220);
    draw_text_c(640, 691, 2, COL(255, 253, 248),
                "СВОБОДНО СТАВЬ РАСТЕНИЯ И ГУСЕЙ. ВСЁ БЕСПЛАТНО.");
}

/* Interactive plant book: the list and details come from the SAME plant
 * definitions as the packets and Zen Garden, so it cannot list fake plants. */
static void draw_book(void) {
    /* An open, warm-paper spread with clear blue-gray ink. */
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(232, 228, 212));
    rect(0, 0, GAME_W - 1, 105, COL(220, 232, 239));
    draw_text_c(565, 28, 5, COL(36, 59, 82), "УМНАЯ КНИГА КИРИЛЛА");
    draw_button_black(1045, 18, 1265, 85, "НАЗАД", 4);
    rect(76, 121, 1209, 697, COL(36, 59, 82));
    rect(84, 115, 1201, 686, COL(102, 128, 148));
    rect(92, 122, 557, 677, COL(255, 253, 248));
    rect(567, 122, 1192, 677, COL(225, 236, 242));
    rect(550, 122, 568, 677, COL(140, 159, 174));
    rect(554, 131, 564, 667, COL(176, 198, 211));
    draw_button_tone(160, 123, 344, 158, "РАСТЕНИЯ", 2,
                     !book_enemy_tab, BUTTON_TONE_WHITE);
    draw_button_tone(355, 123, 540, 158, "ВРАГИ", 2,
                     book_enemy_tab, BUTTON_TONE_WHITE);

    int count = book_enemy_tab ? BOOK_ENEMY_COUNT : PT_COUNT;
    for (int i = 0; i < count; i++) {
        int y0 = BOOK_ENTRY_Y + i * BOOK_ENTRY_STEP;
        int type = book_enemy_tab ? BOOK_ENEMIES[i] : i;
        int highlighted = (book_enemy_tab ? book_enemy_selected : book_selected) == i;
        int inset = highlighted ? 5 : 3;
        rect(160, y0, 540, y0 + BOOK_ENTRY_H, COL(36, 59, 82));
        rect(160 + inset, y0 + inset, 540 - inset,
             y0 + BOOK_ENTRY_H - inset,
             highlighted ? COL(241, 197, 110) : COL(255, 253, 248));
        if (book_enemy_tab) {
            if (type == EN_ROBOT) sprite_draw(SPR_ROBOT, 183, y0 + 11, 67, 67, 0);
            else draw_duck_variant(183, y0 + 15, 63, type, 1);
            draw_text(265, y0 + 28, 2, COL(36, 59, 82), EN_NAMES[type]);
        } else {
            sprite_draw(PDEF[i].sprite, 183, y0 + 11, 67, 67, 0);
            draw_text(265, y0 + 28, 2, COL(36, 59, 82), PDEF[i].short_name);
        }
    }

    if (book_enemy_tab) {
        int type = BOOK_ENEMIES[book_enemy_selected];
        if (type == EN_ROBOT) sprite_draw(SPR_ROBOT, 762, 158, 210, 210, 0);
        else draw_duck_variant(762, 163, 208, type, 1);
        int title_size = 4;
        while (title_size > 2 && text_w(title_size, EN_NAMES[type]) > 565) title_size--;
        draw_text_c(865, 380, title_size, COL(36, 59, 82), EN_NAMES[type]);
        GameBookEntry entry;
        game_book_entry(1, book_enemy_selected, &entry);
        rect(580, 438, 1150, 663, COL(176, 198, 211));
        rect(585, 443, 1145, 658, COL(247, 243, 232));
        draw_text(598, 473, 2, COL(36, 59, 82), entry.description);
        draw_text(598, 542, 2, COL(36, 59, 82), entry.detail);
        return;
    }

    const PlantDef *p = &PDEF[book_selected];
    sprite_draw(p->sprite, 756, 148, 222, 222, 0);
    int title_size = 4;
    while (title_size > 2 && text_w(title_size, p->name) > 565) title_size--;
    draw_text_c(865, 380, title_size, COL(36, 59, 82), p->name);
    /* Only the drawing and its purpose belong on the book page. Combat
     * prices, recharge times and small stat captions obscured the story. */
    rect(580, 438, 1150, 663, COL(176, 198, 211));
    rect(585, 443, 1145, 658, COL(247, 243, 232));
    draw_text(598, 473, 2, COL(36, 59, 82), p->description);
    draw_text(598, 542, 2, COL(36, 59, 82), p->detail);
}

static void draw_intro(void) {
    draw_background();
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(32, 53, 75), 105);
    if (!use_lvgl_ui) {
        rect(0, 0, GAME_W - 1, 115, COL(220, 232, 239));
        draw_text_c(535, 37, 6, COL(36, 59, 82), "УРОВЕНЬ 0: КАТ-СЦЕНА");
        draw_button_black(1020, 10, 1270, 89, "ПРОПУСТИТЬ", 3);
    }
    draw_khlebushek(245, 492, 246, intro_step == 0);
    /* Dima really walks toward Khlebushek in the first shot. */
    float approach = intro_t / 2.8f;
    if (approach > 1 || intro_step > 0) approach = 1;
    int dx = 935 - (int)(430 * approach);
    draw_dima(dx, 492, 240);
    if (intro_step >= 2) draw_kirill(977, 492, 248);
    if (use_lvgl_ui) return; /* LVGL owns the dialog and skip/next controls. */

    rect(95, 533, 1185, 679, COL(36, 59, 82));
    rect(102, 540, 1178, 672, COL(247, 243, 232));
    rect(111, 548, 1169, 665, COL(232, 228, 212));
    const char *speaker = intro_step == 0 ? "ХЛЕБУШЕК" :
                          intro_step == 1 ? "ДИМА" : "КИРИЛЛ";
    const char *line = intro_step == 0 ? "ХЛЕБУШЕК ПЛАЧЕТ..." :
                       intro_step == 1 ? "НЕ ПЛАЧЬ, МЫ НОВОГО СДЕЛАЕМ." :
                       "МОЖЕТ, КТО-ТО ПОМОГАТЬ МНЕ БУДЕТ?";
    draw_text(145, 554, 3, COL(83, 106, 128), speaker);
    int size = 5;
    while (size > 2 && text_w(size, line) > 990) size--;
    draw_text_c(640, 602, size, COL(36, 59, 82), line);
}

static void draw_play_scene(void) {
    draw_background();
    draw_mowers();
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            Plant *p = &grid[r][c];
            if (lily[r][c]) draw_lily(CELL_CX(c), CELL_CY(r));
            if (p->type >= 0)
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
    /* Native LVGL already shows the wave and boss health, with no extra
     * legacy captions or black banners painted over the author's map. */
    if (use_lvgl_ui) return;
    draw_seed_bar();
    if (banner_t > 0 && banner_text && phase == PH_PLAY) {
        int s = 3;
        while (s > 1 && text_w(s, banner_text) > GAME_W - 120) s--;
        int bw = text_w(s, banner_text) + 44;
        rect_blend(640 - bw / 2, 156, 640 + bw / 2, 172 + 7 * s,
                   COL(32, 53, 75), 205);
        draw_text_c(640, 165, s, COL(255, 253, 248), banner_text);
    }
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].active && zomb[i].type == EN_ROBOT) {
            float bf = zomb[i].hp / zomb[i].maxhp;
            if (bf < 0) bf = 0;
            if (bf > 1) bf = 1;
            rect(395, 684, 885, 717, COL(36, 59, 82));
            rect(401, 690, 401 + (int)(478 * bf), 711, COL(241, 197, 110));
            draw_text_c(640, 697, 2, COL(255, 253, 248), "КОРОЛЕВА В РОБОТЕ");
            break;
        }
}

static void draw_result(void) {
    rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(32, 53, 75), 199);
    rect(258, 175, 1022, 645, COL(36, 59, 82));
    rect(265, 182, 1015, 638, COL(247, 243, 232));
    if (phase == PH_LEVEL_CLEAR) {
        draw_text_c(640, 246, 7, COL(36, 59, 82), "УРОВЕНЬ ПРОЙДЕН!");
        draw_button_white(390, 450, 890, 565, "ДАЛЬШЕ", 7);
        draw_button_black(480, 580, 800, 645, "В МЕНЮ", 3);
    } else if (phase == PH_LOSE) {
        draw_text_c(640, 254, 7, COL(36, 59, 82),
                    level == 10 && boss_phase ? "РОБОТ УНИЧТОЖИЛ ВСЕХ!" : "ЗАЩИТА ПРОРВАНА!");
        draw_button_white(335, 460, 645, 565, "ПОВТОРИТЬ", 4);
        draw_button_black(660, 460, 975, 565, "В МЕНЮ", 4);
    } else if (phase == PH_WIN) {
        draw_text_c(640, 237, 7, COL(36, 59, 82), "РОБОТ ОСТАНОВЛЕН!");
        draw_button_black(425, 460, 855, 580, "В МЕНЮ", 6);
    }
}

/* Native rooms and fight — same framebuffer, touch input and author's art as
 * the offline game. A square opens search; only '+' creates a room. */
static void draw_online_rooms(void) {
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(239, 241, 234));
    rect(0, 0, GAME_W - 1, 124, COL(220, 232, 239));
    sprite_draw(SPR_KHLEBUSHEK, 32, 10, 106, 104, 0);
    draw_text(168, 22, 5, COL(36, 59, 82), "ОНЛАЙН");
    draw_text(170, 78, 2, COL(83, 106, 128), "ИГРАЙ ЗА РАСТЕНИЯ ИЛИ УТОК");
    draw_button_black(1070, 17, 1255, 87, "МЕНЮ", 3);
    rect(48, 134, 1231, 231, COL(36, 59, 82));
    rect(55, 140, 1224, 224, COL(247, 243, 232));
    draw_button_tone(70, 144, 303, 222, "ГАЗОН", 3,
                     online_map == 1, BUTTON_TONE_WHITE);
    draw_button_tone(313, 144, 550, 222, "ВОДА", 3,
                     online_map == 5, BUTTON_TONE_WHITE);
    /* The search affordance is visibly a SQUARE, not the create button. */
    draw_button(720, 144, 884, 222, "", 2);
    rect(754, 166, 796, 202, COL(36, 59, 82));
    rect(757, 169, 793, 199, COL(241, 197, 110));
    rect(762, 174, 788, 194, COL(78, 105, 126));
    draw_text(806, 171, 2, COL(36, 59, 82), "ПОИСК");
    draw_button_white(904, 144, 1065, 222, "+ СОЗДАТЬ", 2);
    draw_button_white(1070, 144, 1223, 222, "МАСТЕРСКАЯ", 1);
    rect(45, 239, 1234, 711, COL(36, 59, 82));
    rect(52, 245, 1227, 704, COL(241, 238, 228));
    draw_text(70, 260, 4, COL(36, 59, 82), "КОМНАТЫ");
    draw_button(1019, 248, 1225, 308, "ОБНОВИТЬ", 2);
    int indexes[ON_ROOM_LIST_CAP], count = online_filtered(indexes);
    int page = online_page;
    if (page * 8 >= count) page = 0;
    if (online_view.busy) draw_text(350, 273, 2, COL(83, 106, 128), "ПОДКЛЮЧАЕМСЯ...");
    else if (!online_view.connected && !online_view.notice[0])
        draw_text(350, 273, 2, COL(83, 106, 128), "ЗАГРУЖАЕМ КОМНАТЫ...");
    else if (online_view.notice[0]) {
        int size = text_w(2, online_view.notice) < 730 ? 2 : 1;
        draw_text(362, 276, size, COL(83, 106, 128), online_view.notice);
    } else {
        char label[50];snprintf(label, sizeof label, "НАЙДЕНО: %d", count);
        draw_text(366, 275, 2, COL(83, 106, 128), label);
    }
    if (!count) draw_text_c(640, 459, 4, COL(83, 106, 128),
                           "ПОКА НЕТ СВОБОДНЫХ КОМНАТ");
    for (int i = 0; i < 8; i++) {
        int pos = page * 8 + i;
        if (pos >= count) break;
        const OnRoomSummary *room = &online_view.rooms[indexes[pos]];
        int col = i % 2, row = i / 2, x = 69 + col * 582, y = 321 + row * 80;
        rect(x, y + 2, x + 557, y + 71, COL(36, 59, 82));
        rect(x + 3, y + 5, x + 554, y + 68,
             (i & 1) ? COL(226, 237, 243) : COL(255, 253, 248));
        draw_text(x + 28, y + 13, 4, COL(36, 59, 82), room->id);
        draw_text(x + 236, y + 17, 2, COL(50, 71, 91),
                  room->map == 5 ? "ВОДА" : "ГАЗОН");
        draw_text(x + 362, y + 17, 2, COL(36, 59, 82), "ВОЙТИ >");
    }
    if (count > 8) {
        draw_button(62, 654, 374, 704, "НАЗАД", 2);
        draw_button(873, 654, 1220, 704, "ДАЛЬШЕ", 2);
        char page_text[48];
        snprintf(page_text, sizeof page_text, "%d / %d", page + 1, (count + 7) / 8);
        draw_text_c(640, 670, 2, COL(83, 106, 128), page_text);
    }
    if (online_search) {
        rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(32, 53, 75), 193);
        rect(222, 92, 1058, 667, COL(36, 59, 82));
        rect(229, 99, 1051, 660, COL(247, 243, 232));
        draw_text_c(640, 116, 5, COL(36, 59, 82), "ПОИСК КОМНАТЫ");
        draw_button_black(902, 109, 1009, 159, "X", 3);
        rect(301, 171, 837, 237, COL(70, 94, 114));
        rect(309, 179, 829, 229, COL(255, 253, 248));
        if (online_code[0]) draw_text_c(569, 184, 4, COL(36, 59, 82), online_code);
        else draw_text_c(569, 185, 3, COL(104, 125, 143), "КОД ИЗ 6 СИМВОЛОВ");
        draw_button(847, 171, 993, 236, "СТЕРЕТЬ", 2);
        const char *keys = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        for (int i = 0; i < 32; i++) {
            char letter[2] = {keys[i], 0};
            int kx = 291 + (i % 9) * 78, ky = 270 + (i / 9) * 70;
            draw_button_white(kx, ky, kx + 68, ky + 58, letter, 3);
        }
        char matches[80];snprintf(matches, sizeof matches, "ПОДХОДЯЩИХ КОМНАТ: %d", count);
        draw_text_c(640, 542, 2, COL(83, 106, 128), matches);
        draw_button_white(395, 568, 880, 644, "ВОЙТИ ПО КОДУ", 4);
    }
}

static void draw_online_lobby(void) {
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(239, 241, 234));
    rect(0, 0, GAME_W - 1, 115, COL(220, 232, 239));
    char heading[72];snprintf(heading, sizeof heading, "КОМНАТА %s", online_view.room_id);
    draw_text(60, 26, 5, COL(36, 59, 82), heading);
    draw_button_black(1093, 17, 1265, 91, "ВЫЙТИ", 3);
    draw_text_c(640, 145, 3, COL(36, 59, 82),
                online_view.map == 5 ? "КАРТА: ВОДА" : "КАРТА: ГАЗОН");
    rect(148, 229, 1132, 592, COL(36, 59, 82));
    rect(156, 237, 1124, 585, COL(247, 243, 232));
    int mine = online_role(), other = online_view.slot == ON_SLOT_HOST ?
                                 online_view.guest_role : online_view.host_role;
    for (int side = ON_ROLE_PLANTS; side <= ON_ROLE_ZOMBIES; side++) {
        int x = side == ON_ROLE_PLANTS ? 160 : 663;
        int inset = mine == side ? 5 : 3;
        rect(x, 243, x + 451, 574, COL(36, 59, 82));
        rect(x + inset, 243 + inset, x + 451 - inset, 574 - inset,
             mine == side ? COL(241, 197, 110) :
             other == side ? COL(226, 237, 243) : COL(255, 253, 248));
        sprite_draw(side == ON_ROLE_PLANTS ? SPR_PEA : SPR_DUCK,
                    x + 125, 268, 185, 188, side == ON_ROLE_ZOMBIES);
        draw_text_c(x + 225, 478, 4, COL(36, 59, 82),
                    side == ON_ROLE_PLANTS ? "РАСТЕНИЯ" : "ЗОМБИ");
        draw_text_c(x + 225, 525, 2, COL(36, 59, 82),
                    mine == side ? "ТВОЯ СТОРОНА" :
                    other == side ? "СТОРОНА СОПЕРНИКА" : "ВЫБРАТЬ СТОРОНУ");
    }
    rect(148, 603, 1132, 690, COL(36, 59, 82));
    if (!online_view.guest_id[0])
        draw_text_c(640, 611, 3, COL(255, 253, 248), "ЖДЁМ ВТОРОГО ИГРОКА...");
    else if (online_view.slot == ON_SLOT_GUEST && !online_view.has_state)
        draw_text_c(640, 611, 3, COL(255, 253, 248), "ХОЗЯИН ЗАПУСКАЕТ БОЙ...");
    if (online_view.notice[0])
        draw_text_c(640, 665, 2, COL(232, 240, 244), online_view.notice);
}

static void draw_online_match(void) {
    if (!online_has_match) {draw_online_lobby();return;}
    const OnMatch *s = &online_match;
    draw_background();
    for (int r = 0; r < ON_ROWS; r++) {
        const OnMower *m = &s->mowers[r];
        if (!m->used || m->running)
            sprite_draw(SPR_MOWER, (int)m->x - 44, CELL_CY(r) - 25, 88, 86, 0);
        for (int c = 0; c < ON_COLS; c++) {
            int pos = r * ON_COLS + c, x = CELL_CX(c), y = CELL_CY(r);
            if (s->lilies[pos]) draw_lily(x, y);
            if (s->plants[pos].type >= 0)
                draw_plant(x, y, s->plants[pos].type, sinf(s->time * 2 + r + c));
        }
    }
    for (int i = 0; i < s->duck_count; i++) {
        Zombie z = {0};
        z.row = s->ducks[i].row;z.x = s->ducks[i].x;
        z.type = s->ducks[i].type;z.anim = s->ducks[i].anim;
        draw_enemy(&z);
    }
    for (int i = 0; i < s->pea_count; i++)
        disc((int)s->peas[i].x, (int)s->peas[i].y, 9, COL(120, 210, 90));
    for (int i = 0; i < s->coin_count; i++)
        draw_coin_icon((int)s->coins[i].x, (int)s->coins[i].y, 22);
    /* With LVGL active only the author's map and moving entities are drawn
     * here. The LVGL tree provides touchable cards, status, book and result. */
    if (use_lvgl_ui) return;
    rect(0, 0, GAME_W - 1, 119, COL(220, 232, 239));
    int role = online_role(), plants = role == ON_ROLE_PLANTS;
    draw_text(21, 18, 3, COL(36, 59, 82), plants ? "РАСТЕНИЯ" : "ЗОМБИ");
    draw_coin_icon(46, 82, 21);
    draw_int(80, 69, 4, COL(36, 59, 82),
             plants ? s->plant_cash : s->zombie_cash);
    char title[98];
    snprintf(title, sizeof title, "КОМНАТА %s    УТОК ОСТАЛОСЬ: %d",
             online_view.room_id, s->left + s->duck_count);
    draw_text(269, 22, 3, COL(36, 59, 82), title);

    draw_button_white(917, 20, 1088, 91, "КНИГА", 3);
    draw_button_black(1093, 17, 1265, 91, "ВЫЙТИ", 3);
    int n = plants ? PT_COUNT : 3;
    for (int i = 0; i < n; i++) {
        int y = plants ? 125 + i * 104 : 151 + i * 113;
        int cost = plants ? on_plant_cost[i] : on_duck_cost[i];
        float delay = plants ? s->plant_cooldown[i] : s->duck_cooldown[i];
        int affordable = (plants ? s->plant_cash : s->zombie_cash) >= cost &&
                         delay <= 0 && (plants ? (i != ON_LILY || s->map == 5) : s->left > 0);
        int inset = online_selected == i ? 5 : 3;
        rect(12, y, 235, y + 98, COL(36, 59, 82));
        rect(12 + inset, y + inset, 235 - inset, y + 98 - inset,
             online_selected == i ? COL(241, 197, 110) : COL(255, 253, 248));
        if (plants) sprite_draw(PDEF[i].sprite, 27, y + 8, 67, 65, 0);
        else draw_duck_variant(26, y + 9, 64, on_duck_type[i], 1);
        draw_text(99, y + 16, 2, COL(36, 59, 82),
                  plants ? PDEF[i].short_name : EN_NAMES[on_duck_type[i]]);
        draw_coin_icon(113, y + 67, 12);
        draw_int(136, y + 57, 3, COL(36, 59, 82), cost);
        if (!affordable) rect_blend(17, y + 4, 230, y + 93, COL(80, 105, 126), 105);
    }
    if (!plants)
        draw_button_white(15, 615, 232, 702, "ЗАКОНЧИТЬ", 2);
    if (s->winner) {
        rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(32, 53, 75), 198);
        rect(283, 197, 997, 585, COL(36, 59, 82));
        rect(290, 204, 990, 578, COL(247, 243, 232));
        draw_text_c(640, 246, 6, COL(36, 59, 82),
                    s->winner == role ? "ПОБЕДА!" : "ПОБЕДИЛ СОПЕРНИК");
        draw_text_c(640, 336, 3, COL(83, 106, 128),
                    s->winner == ON_WIN_PLANTS ? "РАСТЕНИЯ ПОБЕДИЛИ" : "УТКИ ПОБЕДИЛИ");
        draw_button_black(411, 459, 869, 560, "В КОМНАТЫ", 5);
    }
}

#define CUSTOM_TILE_W 80.0f
#define CUSTOM_TILE_H 72.0f
#define CUSTOM_WORLD_LIMIT ((float)ON_LEVEL_WORLD_LIMIT)

static float custom_world_clamp(float value, float size) {
    if (value < -CUSTOM_WORLD_LIMIT) return -CUSTOM_WORLD_LIMIT;
    if (value > CUSTOM_WORLD_LIMIT - size) return CUSTOM_WORLD_LIMIT - size;
    return value;
}
typedef struct {float x, y, depth;} CustomContact;
static int custom_object_runtime_index(const OnLevelObject *object) {
    if (!object || custom_object_count <= 0) return -1;
    uintptr_t base = (uintptr_t)&custom_level.objects[0];
    uintptr_t address = (uintptr_t)object;
    if (address < base) return -1;
    uintptr_t offset = address - base;
    size_t bytes = (size_t)custom_object_count * sizeof custom_level.objects[0];
    if (offset >= bytes || offset % sizeof custom_level.objects[0]) return -1;
    return (int)(offset / sizeof custom_level.objects[0]);
}
static unsigned custom_id_hash(int id) {
    return ((uint32_t)id * 2654435761u) & (CUSTOM_ID_MAP_CAP - 1u);
}
static void custom_build_id_map(void) {
    memset(custom_id_map, 0, sizeof custom_id_map);
    for (int index = 0; index < custom_object_count; ++index) {
        int id = custom_level.objects[index].id;
        if (id <= 0 || id > 1000000) continue;
        unsigned slot = custom_id_hash(id);
        while (custom_id_map[slot] &&
               custom_level.objects[custom_id_map[slot] - 1].id != id)
            slot = (slot + 1u) & (CUSTOM_ID_MAP_CAP - 1u);
        if (!custom_id_map[slot]) custom_id_map[slot] = index + 1;
    }
}
static int custom_id_lookup(int id) {
    if (id <= 0 || id > 1000000) return -1;
    unsigned slot = custom_id_hash(id);
    for (unsigned probes = 0; probes < CUSTOM_ID_MAP_CAP; ++probes) {
        int stored = custom_id_map[slot];
        if (!stored) return -1;
        int index = stored - 1;
        if (index >= 0 && index < custom_object_count &&
            custom_level.objects[index].id == id) return index;
        slot = (slot + 1u) & (CUSTOM_ID_MAP_CAP - 1u);
    }
    return -1;
}
static int custom_object_collision_disabled(const OnLevelObject *object) {
    int index = custom_object_runtime_index(object);
    return index >= 0 && custom_collision_disabled[index] != 0;
}
static int custom_object_is_invisible(const OnLevelObject *object) {
    int index = custom_object_runtime_index(object);
    return index >= 0 && custom_invisible[index] != 0;
}
static void custom_hide_object(OnLevelObject *object) {
    int index = custom_object_runtime_index(object);
    if (index >= 0) custom_invisible[index] = 1;
}
static void custom_disable_object_collision(OnLevelObject *object) {
    int index = custom_object_runtime_index(object);
    if (index < 0) return;
    custom_collision_disabled[index] = 1;
    if (object->type == ON_LEVEL_PLAYER) custom_player_collision_enabled = 0;
}
/* Collide with the artwork silhouette; slopes additionally supply an uphill response. */
static int custom_player_triangle_contact(float px, float py, float pw, float ph,
                                           const OnLevelObject *object,
                                           const float u[3], const float v[3],
                                           int climbable, float vx, float vy,
                                           CustomContact *contact) {
    float radians = object->angle * 0.01745329251994329577f;
    float c = cosf(radians), s = sinf(radians);
    float object_x = (object->x + object->w * .5f) * CUSTOM_TILE_W;
    float object_y = (object->y + object->h * .5f) * CUSTOM_TILE_H;
    float vertices[3][2];
    for (int i = 0; i < 3; ++i) {
        float local_u = object->flip_x ? 1.0f - u[i] : u[i];
        float local_v = object->flip_y ? 1.0f - v[i] : v[i];
        float local_x = (local_u - .5f) * object->w * CUSTOM_TILE_W;
        float local_y = (local_v - .5f) * object->h * CUSTOM_TILE_H;
        vertices[i][0] = object_x + local_x * c - local_y * s;
        vertices[i][1] = object_y + local_x * s + local_y * c;
    }
    float axes_x[5] = {1.0f, 0.0f, 0, 0, 0};
    float axes_y[5] = {0.0f, 1.0f, 0, 0, 0};
    int axis_count = 2;
    for (int i = 0; i < 3; ++i) {
        int next = (i + 1) % 3;
        float edge_x = vertices[next][0] - vertices[i][0];
        float edge_y = vertices[next][1] - vertices[i][1];
        float length = hypotf(edge_x, edge_y);
        if (length > 1e-7f) {
            axes_x[axis_count] = -edge_y / length;
            axes_y[axis_count] = edge_x / length;
            axis_count++;
        }
    }
    float player_x = px + pw * .5f, player_y = py + ph * .5f;
    float smallest_depth = INFINITY, normal_x = 0, normal_y = 0;
    float climb_depth = INFINITY, climb_x = 0, climb_y = 0;
    for (int i = 0; i < axis_count; ++i) {
        float axis_x = axes_x[i], axis_y = axes_y[i];
        float object_min = INFINITY, object_max = -INFINITY;
        for (int j = 0; j < 3; ++j) {
            float projection = vertices[j][0] * axis_x + vertices[j][1] * axis_y;
            if (projection < object_min) object_min = projection;
            if (projection > object_max) object_max = projection;
        }
        float player_center = player_x * axis_x + player_y * axis_y;
        float player_radius = pw * .5f * fabsf(axis_x) +
                              ph * .5f * fabsf(axis_y);
        float player_min = player_center - player_radius;
        float player_max = player_center + player_radius;
        float move_positive = object_max - player_min;
        float move_negative = player_max - object_min;
        if (move_positive <= 0 || move_negative <= 0) return 0;
        float depth = fminf(move_positive, move_negative);
        float motion = vx * axis_x + vy * axis_y;
        int positive = move_positive < move_negative - 1e-6f ||
            (fabsf(move_positive - move_negative) <= 1e-6f && motion <= 0);
        float candidate_x = axis_x * (positive ? 1.0f : -1.0f);
        float candidate_y = axis_y * (positive ? 1.0f : -1.0f);
        if (depth < smallest_depth) {
            smallest_depth = depth;
            normal_x = candidate_x;normal_y = candidate_y;
        }
        float candidate_motion = vx * candidate_x + vy * candidate_y;
        if (fabsf(candidate_x) > .1f && candidate_y < -.2f &&
            candidate_motion < -1e-6f && depth < climb_depth) {
            climb_depth = depth;climb_x = candidate_x;climb_y = candidate_y;
        }
    }
    if (climbable && climb_depth < INFINITY) {
        if (contact) *contact = (CustomContact){climb_x, climb_y, climb_depth};
    } else if (contact) {
        *contact = (CustomContact){normal_x, normal_y, smallest_depth};
    }
    return 1;
}
static int custom_player_slope_contact(float px, float py, float pw, float ph,
                                        const OnLevelObject *object,
                                        float vx, float vy, CustomContact *contact) {
    static const float u[3] = {0.0f, 1.0f, 1.0f};
    static const float v[3] = {1.0f, 0.0f, 1.0f};
    return custom_player_triangle_contact(px, py, pw, ph, object,
                                          u, v, 1, vx, vy, contact);
}
static int custom_player_hazard_contact(float px, float py, float pw, float ph,
                                         const OnLevelObject *object,
                                         float vx, float vy, CustomContact *contact) {
    static const float u[3] = {.5f, 1.0f, 0.0f};
    static const float v[3] = {.03f, 1.0f, 1.0f};
    return custom_player_triangle_contact(px, py, pw, ph, object,
                                          u, v, 0, vx, vy, contact);
}
static void custom_art_alpha_bounds(int type, int flip_x, int flip_y,
                                    float *left, float *top,
                                    float *right, float *bottom) {
    *left = *top = 0.0f;*right = *bottom = 1.0f;
    switch (type) {
    case ON_LEVEL_PLAYER:
        *left = .21f;*top = .02f;*right = .80f;*bottom = .96f;break;
    case ON_LEVEL_ENEMY:
        *left = .20f;*top = .21f;*right = .99f;*bottom = .99f;break;
    case ON_LEVEL_COIN:
        *left = .05f;*top = .03f;*right = .96f;*bottom = .97f;break;
    case ON_LEVEL_GOAL:
        *left = .04f;*top = .03f;break;
    case ON_LEVEL_CHECKPOINT:
        *left = .14f;*right = .75f;break;
    case ON_LEVEL_PORTAL_NORMAL:
        *left = .16f;*right = .77f;break;
    case ON_LEVEL_PORTAL_JETPACK:
        *left = .17f;*right = .76f;break;
    default: break;
    }
    if (flip_x) {
        float old_left = *left;
        *left = 1.0f - *right;*right = 1.0f - old_left;
    }
    if (flip_y) {
        float old_top = *top;
        *top = 1.0f - *bottom;*bottom = 1.0f - old_top;
    }
}
static void custom_player_visible_hitbox(float *x, float *y,
                                         float *w, float *h) {
    const OnLevelObject *player = custom_player_index >= 0 &&
                                  custom_player_index < custom_object_count ?
                                  &custom_level.objects[custom_player_index] : NULL;
    float left, top, right, bottom;
    custom_art_alpha_bounds(ON_LEVEL_PLAYER,
                            player ? custom_player_facing_left : 0,
                            player ? player->flip_y : 0,
                            &left, &top, &right, &bottom);
    *x = custom_player_x + custom_player_w * left;
    *y = custom_player_y + custom_player_h * top;
    *w = custom_player_w * (right - left);
    *h = custom_player_h * (bottom - top);
}
static int custom_player_in_orb_range(float px, float py, float pw, float ph,
                                        const OnLevelObject *object) {
    float center_x = (object->x + object->w * .5f) * CUSTOM_TILE_W;
    float center_y = (object->y + object->h * .5f) * CUSTOM_TILE_H;
    float radius = fminf(object->w * CUSTOM_TILE_W,
                         object->h * CUSTOM_TILE_H) * .46f;
    float closest_x = fmaxf(px, fminf(center_x, px + pw));
    float closest_y = fmaxf(py, fminf(center_y, py + ph));
    float dx = closest_x - center_x, dy = closest_y - center_y;
    float range = radius + .1f;
    return dx * dx + dy * dy <= range * range;
}
static int custom_activate_orb(void) {
    if (!custom_player_collision_enabled || custom_player_index < 0) return 0;
    float player_x, player_y, player_w, player_h;
    custom_player_visible_hitbox(&player_x, &player_y, &player_w, &player_h);
    for (int i = custom_object_count - 1; i >= 0; --i) {
        OnLevelObject *object = &custom_level.objects[i];
        if ((object->type != ON_LEVEL_ORB_YELLOW &&
             object->type != ON_LEVEL_ORB_ORANGE) || !object->visible ||
            custom_object_collision_disabled(object) ||
            !custom_player_in_orb_range(player_x, player_y, player_w, player_h,
                                        object)) continue;
        custom_player_vy = object->type == ON_LEVEL_ORB_ORANGE ? -1050.0f : -650.0f;
        custom_player_grounded = 0;custom_jump_count = 1;
        return 1;
    }
    return 0;
}
static int custom_player_object_contact(float px, float py, float pw, float ph,
                                        const OnLevelObject *object,
                                        float vx, float vy, CustomContact *contact) {
    if (!object || object->type == ON_LEVEL_PARTICLE) return 0;
    float center_x = (object->x + object->w * .5f) * CUSTOM_TILE_W;
    float center_y = (object->y + object->h * .5f) * CUSTOM_TILE_H;
    float conservative_radius =
        (object->w * CUSTOM_TILE_W + object->h * CUSTOM_TILE_H) * .5f;
    if (fabsf(px + pw * .5f - center_x) > conservative_radius + pw * .5f ||
        fabsf(py + ph * .5f - center_y) > conservative_radius + ph * .5f)
        return 0;
    if (object->type == ON_LEVEL_SLOPE)
        return custom_player_slope_contact(px, py, pw, ph, object, vx, vy, contact);
    if (object->type == ON_LEVEL_HAZARD)
        return custom_player_hazard_contact(px, py, pw, ph, object, vx, vy, contact);
    float left, top, right, bottom;
    int art_flip_x = object->flip_x ^ (object->type == ON_LEVEL_ENEMY);
    custom_art_alpha_bounds(object->type, art_flip_x, object->flip_y,
                            &left, &top, &right, &bottom);
    float radians = object->angle * 0.01745329251994329577f;
    float c = cosf(radians), s = sinf(radians);
    float frame_w = object->w * CUSTOM_TILE_W;
    float frame_h = object->h * CUSTOM_TILE_H;
    float offset_x = ((left + right) * .5f - .5f) * frame_w;
    float offset_y = ((top + bottom) * .5f - .5f) * frame_h;
    float object_x = (object->x + object->w * .5f) * CUSTOM_TILE_W +
                     offset_x * c - offset_y * s;
    float object_y = (object->y + object->h * .5f) * CUSTOM_TILE_H +
                     offset_x * s + offset_y * c;
    float half_object_w = frame_w * (right - left) * .5f;
    float half_object_h = frame_h * (bottom - top) * .5f;
    float half_player_w = pw * .5f, half_player_h = ph * .5f;
    float dx = px + half_player_w - object_x;
    float dy = py + half_player_h - object_y;
    const float axes_x[4] = {1.0f, 0.0f, c, -s};
    const float axes_y[4] = {0.0f, 1.0f, s, c};
    float smallest_overlap = INFINITY, normal_x = 0, normal_y = 0;
    for (int i = 0; i < 4; ++i) {
        float axis_x = axes_x[i], axis_y = axes_y[i];
        float object_radius = half_object_w * fabsf(axis_x * c + axis_y * s) +
            half_object_h * fabsf(-axis_x * s + axis_y * c);
        float player_radius = half_player_w * fabsf(axis_x) +
            half_player_h * fabsf(axis_y);
        float distance = dx * axis_x + dy * axis_y;
        float penetration = object_radius + player_radius - fabsf(distance);
        if (penetration <= 0) return 0;
        if (penetration < smallest_overlap) {
            float motion = vx * axis_x + vy * axis_y;
            float sign = distance > 1e-7f ? 1.0f :
                         distance < -1e-7f ? -1.0f : motion > 0 ? -1.0f : 1.0f;
            smallest_overlap = penetration;
            normal_x = axis_x * sign;normal_y = axis_y * sign;
        }
    }
    if (contact) *contact = (CustomContact){normal_x, normal_y, smallest_overlap};
    return 1;
}

static int custom_solid(const OnLevelObject *o) {
    return o->visible && !custom_object_collision_disabled(o) &&
           (o->type == ON_LEVEL_BLOCK || o->type == ON_LEVEL_GROUND ||
            o->type == ON_LEVEL_SLOPE);
}
static OnLevelObject *custom_find_id(int id) {
    int index = custom_id_lookup(id);
    return index >= 0 ? &custom_level.objects[index] : NULL;
}
static void custom_activate_checkpoint(const OnLevelObject *checkpoint) {
    if (!checkpoint || checkpoint->type != ON_LEVEL_CHECKPOINT) return;
    custom_checkpoint_id = checkpoint->id;
    float player_w_tiles = custom_player_w / CUSTOM_TILE_W;
    float player_h_tiles = custom_player_h / CUSTOM_TILE_H;
    custom_respawn_x = custom_world_clamp(
        checkpoint->x + checkpoint->w * .5f - player_w_tiles * .5f,
        player_w_tiles) * CUSTOM_TILE_W;
    custom_respawn_y = custom_world_clamp(
        checkpoint->y + checkpoint->h - player_h_tiles,
        player_h_tiles) * CUSTOM_TILE_H;
}
static void custom_update_portals(int allow_activation) {
    float player_x, player_y, player_w, player_h;
    custom_player_visible_hitbox(&player_x, &player_y, &player_w, &player_h);
    int form_changed = 0;
    for (int i = 0; i < custom_object_count; ++i) {
        OnLevelObject *portal = &custom_level.objects[i];
        if (portal->type != ON_LEVEL_PORTAL_NORMAL &&
            portal->type != ON_LEVEL_PORTAL_JETPACK) {
            custom_portal_inside[i] = 0;
            continue;
        }
        int touching = allow_activation && portal->visible &&
            !custom_object_collision_disabled(portal) &&
            custom_player_object_contact(player_x, player_y, player_w, player_h,
                portal, custom_player_vx, custom_player_vy, NULL);
        if (!touching) {
            custom_portal_inside[i] = 0;
            continue;
        }
        int entered = !custom_portal_inside[i];
        custom_portal_inside[i] = 1;
        if (!entered || form_changed) continue;
        int jetpack = portal->type == ON_LEVEL_PORTAL_JETPACK;
        if (custom_jetpack_mode == jetpack) continue;
        custom_jetpack_mode = jetpack;
        custom_jetpack_active = 0;
        custom_player_vy = 0;
        custom_player_grounded = 0;
        custom_jump_count = 0;custom_dash_remaining = 0;
        custom_jump_request = 0;
        form_changed = 1;
    }
}

static void custom_player_reset(void) {
    if (custom_player_index < 0 || custom_player_index >= custom_object_count) return;
    custom_player_x = custom_respawn_x;
    custom_player_y = custom_respawn_y;
    custom_player_vx = custom_player_vy = 0;
    custom_player_facing_left = !!custom_level.objects[custom_player_index].flip_x;
    if (custom_level_attempts < 999999) custom_level_attempts++;
    custom_player_grounded = 0;custom_jetpack_active = 0;
    custom_jump_count = 0;custom_dash_remaining = custom_dash_cooldown = 0;
    custom_dash_request = custom_dash_held = 0;
    custom_control_axis = custom_control_vertical = 0;
    memset(custom_portal_inside, 0, sizeof custom_portal_inside);
    custom_jump_request = custom_jump_held = 0;
}

static float custom_level_fall_plane(void) {
    float lowest_object_bottom = custom_level.height > 0 ?
                                (float)custom_level.height : 10.0f;
    for (int i = 0; i < custom_object_count; ++i) {
        const OnLevelObject *object = &custom_level.objects[i];
        if (!object->visible || object->type == ON_LEVEL_TRIGGER ||
            object->type == ON_LEVEL_PARTICLE) continue;
        float object_bottom = object->y + object->h;
        if (object_bottom > lowest_object_bottom)
            lowest_object_bottom = object_bottom;
    }
    float plane = lowest_object_bottom + CUSTOM_FALL_MARGIN_TILES;
    if (plane > CUSTOM_WORLD_LIMIT) plane = CUSTOM_WORLD_LIMIT;
    return plane * CUSTOM_TILE_H;
}

static int custom_platformer_start(const OnPublishedLevel *source) {
    if (!source || source->object_count < 1 ||
        source->object_count > ON_LEVEL_OBJECT_CAP) return 0;
    if (source != &custom_level) custom_level = *source;
    custom_object_count = custom_level.object_count;
    custom_player_index = -1;
    custom_build_id_map();
    int has_player = 0, has_goal = 0;
    for (int i = 0; i < custom_object_count; i++) {
        OnLevelObject *o = &custom_level.objects[i];
        if (o->type == ON_LEVEL_PLAYER) {
            has_player = 1;
            custom_player_index = i;
        }
        has_goal |= o->type == ON_LEVEL_GOAL;
    }
    if (!has_player || !has_goal) return 0;
    memset(custom_trigger_fired, 0, sizeof custom_trigger_fired);
    memset(custom_trigger_active, 0, sizeof custom_trigger_active);
    memset(custom_trigger_touch_inside, 0, sizeof custom_trigger_touch_inside);
    memset(custom_trigger_counts, 0, sizeof custom_trigger_counts);
    memset(custom_trigger_timers, 0, sizeof custom_trigger_timers);
    memset(custom_trigger_touch_timers, 0, sizeof custom_trigger_touch_timers);
    memset(custom_collision_disabled, 0, sizeof custom_collision_disabled);
    memset(custom_invisible, 0, sizeof custom_invisible);
    memset(custom_portal_inside, 0, sizeof custom_portal_inside);
    custom_jetpack_mode = custom_jetpack_active = 0;
    custom_background_color = CUSTOM_BACKGROUND_DEFAULT;
    custom_gravity = 1450.0f;custom_elapsed_time = 0;
    custom_player_collision_enabled = 1;
    custom_group_rotation_count = 0;
    memset(custom_group_rotation_slots, 0, sizeof custom_group_rotation_slots);
    custom_camera_x = custom_camera_y = 0;
    custom_player_w = CUSTOM_TILE_W * 0.65f;
    custom_player_h = CUSTOM_TILE_H * 0.85f;
    custom_checkpoint_id = 0;
    custom_level_attempts = 0;custom_level_total_coins = 0;
    const OnLevelObject *start_player = &custom_level.objects[custom_player_index];
    custom_start_player_x = start_player->x * CUSTOM_TILE_W;
    custom_start_player_y = start_player->y * CUSTOM_TILE_H;
    custom_goal_center_x = custom_goal_center_y = 0;
    for (int i = 0; i < custom_object_count; ++i) {
        const OnLevelObject *object = &custom_level.objects[i];
        if (object->type == ON_LEVEL_COIN)
            custom_level_total_coins++;
        if (object->type == ON_LEVEL_GOAL) {
            custom_goal_center_x = (object->x + object->w * .5f) * CUSTOM_TILE_W;
            custom_goal_center_y = (object->y + object->h * .5f) * CUSTOM_TILE_H;
        }
    }
    custom_respawn_x = custom_start_player_x;
    custom_respawn_y = custom_start_player_y;
    custom_player_reset();
    custom_level_coins = 0;custom_level_won = 0;custom_level_active = 1;
    custom_control_axis = custom_control_vertical = 0;
    custom_jump_request = custom_jump_held = 0;
    custom_trigger_request = custom_trigger_held = 0;
    custom_dash_request = custom_dash_held = 0;
    custom_dash_remaining = custom_dash_cooldown = 0;custom_jump_count = 0;
    custom_fire_triggers(ON_TRIGGER_START);
    custom_fall_plane_y = custom_level_fall_plane();
    return 1;
}
static void custom_platformer_stop(void) {
    custom_level_active = 0;custom_level_won = 0;custom_object_count = 0;
    custom_level_attempts = custom_level_total_coins = custom_level_coins = 0;
    custom_checkpoint_id = 0;custom_respawn_x = custom_respawn_y = 0;
    custom_dash_remaining = custom_dash_cooldown = 0;
    custom_dash_request = custom_dash_held = custom_jump_count = 0;
    custom_fall_plane_y = 0;
    custom_gravity = 1450.0f;custom_elapsed_time = 0;
    custom_group_rotation_count = 0;
    custom_control_axis = custom_control_vertical = 0;
    custom_player_facing_left = 0;
    custom_jump_request = custom_jump_held = 0;
    custom_trigger_request = custom_trigger_held = 0;
    custom_jetpack_mode = custom_jetpack_active = 0;
    custom_background_color = CUSTOM_BACKGROUND_DEFAULT;
    memset(custom_portal_inside, 0, sizeof custom_portal_inside);
}
int game_workshop_preview(const OnPublishedLevel *level) {
    if (phase != PH_WORKSHOP_EDIT || !custom_platformer_start(level)) return 0;
    custom_play_return = PH_WORKSHOP_EDIT;
    phase = PH_CUSTOM_PLAY;
    return 1;
}
static int custom_object_can_manually_recolor(int type);
static void custom_apply_trigger_to_object(const OnLevelObject *trigger,
                                           OnLevelObject *target) {
    if (!trigger || !target) return;
    int action = trigger->trigger_kind == ON_TRIGGER_KIND_ROTATE ?
                 ON_TRIGGER_ROTATE : trigger->trigger_action;
    switch (action) {
    case ON_TRIGGER_TOGGLE: target->visible = !target->visible;break;
    case ON_TRIGGER_ACTIVATE: target->visible = 1;break;
    case ON_TRIGGER_UNACTIVATE: target->visible = 0;break;
    case ON_TRIGGER_INVISIBLE: custom_hide_object(target);break;
    case ON_TRIGGER_NO_COLLISION: custom_disable_object_collision(target);break;
    case ON_TRIGGER_MOVE:
        target->x = custom_world_clamp(target->x + trigger->trigger_value, target->w);
        target->y = custom_world_clamp(target->y + trigger->trigger_value_y, target->h);
        break;
    case ON_TRIGGER_ROTATE:
        target->angle = fmodf(target->angle + trigger->trigger_value, 360.0f);
        if (target->angle < 0) target->angle += 360.0f;
        break;
    case ON_TRIGGER_RECOLOR:
        if (target->type != ON_LEVEL_TRIGGER &&
            custom_object_can_manually_recolor(target->type)) {
            target->color = trigger->trigger_color;
            target->color_default = trigger->trigger_color_default;
        }
        break;
    case ON_TRIGGER_NUMBER:
        target->number = (int)fmaxf(0, fminf(9999, trigger->trigger_value));break;
    default: break;
    }
}
static void custom_start_group_rotation(int group_id, int duration) {
    if (group_id < 0 || group_id > 9999 || duration < 1 || duration > 9999) return;
    int slot = custom_group_rotation_slots[group_id] - 1;
    if (slot >= 0 && slot < custom_group_rotation_count) {
        custom_group_rotations[slot].remaining = (float)duration;
        return;
    }
    if (custom_group_rotation_count >= ON_LEVEL_OBJECT_CAP) return;
    slot = custom_group_rotation_count++;
    custom_group_rotations[slot] = (CustomGroupRotation){group_id, (float)duration, 0};
    custom_group_rotation_slots[group_id] = slot + 1;
}
static void custom_update_group_rotations(float dt) {
    if (!custom_group_rotation_count) return;
    for (int i = 0; i < custom_group_rotation_count; ++i) {
        CustomGroupRotation *rotation = &custom_group_rotations[i];
        rotation->frame_step = fminf(dt, rotation->remaining);
        rotation->remaining -= rotation->frame_step;
    }
    for (int i = 0; i < custom_object_count; ++i) {
        OnLevelObject *object = &custom_level.objects[i];
        if (object->type == ON_LEVEL_TRIGGER || object->number < 0 ||
            object->number > 9999) continue;
        int slot = custom_group_rotation_slots[object->number] - 1;
        if (slot < 0 || slot >= custom_group_rotation_count) continue;
        object->angle = fmodf(object->angle +
            CUSTOM_GROUP_ROTATION_DEGREES_PER_SECOND *
            custom_group_rotations[slot].frame_step, 360.0f);
    }
    for (int i = 0; i < custom_group_rotation_count;) {
        if (custom_group_rotations[i].remaining > .00001f) {
            ++i;
            continue;
        }
        custom_group_rotation_slots[custom_group_rotations[i].group_id] = 0;
        int last = --custom_group_rotation_count;
        if (i != last) {
            custom_group_rotations[i] = custom_group_rotations[last];
            custom_group_rotation_slots[custom_group_rotations[i].group_id] = i + 1;
        }
    }
}
static void custom_execute_trigger(OnLevelObject *trigger) {
    if (!trigger || trigger->type != ON_LEVEL_TRIGGER) return;
    if (trigger->trigger_kind == ON_TRIGGER_KIND_BACKGROUND) {
        custom_background_color = trigger->trigger_color_default ?
            CUSTOM_BACKGROUND_DEFAULT : trigger->trigger_color & 0xffffffu;
        return;
    }
    if (trigger->trigger_kind == ON_TRIGGER_KIND_GRAVITY) {
        float offset = fmaxf(-100.0f, fminf(100.0f, trigger->trigger_value));
        custom_gravity = 1450.0f + offset * 10.0f;
        return;
    }
    if (trigger->trigger_kind == ON_TRIGGER_KIND_SPAWN) {
        if (trigger->trigger_has_group) {
            for (int i = 0; i < custom_object_count; ++i) {
                OnLevelObject *object = &custom_level.objects[i];
                if (object != trigger && object->type != ON_LEVEL_TRIGGER &&
                    object->number == trigger->trigger_group_id)
                    object->visible = 1;
            }
        }
        return;
    }
    if (trigger->trigger_kind == ON_TRIGGER_KIND_FOREVER) {
        if (trigger->trigger_has_group) {
            int activate = trigger->trigger_action == ON_TRIGGER_ACTIVATE;
            for (int i = 0; i < custom_object_count; ++i) {
                OnLevelObject *object = &custom_level.objects[i];
                if (object != trigger && object->type != ON_LEVEL_TRIGGER &&
                    object->number == trigger->trigger_group_id)
                    object->visible = activate;
            }
        } else {
            /* Continue to read older records whose forever kind was a timer loop. */
            for (int i = 0; i < custom_object_count; ++i) {
                OnLevelObject *first = &custom_level.objects[i];
                if (first != trigger && first->visible &&
                    first->type == ON_LEVEL_TRIGGER &&
                    first->trigger_kind != ON_TRIGGER_KIND_FOREVER) {
                    custom_execute_trigger(first);
                    break;
                }
            }
        }
        return;
    }
    if (trigger->trigger_kind == ON_TRIGGER_KIND_ROTATE &&
        trigger->trigger_has_duration) {
        if (trigger->trigger_has_group)
            custom_start_group_rotation(trigger->trigger_group_id,
                                        trigger->trigger_duration);
        return;
    }
    if (trigger->trigger_has_group) {
        for (int i = 0; i < custom_object_count; ++i) {
            OnLevelObject *target = &custom_level.objects[i];
            if (target != trigger && target->type != ON_LEVEL_TRIGGER &&
                target->number == trigger->trigger_group_id)
                custom_apply_trigger_to_object(trigger, target);
        }
    } else {
        custom_apply_trigger_to_object(trigger, custom_find_id(trigger->target_id));
    }
}
static int custom_trigger_legacy_loop(const OnLevelObject *trigger) {
    return trigger && trigger->trigger_kind == ON_TRIGGER_KIND_FOREVER &&
           !trigger->trigger_has_group;
}
static void custom_activate_trigger(int index) {
    if (index < 0 || index >= custom_object_count) return;
    OnLevelObject *trigger = &custom_level.objects[index];
    if (!trigger->visible || custom_object_collision_disabled(trigger) ||
        trigger->type != ON_LEVEL_TRIGGER) return;
    if (custom_trigger_legacy_loop(trigger)) {
        if (!custom_trigger_active[index]) {
            custom_trigger_active[index] = 1;
            custom_trigger_timers[index] = 0;
        }
        return;
    }
    if (trigger->trigger_kind == ON_TRIGGER_KIND_COUNT) {
        if (custom_trigger_fired[index]) return;
        if (custom_trigger_counts[index] < 999)
            custom_trigger_counts[index]++;
        int threshold = trigger->trigger_count;
        if (threshold < 1) threshold = 3;
        if (custom_trigger_counts[index] >= threshold) {
            custom_trigger_fired[index] = 1;
            custom_execute_trigger(trigger);
        }
        return;
    }
    if (trigger->trigger_kind == ON_TRIGGER_KIND_TOGGLE) {
        custom_execute_trigger(trigger);
        return;
    }
    if (custom_trigger_fired[index]) return;
    custom_trigger_fired[index] = 1;
    custom_execute_trigger(trigger);
}
static void custom_fire_triggers(int event) {
    for (int i = 0; i < custom_object_count; i++) {
        OnLevelObject *trigger = &custom_level.objects[i];
        if (!trigger->visible || custom_object_collision_disabled(trigger) ||
            trigger->type != ON_LEVEL_TRIGGER ||
            trigger->trigger_event != event) continue;
        custom_activate_trigger(i);
    }
}
static void custom_update_touch_triggers(float dt) {
    float player_x, player_y, player_w, player_h;
    custom_player_visible_hitbox(&player_x, &player_y, &player_w, &player_h);
    for (int i = 0; i < custom_object_count; ++i) {
        OnLevelObject *trigger = &custom_level.objects[i];
        if (trigger->type != ON_LEVEL_TRIGGER ||
            trigger->trigger_event != ON_TRIGGER_TOUCH) {
            custom_trigger_touch_inside[i] = 0;
            custom_trigger_touch_timers[i] = 0;
            continue;
        }
        if (!trigger->visible || custom_object_collision_disabled(trigger)) {
            custom_trigger_touch_inside[i] = 0;
            custom_trigger_touch_timers[i] = 0;
            continue;
        }
        int inside = custom_player_object_contact(player_x, player_y,
            player_w, player_h, trigger, custom_player_vx, custom_player_vy, NULL);
        int was_inside = custom_trigger_touch_inside[i];
        custom_trigger_touch_inside[i] = (uint8_t)!!inside;
        int mode = trigger->trigger_touch_mode;
        if (mode < ON_TRIGGER_TOUCH_ENTER || mode > ON_TRIGGER_TOUCH_STAY)
            mode = ON_TRIGGER_TOUCH_ENTER;
        int activated = 0;
        if (mode == ON_TRIGGER_TOUCH_ENTER) {
            activated = inside && !was_inside;
        } else if (mode == ON_TRIGGER_TOUCH_EXIT) {
            activated = !inside && was_inside;
        } else if (!inside) {
            custom_trigger_touch_timers[i] = 0;
        } else if (!was_inside) {
            /* Stay triggers fire once on contact, then repeat every quarter
             * second while the player's hitbox remains inside. */
            activated = 1;
            custom_trigger_touch_timers[i] = 0;
        } else {
            custom_trigger_touch_timers[i] += dt;
            if (custom_trigger_touch_timers[i] >= .25f) {
                custom_trigger_touch_timers[i] -= .25f;
                activated = 1;
            }
        }
        if (activated) custom_activate_trigger(i);
    }
}
static void custom_run_forever_triggers(float dt) {
    for (int i = 0; i < custom_object_count; ++i) {
        OnLevelObject *trigger = &custom_level.objects[i];
        if (trigger->type != ON_LEVEL_TRIGGER ||
            trigger->trigger_kind != ON_TRIGGER_KIND_FOREVER ||
            trigger->trigger_has_group || !trigger->visible ||
            custom_object_collision_disabled(trigger)) continue;
        if (!custom_trigger_active[i]) {
            custom_trigger_active[i] = 1;custom_trigger_timers[i] = 0;
            continue;
        }
        custom_trigger_timers[i] += dt;
        if (custom_trigger_timers[i] >= .5f) {
            custom_trigger_timers[i] -= .5f;
            custom_execute_trigger(trigger);
        }
    }
}
static int custom_resolve_player_solids(void) {
    if (!custom_player_collision_enabled) return 0;
    int grounded = 0;
    for (int iteration = 0; iteration < 4; ++iteration) {
        int collided = 0;
        for (int i = 0; i < custom_object_count; ++i) {
            OnLevelObject *object = &custom_level.objects[i];
            if (!custom_solid(object)) continue;
            float player_x, player_y, player_w, player_h;
            custom_player_visible_hitbox(&player_x, &player_y, &player_w, &player_h);
            CustomContact contact;
            if (!custom_player_object_contact(player_x, player_y,
                    player_w, player_h, object, custom_player_vx,
                    custom_player_vy, &contact)) continue;
            custom_player_x += contact.x * contact.depth;
            custom_player_y += contact.y * contact.depth;
            if (fabsf(contact.x) > .7f) {
                custom_player_wall_contact = 1;
                if (custom_dash_remaining > 0) custom_dash_remaining = 0;
                if ((custom_level.movement_abilities & ON_LEVEL_ABILITY_WALL_SLIDE) &&
                    custom_player_vy > 0 && custom_player_vy > 140.0f)
                    custom_player_vy = 140.0f;
            }
            float inward_velocity = custom_player_vx * contact.x +
                                    custom_player_vy * contact.y;
            if (inward_velocity < 0) {
                custom_player_vx -= inward_velocity * contact.x;
                custom_player_vy -= inward_velocity * contact.y;
            }
            if (contact.y < (object->type == ON_LEVEL_SLOPE ? -.35f : -.5f))
                grounded = 1;
            collided = 1;
        }
        if (!collided) break;
    }
    return grounded;
}
static void custom_platformer_update(float dt) {
    if (!custom_level_active || custom_level_won) return;
    if (dt < 0) dt = 0;
    if (dt > .05f) dt = .05f;
    custom_elapsed_time += dt;
    custom_player_wall_contact = 0;
    custom_dash_cooldown = fmaxf(0.0f, custom_dash_cooldown - dt);
    if (custom_player_grounded) custom_jump_count = 0;
    custom_player_vx = (float)custom_control_axis * 250.0f;
    if (custom_control_axis < 0) custom_player_facing_left = 1;
    else if (custom_control_axis > 0) custom_player_facing_left = 0;
    float jetpack_velocity = custom_control_vertical ?
        -(float)custom_control_vertical * 250.0f : 0.0f;
    if (!custom_jetpack_mode && custom_dash_request &&
        (custom_level.movement_abilities & ON_LEVEL_ABILITY_DASH) &&
        custom_dash_cooldown <= 0) {
        custom_dash_direction = custom_control_axis ? custom_control_axis :
                                custom_player_facing_left ? -1 : 1;
        custom_dash_remaining = .18f;
        custom_dash_cooldown = .65f;
        custom_player_vy = 0;
    }
    custom_dash_request = 0;
    if (custom_jetpack_mode) {
        custom_player_vy = jetpack_velocity;
    } else if (custom_jump_request) {
        if (!custom_activate_orb()) {
            if (custom_player_grounded) {
                custom_player_vy = -570.0f;custom_player_grounded = 0;
                custom_jump_count = 1;
            } else if ((custom_level.movement_abilities &
                        ON_LEVEL_ABILITY_DOUBLE_JUMP) && custom_jump_count < 2) {
                custom_player_vy = -520.0f;custom_jump_count = 2;
            }
        }
    }
    custom_jump_request = 0;
    if (custom_dash_remaining > 0) {
        custom_player_vx = custom_dash_direction * 650.0f;
        custom_player_vy = 0;
    }
    custom_jetpack_active = custom_jetpack_mode &&
        (custom_control_axis != 0 || custom_control_vertical != 0);
    float predicted_vy = (custom_jetpack_mode || custom_dash_remaining > 0) ?
        custom_player_vy : fminf(780.0f, custom_player_vy + custom_gravity * dt);
    float displacement = fmaxf(fabsf(custom_player_vx * dt),
                                fabsf(predicted_vy * dt));
    int substeps = (int)ceilf(displacement / 4.0f);
    if (substeps < 1) substeps = 1;
    if (substeps > 16) substeps = 16;
    float sub_dt = dt / (float)substeps;
    custom_player_grounded = 0;
    custom_player_wall_contact = 0;
    for (int step = 0; step < substeps; ++step) {
        if (custom_dash_remaining > 0)
            custom_player_vy = 0;
        else if (custom_jetpack_mode)
            custom_player_vy = jetpack_velocity;
        else
            custom_player_vy = fminf(780.0f,
                custom_player_vy + custom_gravity * sub_dt);
        custom_player_x += custom_player_vx * sub_dt;
        if (custom_player_x < -CUSTOM_WORLD_LIMIT * CUSTOM_TILE_W)
            custom_player_x = -CUSTOM_WORLD_LIMIT * CUSTOM_TILE_W;
        if (custom_player_x > CUSTOM_WORLD_LIMIT * CUSTOM_TILE_W - custom_player_w)
            custom_player_x = CUSTOM_WORLD_LIMIT * CUSTOM_TILE_W - custom_player_w;
        custom_player_y += custom_player_vy * sub_dt;
        custom_player_grounded = custom_resolve_player_solids();
        if (custom_dash_remaining > 0)
            custom_dash_remaining = fmaxf(0.0f, custom_dash_remaining - sub_dt);
    }
    if (custom_player_grounded) custom_jump_count = 0;
    int player_respawned = 0;
    int fell_below_level = custom_player_y + custom_player_h >
                           custom_fall_plane_y;
    int escaped_world_top = custom_player_y + custom_player_h <
                            -CUSTOM_WORLD_LIMIT * CUSTOM_TILE_H;
    if (fell_below_level || escaped_world_top) {
        /* A fall is handled like a spike hit: return to the latest checkpoint,
         * or to the level start if the player has not reached one yet. */
        custom_player_reset();
        player_respawned = 1;
    }
    if (custom_player_collision_enabled && !player_respawned) {
        float player_x, player_y, player_w, player_h;
        custom_player_visible_hitbox(&player_x, &player_y, &player_w, &player_h);
        for (int i = 0; i < custom_object_count; ++i) {
            OnLevelObject *object = &custom_level.objects[i];
            if (!object->visible ||
                (object->type != ON_LEVEL_HAZARD && object->type != ON_LEVEL_ENEMY) ||
                custom_object_collision_disabled(object) ||
                !custom_player_object_contact(player_x, player_y,
                    player_w, player_h, object, custom_player_vx,
                    custom_player_vy, NULL)) continue;
            custom_player_reset();player_respawned = 1;break;
        }
        if (!player_respawned) {
            custom_player_visible_hitbox(&player_x, &player_y, &player_w, &player_h);
            for (int i = 0; i < custom_object_count; ++i) {
                OnLevelObject *object = &custom_level.objects[i];
                if (!object->visible || object->type == ON_LEVEL_PLAYER ||
                    object->type == ON_LEVEL_TRIGGER || object->type == ON_LEVEL_PARTICLE ||
                    object->type == ON_LEVEL_ORB_YELLOW ||
                    object->type == ON_LEVEL_ORB_ORANGE ||
                    object->type == ON_LEVEL_HAZARD || object->type == ON_LEVEL_ENEMY ||
                    custom_object_collision_disabled(object) ||
                    !custom_player_object_contact(player_x, player_y,
                        player_w, player_h, object, custom_player_vx,
                        custom_player_vy, NULL)) continue;
                if (object->type == ON_LEVEL_COIN) {
                    object->visible = 0;custom_level_coins++;
                    custom_fire_triggers(ON_TRIGGER_COIN);
                } else if (object->type == ON_LEVEL_GOAL) {
                    custom_level_won = 1;
                } else if (object->type == ON_LEVEL_CHECKPOINT) {
                    custom_activate_checkpoint(object);
                }
            }
        }
    }
    custom_update_portals(custom_player_collision_enabled && !player_respawned);
    custom_jetpack_active = custom_jetpack_mode &&
        (custom_control_axis != 0 || custom_control_vertical != 0);
    custom_update_touch_triggers(dt);
    if (custom_trigger_request) custom_fire_triggers(ON_TRIGGER_MANUAL);
    custom_trigger_request = 0;
    custom_run_forever_triggers(dt);
    custom_update_group_rotations(dt);
}
static int custom_object_can_manually_recolor(int type) {
    return type == ON_LEVEL_BLOCK || type == ON_LEVEL_GROUND ||
           type == ON_LEVEL_HAZARD || type == ON_LEVEL_TRIGGER ||
           type == ON_LEVEL_SLOPE || type == ON_LEVEL_PARTICLE;
}
static void custom_draw_object(const OnLevelObject *o) {
    if (!o || o->type == ON_LEVEL_TRIGGER || o->type == ON_LEVEL_PARTICLE ||
        o->type == ON_LEVEL_PLAYER) return;
    float pulse = o->pulse ? 1.0f + .07f *
        sinf(fmaxf(0.0f, custom_elapsed_time) * 6.28318530718f * 1.6f) : 1.0f;
    float shake = o->shake ?
        sinf(custom_elapsed_time * 37.0f + (o->id % 4093) * .13f) * 2.5f : 0.0f;
    float base_w = o->w * CUSTOM_TILE_W, base_h = o->h * CUSTOM_TILE_H;
    int w = (int)lrintf(base_w * pulse), h = (int)lrintf(base_h * pulse);
    int x = (int)lrintf((o->x * CUSTOM_TILE_W - custom_camera_x) +
                        (base_w - w) * .5f + shake);
    int y = (int)lrintf((o->y * CUSTOM_TILE_H - custom_camera_y) +
                        (base_h - h) * .5f);
    if (w < 3 || h < 3 || o->alpha <= 0) return;
    int previous_alpha = sprite_alpha_multiplier;
    int object_alpha = o->alpha > 100 ? 100 : o->alpha;
    sprite_alpha_multiplier = (object_alpha * 255 + 50) / 100;
    int rotated = fabsf(o->angle) >= .01f;
    uint32_t tint = COL((o->color >> 16) & 255u,
                        (o->color >> 8) & 255u, o->color & 255u);
    /* Objects flagged as default keep the author's own colours: no tint. */
    int tint_opacity = (custom_object_can_manually_recolor(o->type) && !o->color_default) ? 128 : 0;
#define DRAW_LEVEL_ART(id) do { \
        if (tint_opacity > 0 && rotated) \
            sprite_draw_rotated_tinted_flipped((id), x, y, w, h, \
                o->angle, o->flip_x, o->flip_y, tint, tint_opacity); \
        else if (tint_opacity > 0) \
            sprite_draw_tinted_flipped((id), x, y, w, h, \
                o->flip_x, o->flip_y, tint, tint_opacity); \
        else if (rotated) \
            sprite_draw_rotated_flipped((id), x, y, w, h, o->angle, \
                o->flip_x, o->flip_y); \
        else sprite_draw_flipped((id), x, y, w, h, o->flip_x, o->flip_y); \
    } while (0)
    switch (o->type) {
    case ON_LEVEL_BLOCK:
        DRAW_LEVEL_ART(PV_ART_LEVEL_BLOCK);break;
    case ON_LEVEL_GROUND: {
        /* Repeat supplied platform art across the width; rotation draws as one object. */
        if (rotated) DRAW_LEVEL_ART(PV_ART_LEVEL_PLATFORM);
        else {
            const int tile_w = (int)(CUSTOM_TILE_W * 2.0f);
            const int tile_h = (int)CUSTOM_TILE_H;
            for (int dy = 0; dy < h; dy += tile_h)
                for (int dx = 0; dx < w; dx += tile_w) {
                    int draw_w = w - dx < tile_w ? w - dx : tile_w;
                    int draw_h = h - dy < tile_h ? h - dy : tile_h;
                    if (tint_opacity > 0)
                        sprite_draw_tinted_flipped(PV_ART_LEVEL_PLATFORM,
                            x + dx, y + dy, draw_w, draw_h,
                            o->flip_x, o->flip_y, tint, tint_opacity);
                    else
                        sprite_draw_flipped(PV_ART_LEVEL_PLATFORM,
                            x + dx, y + dy, draw_w, draw_h,
                            o->flip_x, o->flip_y);
                }
        }
        break;
    }
    case ON_LEVEL_HAZARD:
        DRAW_LEVEL_ART(PV_ART_LEVEL_SPIKE);break;
    case ON_LEVEL_SLOPE:
        DRAW_LEVEL_ART(PV_ART_LEVEL_SLOPE);break;
    case ON_LEVEL_COIN:
        DRAW_LEVEL_ART(PV_ART_COIN);break;
    case ON_LEVEL_ORB_YELLOW:
        DRAW_LEVEL_ART(PV_ART_LEVEL_ORB_YELLOW);break;
    case ON_LEVEL_ORB_ORANGE:
        DRAW_LEVEL_ART(PV_ART_LEVEL_ORB_ORANGE);break;
    case ON_LEVEL_ENEMY: {
        int flip_x = !o->flip_x;
        if (rotated) sprite_draw_rotated_flipped(PV_ART_DUCK, x, y, w, h,
            o->angle, flip_x, o->flip_y);
        else sprite_draw_flipped(PV_ART_DUCK, x, y, w, h,
                                 flip_x, o->flip_y);
        break;
    }
    case ON_LEVEL_PLAYER: /* The moving player is rendered separately. */
        break;
    case ON_LEVEL_GOAL:
        DRAW_LEVEL_ART(PV_ART_LEVEL_FLAG);break;
    case ON_LEVEL_CHECKPOINT:
        DRAW_LEVEL_ART(custom_checkpoint_id == o->id ?
                       PV_ART_LEVEL_CHECKPOINT_ACTIVE :
                       PV_ART_LEVEL_CHECKPOINT_INACTIVE);break;
    case ON_LEVEL_PORTAL_NORMAL:
        DRAW_LEVEL_ART(PV_ART_LEVEL_PORTAL_NORMAL);break;
    case ON_LEVEL_PORTAL_JETPACK:
        DRAW_LEVEL_ART(PV_ART_LEVEL_PORTAL_JETPACK);break;
    case ON_LEVEL_TRIGGER:
        break; /* Trigger textures are editor-only; triggers stay hidden in play. */
    }
#undef DRAW_LEVEL_ART
    sprite_alpha_multiplier = previous_alpha;
}
static void custom_particle_dot(float x, float y, float radius,
                                uint32_t color, float opacity) {
    if (opacity <= .01f) return;
    if (opacity > 1.0f) opacity = 1.0f;
    int cx = (int)lrintf(x), cy = (int)lrintf(y);
    int extent = (int)ceilf(radius);
    if (extent < 1) extent = 1;
    float radius_sq = radius * radius;
    if (radius_sq < 1.0f) radius_sq = 1.0f;
    for (int dy = -extent; dy <= extent; ++dy)
        for (int dx = -extent; dx <= extent; ++dx) {
            float distance_sq = (float)(dx * dx + dy * dy);
            if (distance_sq > radius_sq) continue;
            float edge = .72f + .28f * (1.0f - distance_sq / radius_sq);
            int alpha = (int)lrintf(255.0f * opacity * edge);
            if (alpha > 0) setpixA(cx + dx, cy + dy, color, alpha);
        }
}
static uint32_t custom_particle_color(const OnLevelObject *object) {
    if (object->type == ON_LEVEL_ORB_YELLOW) return COL(255, 248, 107);
    if (object->type == ON_LEVEL_ORB_ORANGE) return COL(255, 187, 102);
    return COL((object->color >> 16) & 255u,
               (object->color >> 8) & 255u, object->color & 255u);
}
static void custom_draw_particle_object(const OnLevelObject *object,
                                       int *particle_budget) {
    if (!object || !particle_budget || !object->visible || object->alpha <= 0 ||
        (object->type != ON_LEVEL_PARTICLE &&
         object->type != ON_LEVEL_ORB_YELLOW &&
         object->type != ON_LEVEL_ORB_ORANGE) ||
        custom_object_is_invisible(object)) return;
    float effect_alpha = fminf(100.0f, (float)object->alpha) / 100.0f;
    float pulse = object->pulse ? 1.0f + .07f *
        sinf(fmaxf(0.0f, custom_elapsed_time) * 6.28318530718f * 1.6f) : 1.0f;
    float shake = object->shake ?
        sinf(custom_elapsed_time * 37.0f + (object->id % 4093) * .13f) * 2.5f : 0.0f;
    float width = object->w * CUSTOM_TILE_W * pulse;
    float height = object->h * CUSTOM_TILE_H * pulse;
    float center_x = (object->x + object->w * .5f) * CUSTOM_TILE_W -
                     custom_camera_x + shake;
    float center_y = (object->y + object->h * .5f) * CUSTOM_TILE_H - custom_camera_y;
    float extent = fmaxf(width, height) + 32.0f;
    if (object->type == ON_LEVEL_PARTICLE) {
        float life = object->emitter.lifetime;
        extent += object->emitter.speed * life +
            (object->emitter.gravity_enabled ?
             .5f * object->emitter.gravity * life * life : 0.0f);
    }
    if (center_x + extent < 0 || center_x - extent >= GAME_W ||
        center_y + extent < 102 || center_y - extent >= GAME_H) return;
    float radians = object->angle * 0.01745329251994329577f;
    float c = cosf(radians), s = sinf(radians);
    uint32_t color = custom_particle_color(object);
    if (object->type == ON_LEVEL_ORB_YELLOW ||
        object->type == ON_LEVEL_ORB_ORANGE) {
        const int count = 5;
        float orbit = fminf(width, height) * .58f;
        for (int i = 0; i < count; ++i) {
            float phase = custom_elapsed_time * 2.1f +
                i * 6.2831853071795864769f / count +
                (object->id % 4093) * .023f;
            float local_x = cosf(phase) * orbit * (object->flip_x ? -1.0f : 1.0f);
            float local_y = sinf(phase) * orbit * .7f *
                            (object->flip_y ? -1.0f : 1.0f);
            float x = center_x + local_x * c - local_y * s;
            float y = center_y + local_x * s + local_y * c;
            float flicker = .5f + .5f * sinf(phase * 1.6f);
            custom_particle_dot(x, y, (1.2f + flicker * .8f) * pulse,
                                color, (.18f + flicker * .62f) * effect_alpha);
        }
        return;
    }
    const OnLevelParticle *emitter = &object->emitter;
    if (*particle_budget <= 0 || !emitter->enabled) return;
    for (int i = 0; i < ON_LEVEL_PARTICLE_MAX_VISIBLE &&
                    *particle_budget > 0; ++i) {
        float local_x, local_y, size, opacity;
        if (!on_level_particle_sample(emitter, object->id,
                custom_elapsed_time, i, width, height,
                &local_x, &local_y, &size, &opacity)) continue;
        local_x *= object->flip_x ? -1.0f : 1.0f;
        local_y *= object->flip_y ? -1.0f : 1.0f;
        float x = center_x + local_x * c - local_y * s;
        float y = center_y + local_x * s + local_y * c;
        opacity *= effect_alpha;
        if (emitter->glow)
            custom_particle_dot(x, y, size * 1.5f, color, opacity * .16f);
        custom_particle_dot(x, y, fmaxf(.7f, size * .5f), color, opacity);
        --*particle_budget;
    }
}
static int custom_draw_order_compare(const void *left, const void *right) {
    int a = *(const int *)left, b = *(const int *)right;
    if (!custom_draw_sort_level || a < 0 || b < 0) return a - b;
    const OnLevelObject *oa = &custom_draw_sort_level->objects[a];
    const OnLevelObject *ob = &custom_draw_sort_level->objects[b];
    if (oa->layer != ob->layer) return oa->layer < ob->layer ? -1 : 1;
    if (oa->layer2 != ob->layer2) return oa->layer2 < ob->layer2 ? -1 : 1;
    if (oa->z_order != ob->z_order) return oa->z_order < ob->z_order ? -1 : 1;
    return a - b;
}
static void custom_draw_player(void) {
    if (!custom_level_active || custom_player_index < 0 ||
        custom_player_index >= custom_object_count) return;
    const OnLevelObject *player = &custom_level.objects[custom_player_index];
    if (!player->visible || custom_object_is_invisible(player) || player->alpha <= 0)
        return;
    float pulse = player->pulse ? 1.0f + .07f *
        sinf(fmaxf(0.0f, custom_elapsed_time) * 6.28318530718f * 1.6f) : 1.0f;
    float shake = player->shake ?
        sinf(custom_elapsed_time * 37.0f + (player->id % 4093) * .13f) * 2.5f : 0.0f;
    int w = (int)lrintf(custom_player_w * pulse);
    int h = (int)lrintf(custom_player_h * pulse);
    int px = (int)lrintf(custom_player_x - custom_camera_x +
                         (custom_player_w - w) * .5f + shake);
    int py = (int)lrintf(custom_player_y - custom_camera_y +
                         (custom_player_h - h) * .5f);
    int player_art = !custom_jetpack_mode ? PV_ART_BREAD :
        custom_jetpack_active ? PV_ART_JETPACK_ACTIVE : PV_ART_JETPACK_INACTIVE;
    int previous_alpha = sprite_alpha_multiplier;
    int object_alpha = player->alpha > 100 ? 100 : player->alpha;
    sprite_alpha_multiplier = (object_alpha * 255 + 50) / 100;
    if (fabsf(player->angle) >= .01f)
        sprite_draw_rotated_flipped(player_art, px, py, w, h, player->angle,
            custom_player_facing_left, player->flip_y);
    else
        sprite_draw_flipped(player_art, px, py, w, h,
            custom_player_facing_left, player->flip_y);
    sprite_alpha_multiplier = previous_alpha;
}
static void custom_platformer_draw(void) {
    custom_camera_x = custom_player_x + custom_player_w * .5f - GAME_W * .40f;
    custom_camera_y = custom_player_y + custom_player_h * .5f - 411.0f;
    uint32_t rendered_background = preferences_neutral_background_enabled() ?
                                  CUSTOM_BACKGROUND_DEFAULT : custom_background_color;
    rect(0, 0, GAME_W - 1, GAME_H - 1,
         COL((rendered_background >> 16) & 255u,
             (rendered_background >> 8) & 255u,
             rendered_background & 255u));
    /* Render every authored object in the same (L, L2, Z, ID) order as the
     * browser preview. Particles and the moving player participate too. */
    int particle_budget = 4096;
    int render_count = custom_object_count;
    if (render_count > ON_LEVEL_OBJECT_CAP) render_count = ON_LEVEL_OBJECT_CAP;
    for (int i = 0; i < render_count; ++i) custom_draw_order[i] = i;
    custom_draw_sort_level = &custom_level;
    qsort(custom_draw_order, (size_t)render_count, sizeof custom_draw_order[0],
          custom_draw_order_compare);
    for (int order = 0; order < render_count; ++order) {
        int i = custom_draw_order[order];
        OnLevelObject *object = &custom_level.objects[i];
        if (!object->visible || custom_object_is_invisible(object) ||
            object->type == ON_LEVEL_TRIGGER) continue;
        if (object->type == ON_LEVEL_PLAYER) custom_draw_player();
        else {
            custom_draw_object(object);
            custom_draw_particle_object(object, &particle_budget);
        }
    }
    rect(0, 0, GAME_W - 1, 102, COL(220, 232, 239));
    rect(0, 100, GAME_W - 1, 102, COL(36, 59, 82));
    draw_text(24, 16, 3, COL(36, 59, 82), custom_level.title);
    GameCustomHudSnapshot hud;game_custom_hud_snapshot(&hud);
    char label_text[64];
    snprintf(label_text, sizeof label_text, "Прогресс %d%%", hud.progress_percent);
    draw_text(24, 60, 1, COL(83, 106, 128), label_text);
    snprintf(label_text, sizeof label_text, "Попытка %d", hud.attempts);
    draw_text(226, 60, 1, COL(83, 106, 128), label_text);
    int elapsed = (int)hud.elapsed_seconds;
    snprintf(label_text, sizeof label_text, "Время %d:%02d", elapsed / 60, elapsed % 60);
    draw_text(388, 60, 1, COL(83, 106, 128), label_text);
    snprintf(label_text, sizeof label_text, "Монеты %d/%d",
             hud.coins, hud.total_coins);
    draw_text(570, 60, 1, COL(83, 106, 128), label_text);
    rect(24, 87, 284, 93, COL(183, 197, 207));
    if (hud.progress_percent > 0)
        rect(24, 87, 24 + 260 * hud.progress_percent / 100, 93, COL(247, 200, 91));
    if (custom_level_won) {
        rect_blend(0, 0, GAME_W - 1, GAME_H - 1, COL(32, 53, 75), 170);
        rect(358, 264, 922, 447, COL(36, 59, 82));
        rect(365, 271, 915, 440, COL(247, 243, 232));
        draw_text_c(640, 304, 5, COL(36, 59, 82), "УРОВЕНЬ ПРОЙДЕН!");
    }
}

static void draw_workshop_fallback(void) {
    rect(0, 0, GAME_W - 1, GAME_H - 1, COL(25, 43, 64));
    for (int x = 45; x < GAME_W; x += 48)
        rect(x, 0, x + 1, GAME_H - 1, COL(37, 57, 78));
    for (int y = 40; y < GAME_H; y += 48)
        rect(0, y, GAME_W - 1, y + 1, COL(37, 57, 78));
    rect(42, 32, 1238, 688, COL(115, 66, 43));
    rect(49, 39, 1231, 681, COL(153, 91, 59));
    rect(49, 39, 1231, 108, COL(115, 66, 43));
    draw_text(78, 59, 4, COL(255, 247, 224),
              phase == PH_WORKSHOP ? "МОИ УРОВНИ" :
              phase == PH_WORKSHOP_DETAILS ? "НОВЫЙ УРОВЕНЬ" : "РЕДАКТОР УРОВНЯ");
    draw_button_black(1050, 50, 1210, 98, "НАЗАД", 2);
    if (phase == PH_WORKSHOP) {
        rect(110, 160, 1170, 470, COL(111, 63, 43));
        draw_text_c(640, 235, 4, COL(255, 247, 224),
                    "ЛОКАЛЬНЫХ ЧЕРНОВИКОВ ПОКА НЕТ");
        draw_button_white(118, 548, 520, 638, "СОЗДАТЬ УРОВЕНЬ", 3);
        draw_button_white(790, 548, 1202, 638, "ОПУБЛИКОВАННЫЕ", 3);
    } else if (phase == PH_WORKSHOP_DETAILS) {
        rect(130, 145, 1150, 510, COL(111, 63, 43));
        draw_text(180, 205, 4, COL(255, 247, 224), "БЕЗ НАЗВАНИЯ");
        draw_text(180, 275, 2, COL(255, 247, 224),
                  "ЛОКАЛЬНЫЙ ЧЕРНОВИК ПЛАТФОРМЕННОГО УРОВНЯ");
        draw_button_white(720, 564, 1190, 650, "ОТКРЫТЬ РЕДАКТОР", 3);
    } else {
        rect(52, 126, 908, 565, COL(24, 40, 61));
        for (int x = 100; x < 908; x += 50)
            rect(x, 126, x + 1, 565, COL(63, 78, 96));
        for (int y = 176; y < 565; y += 50)
            rect(52, y, 908, y + 1, COL(63, 78, 96));
        rect(52, 515, 908, 565, COL(89, 126, 65));
        rect(930, 126, 1218, 565, COL(111, 63, 43));
        draw_text(966, 158, 2, COL(255, 247, 224), "ОБЪЕКТЫ И ТРИГГЕРЫ");
        draw_button_white(955, 218, 1192, 274, "ДВИЖЕНИЕ", 2);
        draw_button_white(955, 292, 1192, 348, "ГРУППА / СЛОЙ", 2);
        draw_button_white(955, 366, 1192, 422, "ЦВЕТ", 2);
        draw_button_white(74, 600, 290, 655, "СТРОИТЬ", 2);
        draw_button_white(318, 600, 534, 655, "ИЗМЕНИТЬ", 2);
        draw_button_white(562, 600, 778, 655, "УДАЛИТЬ", 2);
        draw_button_white(886, 600, 1015, 655, "СОХРАНИТЬ", 2);
        draw_button_white(1030, 600, 1205, 655, "ПРОСМОТР", 2);
    }
}

static void render(void) {
    if (phase == PH_WORKSHOP || phase == PH_WORKSHOP_DETAILS ||
        phase == PH_WORKSHOP_EDIT) {draw_workshop_fallback();return;}
    if (phase == PH_CUSTOM_LEVELS) {rect(0, 0, GAME_W - 1, GAME_H - 1, COL(245, 242, 232));return;}
    if (phase == PH_CUSTOM_PLAY) {custom_platformer_draw();return;}
    if (phase == PH_ONLINE_ROOMS) {draw_online_rooms();return;}
    if (phase == PH_ONLINE_LOBBY) {draw_online_lobby();return;}
    if (phase == PH_ONLINE_MATCH) {draw_online_match();return;}
    if (phase == PH_MENU) { draw_menu(); return; }
    if (phase == PH_SELECT) { draw_level_select(); return; }
    if (phase == PH_INTRO) { draw_intro(); return; }
    if (phase == PH_GARDEN) { draw_garden(); return; }
    if (phase == PH_BOOK) { draw_book(); return; }
    draw_play_scene();
    if (phase != PH_PLAY && !use_lvgl_ui) draw_result();
}

static void update_online(float dt) {
    on_net_view(&online_view); /* locked snapshot; renderer never reads worker memory */
    if (phase == PH_ONLINE_ROOMS) {
        if (online_view.mode == ON_NET_LOBBY) {
            online_has_match = 0;online_selected = -1;
            phase = PH_ONLINE_LOBBY;
        }
        return;
    }
    if (online_view.mode != ON_NET_LOBBY || !online_view.slot) {
        phase = PH_ONLINE_ROOMS;
        online_has_match = 0;online_selected = -1;
        online_code[0] = 0;online_page = online_search = 0;
        return;
    }
    int mine = online_role(), other = online_view.slot == ON_SLOT_HOST ?
                                 online_view.guest_role : online_view.host_role;
    if (phase == PH_ONLINE_LOBBY) {
        if (!mine || !other || mine == other || !online_view.guest_id[0]) return;
        if (online_view.slot == ON_SLOT_HOST) {
            on_match_new(&online_match, online_view.map);
            on_net_publish(&online_match);
        } else {
            if (!online_view.has_state ||
                !on_match_valid(&online_view.state)) return;
            online_match = online_view.state;
        }
        online_has_match = 1;online_selected = -1;
        phase = PH_ONLINE_MATCH;
        return;
    }
    if (phase != PH_ONLINE_MATCH || !online_has_match) return;
    if (online_view.slot == ON_SLOT_GUEST) {
        if (online_view.has_state && on_match_valid(&online_view.state) &&
            (online_view.state.time >= online_match.time || online_view.state.winner))
            online_match = online_view.state;
    } else {
        int changed = 0;
        if (online_view.has_command &&
            online_view.command.seq > online_match.ack_guest &&
            online_view.command.seq < 2000000000) {
            /* Even rejected moves are acknowledged; otherwise the guest's
             * single-command mailbox could become permanently blocked. */
            (void)on_match_apply(&online_match, online_view.guest_role,
                                 &online_view.command);
            online_match.ack_guest = online_view.command.seq;
            changed = 1;
        }
        if (!online_match.winner && online_view.guest_id[0] &&
            mine && other && mine != other) {
            on_match_step(&online_match, dt);
            changed = 1;
        }
        if (changed) on_net_publish(&online_match);
    }
}

void game_tick(float dt, uint32_t *fb) {
    if (dt < 0) dt = 0;
    int in_online = phase == PH_ONLINE_ROOMS || phase == PH_ONLINE_LOBBY ||
                    phase == PH_ONLINE_MATCH;
    if (in_online) {
        update_online(dt > 0.05f ? 0.05f : dt);
        if (online_hint_time > 0) online_hint_time -= dt;
    } else if (phase == PH_CUSTOM_LEVELS) {
        if (on_net_take_loaded_level(&custom_level)) {
            custom_play_return = PH_CUSTOM_LEVELS;
            if (custom_platformer_start(&custom_level)) phase = PH_CUSTOM_PLAY;
        }
    } else if (phase == PH_CUSTOM_PLAY) {
        custom_platformer_update(dt > 0.05f ? 0.05f : dt);
    }
    if (phase == PH_PLAY) update_play(dt);
    else {
        /* The online match must not change even the elapsed time of a
         * separately saved, paused offline battle. */
        if (!in_online && !(phase == PH_BOOK && book_return == PH_ONLINE_MATCH))
            global_t += dt;
        if (phase == PH_INTRO) {
            intro_t += dt;
            const float length[3] = { 3.5f, 4.2f, 4.2f };
            if (intro_t >= length[intro_step]) advance_intro();
        }
    }
    if (fb) {
        FB = fb;
        render();
    }
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
        if (cells[i] > PT_COUNT + GARDEN_DUCK_COUNT)
            return 0; /* reject malformed saves atomically */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            garden[r][c] = (int)cells[r * COLS + c] - 1;
    return 1;
}

int game_garden_map(void) { return garden_map; }

void game_garden_set_map(int map) {
    if (map == 1 || map == WATER_LEVEL) garden_map = map;
}

/* The garden remains in pvg3-garden.v1. Campaign V1-V4 had the exact same
 * binary layout; V5 added water-pad flags and V6 keeps the V5 length/layout
 * while allowing the two new duck IDs. Old in-progress battles still load.
 * No pointers or random bytes persist; integers/floats are 32-bit on the
 * supported little-endian Android ABIs. */
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
               "Keep the campaign V1-V4 save layout readable on both Android ABIs");
typedef struct {
    SaveState base;                  /* the unchanged V1-V4 prefix */
    uint8_t lily[GAME_GARDEN_CELLS]; /* row-major, 0 or 1 */
    uint8_t reserved[3];            /* zero-filled, keep float aligned */
    float lily_cooldown;
    uint32_t checksum;              /* covers the prefix AND water state */
} SaveStateV5;
_Static_assert(sizeof(SaveStateV5) == 10044 &&
               offsetof(SaveStateV5, checksum) == sizeof(SaveStateV5) - 4,
               "Keep the campaign V5 save layout readable on both Android ABIs");

#define SAVE_MAGIC 0x33477650u /* little-endian bytes 'P', 'v', 'G', '3' */
#define SAVE_VERSION 6u

/* Campaigns saved before the vertical-card UI used the lawn at (120,150)
 * with 120x108 cells. Preserve each entity's position within its cell when
 * loading: otherwise ducks, projectiles, coins and mowers would teleport. */
static float saved_world_x(float x) {
    return LAWN_X + (x - 120.0f) * (float)CELL_W / 120.0f;
}
static float saved_world_y(float y) {
    return LAWN_Y + (y - 150.0f) * (float)CELL_H / 108.0f;
}

static uint32_t save_hash_bytes(const void *bytes, size_t length) {
    const uint8_t *p = (const uint8_t *)bytes;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < length; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t save_checksum(const SaveState *s) {
    return save_hash_bytes(s, offsetof(SaveState, checksum));
}

static uint32_t save_checksum_v5(const SaveStateV5 *s) {
    return save_hash_bytes(s, offsetof(SaveStateV5, checksum));
}

size_t game_save_size(void) { return sizeof(SaveStateV5); }

int game_save_export(void *dst, size_t capacity) {
    if (!dst || capacity < sizeof(SaveStateV5)) return 0;
    SaveStateV5 out;
    memset(&out, 0, sizeof(out));
    SaveState *s = &out.base;
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->completed = (int)completed_mask;
    s->resume = resume_level;
    s->active = saved_battle;
    s->level = level;
    if (s->active) {
        s->jumper_cooldown = cooldown[PT_JUMPER];
        out.lily_cooldown = cooldown[PT_LILY];
        s->boss_phase = boss_phase;
        s->boss_spawned = boss_spawned;
        s->coin_balance = coin_balance;
        s->selected = selected;
        s->to_spawn = to_spawn;
        s->total_zombies = total_zombies;
        s->spawn_t = spawn_t;
        s->global_t = global_t;
        s->banner_t = banner_t;
        s->rng = RNG;
        memcpy(s->cooldown, cooldown, sizeof(s->cooldown));
        memcpy(s->grid, grid, sizeof(grid));
        memcpy(s->zomb, zomb, sizeof(zomb));
        memcpy(s->peas, peas, sizeof(peas));
        memcpy(s->coins, coins, sizeof(coins));
        memcpy(s->mower, mower, sizeof(mower));
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                out.lily[r * COLS + c] = lily[r][c];
    }
    s->checksum = save_checksum(s);
    out.checksum = save_checksum_v5(&out);
    memcpy(dst, &out, sizeof(out));
    return 1;
}

int game_save_import(const void *src, size_t length) {
    if (!src) return 0;
    SaveState s;
    uint8_t imported_lily[ROWS][COLS] = {{0}};
    float imported_lily_cooldown = 0;
    if (length == sizeof(SaveStateV5)) {
        SaveStateV5 v5;
        memcpy(&v5, src, sizeof(v5)); /* src need not be aligned */
        if ((v5.base.version != 5u && v5.base.version != SAVE_VERSION) ||
            v5.checksum != save_checksum_v5(&v5) ||
            v5.base.checksum != save_checksum(&v5.base) ||
            v5.reserved[0] || v5.reserved[1] || v5.reserved[2] ||
            !isfinite(v5.lily_cooldown) ||
            v5.lily_cooldown > PDEF[PT_LILY].recharge + 1)
            return 0;
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                int value = v5.lily[r * COLS + c];
                if (value > 1 ||
                    (value && (v5.base.active != 1 || v5.base.level != WATER_LEVEL ||
                               r < WATER_FIRST_ROW || r > WATER_LAST_ROW ||
                               v5.base.grid[r][c].type == PT_LILY))) return 0;
                imported_lily[r][c] = (uint8_t)value;
            }
        memcpy(&s, &v5.base, sizeof(s));
        imported_lily_cooldown = v5.lily_cooldown;
    } else if (length == sizeof(SaveState)) {
        memcpy(&s, src, sizeof(s)); /* legacy V1-V4 file */
        if (s.version >= 5u) return 0;
    } else return 0;
    if (s.magic != SAVE_MAGIC ||
        (s.version != 1u && s.version != 2u && s.version != 3u &&
         s.version != 4u && s.version != 5u && s.version != SAVE_VERSION) ||
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
            (s.version <= 2u &&
             (!isfinite(s.old_level_left) || s.old_level_left < 0 ||
              s.old_level_left > 45 + s.level * 5 ||
              !isfinite(s.jumper_cooldown) || s.jumper_cooldown < 0 ||
              s.jumper_cooldown > 100)) ||
            (s.version >= 3u &&
             (!isfinite(s.jumper_cooldown) ||
              s.jumper_cooldown > PDEF[PT_JUMPER].recharge + 1)) ||
            s.coin_balance < 0 || s.coin_balance > 1000000 ||
            s.selected < -1 ||
            s.selected >= (s.version < 5u ? PT_LILY : PT_COUNT) ||
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
                if (p->type < PT_NONE || p->type >= PT_LILY ||
                    (p->type != PT_NONE && (!isfinite(p->hp) ||
                      !isfinite(p->fire_t) || !isfinite(p->sway))) ||
                    (s.version >= 5u && s.level == WATER_LEVEL &&
                     r >= WATER_FIRST_ROW && r <= WATER_LAST_ROW &&
                     p->type != PT_NONE && !imported_lily[r][c])) return 0;
            }
        }
        for (int i = 0; i < ZMAX; i++) {
            const Zombie *z = &s.zomb[i];
            if (z->active != 0 && z->active != 1) return 0;
            if (z->active && (z->row < 0 || z->row >= ROWS ||
                z->type < EN_DUCK ||
                z->type > (s.version >= SAVE_VERSION ? EN_BUCKET : EN_ROBOT) ||
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
        if (s.version < 4u) {
            for (int r = 0; r < ROWS; r++) {
                /* An unused mower follows the new lawn edge; a sweeping
                 * mower must keep its in-progress position. */
                s.mower[r].x = !s.mower[r].used && !s.mower[r].running ?
                    LAWN_X + 30 : saved_world_x(s.mower[r].x);
                if (!isfinite(s.mower[r].x)) return 0;
            }
            for (int i = 0; i < ZMAX; i++) if (s.zomb[i].active) {
                s.zomb[i].x = saved_world_x(s.zomb[i].x);
                if (!isfinite(s.zomb[i].x)) return 0;
            }
            for (int i = 0; i < PEAMAX; i++) if (s.peas[i].active) {
                s.peas[i].x = saved_world_x(s.peas[i].x);
                s.peas[i].y = saved_world_y(s.peas[i].y);
                if (!isfinite(s.peas[i].x) || !isfinite(s.peas[i].y)) return 0;
            }
            for (int i = 0; i < COINMAX; i++) if (s.coins[i].active) {
                s.coins[i].x = saved_world_x(s.coins[i].x);
                s.coins[i].y = saved_world_y(s.coins[i].y);
                s.coins[i].target_y = saved_world_y(s.coins[i].target_y);
                if (!isfinite(s.coins[i].x) || !isfinite(s.coins[i].y) ||
                    !isfinite(s.coins[i].target_y)) return 0;
            }
        }
        if (s.version < 5u && s.level == WATER_LEVEL) {
            /* Level 5 used to be land. Do not drown plants on a saved V1-V4
             * board: give each occupied water cell a free supporting pad. */
            for (int r = WATER_FIRST_ROW; r <= WATER_LAST_ROW; r++)
                for (int c = 0; c < COLS; c++)
                    if (s.grid[r][c].type != PT_NONE) imported_lily[r][c] = 1;
        }
    }
    completed_mask = s.version == 1u ? (1u << s.completed) - 1u : (unsigned)s.completed;
    resume_level = s.resume;
    saved_battle = s.active;
    level = s.active ? s.level : s.resume;
    memset(lily, 0, sizeof(lily));
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
        cooldown[PT_JUMPER] = s.version >= 3u ? s.jumper_cooldown : 0;
        cooldown[PT_LILY] = imported_lily_cooldown;
        memcpy(lily, imported_lily, sizeof(lily));
        memcpy(grid, s.grid, sizeof(grid));
        memcpy(zomb, s.zomb, sizeof(zomb));
        memcpy(peas, s.peas, sizeof(peas));
        memcpy(coins, s.coins, sizeof(coins));
        memcpy(mower, s.mower, sizeof(mower));
        memset(parts, 0, sizeof(parts)); /* cosmetic particles do not persist */
        banner_text = boss_phase ? "КОРОЛЕВА В РОБОТЕ!" :
                      level == WATER_LEVEL ? "В ВОДЕ СНАЧАЛА ПОСАДИ КУВШИНКУ!" :
                      LEVEL_NAMES[level - 1];
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
void game_debug_spawn_armored_duck(int row, float x, int type) {
    if (row < 0 || row >= ROWS || (type != EN_CONE && type != EN_BUCKET)) return;
    for (int i = 0; i < ZMAX; i++)
        if (!zomb[i].active) {
            Zombie *z = &zomb[i];
            memset(z, 0, sizeof(*z));
            z->active = 1; z->type = type; z->row = row; z->x = x;
            z->hp = z->maxhp = EN_BASE_HP[type]; z->speed = 25;
            if (to_spawn > 0) to_spawn--;
            return;
        }
}
float game_debug_enemy_hp(int type) {
    for (int i = 0; i < ZMAX; i++)
        if (zomb[i].active && zomb[i].type == type) return zomb[i].hp;
    return -1;
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
float game_debug_first_pea_x(void) {
    for (int i = 0; i < PEAMAX; i++) if (peas[i].active) return peas[i].x;
    return -1;
}
float game_debug_first_pea_y(void) {
    for (int i = 0; i < PEAMAX; i++) if (peas[i].active) return peas[i].y;
    return -1;
}
float game_debug_first_coin_x(void) {
    for (int i = 0; i < COINMAX; i++) if (coins[i].active) return coins[i].x;
    return -1;
}
float game_debug_first_coin_y(void) {
    for (int i = 0; i < COINMAX; i++) if (coins[i].active) return coins[i].y;
    return -1;
}
float game_debug_first_coin_target_y(void) {
    for (int i = 0; i < COINMAX; i++) if (coins[i].active) return coins[i].target_y;
    return -1;
}
float game_debug_mower_x(int row) {
    return (unsigned)row < ROWS ? mower[row].x : -1;
}
int game_debug_plant_type(int row, int col) {
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS) return PT_NONE;
    return grid[row][col].type;
}
int game_debug_lily_at(int row, int col) {
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS) return 0;
    return lily[row][col];
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
float game_debug_custom_player_x(void) {return custom_player_x;}
float game_debug_custom_player_y(void) {return custom_player_y;}
float game_debug_custom_player_vx(void) {return custom_player_vx;}
float game_debug_custom_player_vy(void) {return custom_player_vy;}
int game_debug_custom_player_grounded(void) {return custom_player_grounded;}
int game_debug_custom_player_facing_left(void) {return custom_player_facing_left;}
int game_debug_custom_jump_count(void) {return custom_jump_count;}
int game_debug_custom_dash_active(void) {return custom_dash_remaining > 0.0f;}
int game_debug_custom_trigger_count(int id) {
    int index = custom_id_lookup(id);
    return index >= 0 ? custom_trigger_counts[index] : 0;
}
int game_debug_custom_trigger_inside(int id) {
    int index = custom_id_lookup(id);
    return index >= 0 ? custom_trigger_touch_inside[index] : 0;
}
float game_debug_custom_gravity(void) {return custom_gravity;}
uint32_t game_debug_custom_background_color(void) {return custom_background_color;}
int game_debug_custom_checkpoint_id(void) {return custom_checkpoint_id;}
int game_debug_custom_jetpack_mode(void) {return custom_jetpack_mode;}
int game_debug_custom_jetpack_active(void) {return custom_jetpack_active;}
int game_debug_custom_object(int id, OnLevelObject *out) {
    if (!out) return 0;
    OnLevelObject *object = custom_find_id(id);
    if (!object) return 0;
    *out = *object;return 1;
}
int game_debug_custom_object_invisible(int id) {
    OnLevelObject *object = custom_find_id(id);
    return object ? custom_object_is_invisible(object) : 0;
}
int game_debug_garden_plant_type(int row, int col) {
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS) return PT_NONE;
    return garden[row][col];
}
int game_debug_book_plant(void) { return book_enemy_tab ? -1 : book_selected; }
int game_debug_book_enemy(void) {
    return book_enemy_tab ? BOOK_ENEMIES[book_enemy_selected] : -1;
}
int game_debug_level_completed(int n) {
    return n >= 1 && n <= MAX_LEVEL && !!(completed_mask & (1u << (n - 1)));
}
#endif
