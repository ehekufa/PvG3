/* game.c — "PvG3: Портал" — a first-person Portal-style 3D puzzle game,
 * written in pure C with a software perspective renderer (z-buffer + flat
 * Gouraud-style shading from face normals). No engine, no external assets:
 * the whole test chamber is built from boxes / discs and lit procedurally.
 *
 * The platform layer (Android / desktop tool) only blits the GAME_W*GAME_H
 * RGBA framebuffer; all 3D math and rasterisation happen here.
 *
 * Mechanics (Portal-flavoured):
 *   - shoot two portals (blue / orange) onto the chamber walls;
 *   - walk into one portal and come out of the other, keeping momentum;
 *   - pick up a weighted storage cube and drop it on a floor button;
 *   - a held button keeps the door open; reach the glowing exit to win.
 * Everything is driven by a compact level table below.
 */

#include "game.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* framebuffer helpers (kept from the 2D version for the UI overlay)   */
/* ------------------------------------------------------------------ */

static uint32_t *FB;

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

static void disc(int cx, int cy, int r, uint32_t c) {
    if (r <= 0) { setpix(cx, cy, c); return; }
    int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    int r2 = r * r;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if ((unsigned)x >= GAME_W || (unsigned)y >= GAME_H) continue;
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 <= r2) setpix(x, y, c);
        }
}

/* ------------------------------------------------------------------ */
/* tiny 5x7 bitmap font (ASCII + Cyrillic) — same as the 2D version    */
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

static const uint8_t FNT_CYR[33][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},
    {0x0E,0x0A,0x0A,0x0A,0x1F,0x15,0x11}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x15,0x15,0x0E,0x1F,0x0E,0x15,0x15}, {0x1E,0x11,0x01,0x0E,0x01,0x11,0x1E},
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, {0x0E,0x00,0x11,0x19,0x15,0x13,0x11},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x11},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x04,0x0E,0x15,0x15,0x15,0x0E,0x04}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x1F,0x01}, {0x11,0x11,0x11,0x1F,0x01,0x01,0x01},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x1F}, {0x11,0x11,0x11,0x11,0x11,0x1F,0x01},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, {0x12,0x12,0x12,0x1E,0x12,0x12,0x1E},
    {0x10,0x10,0x10,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x01,0x07,0x01,0x11,0x0E},
    {0x16,0x19,0x19,0x19,0x19,0x19,0x16}, {0x0F,0x11,0x11,0x0F,0x01,0x01,0x01},
    {0x0A,0x00,0x1F,0x10,0x1E,0x10,0x1F},
};

static const uint8_t *glyph_for(uint32_t cp) {
    if (cp == 0x451) cp = 0x401;
    if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
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

static void draw_text(int x, int y, int s, uint32_t c, const char *str) {
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xC0 && *p) {
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
        if ((*p & 0xC0) != 0x80) n++;
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

/* ================================================================== */
/* 3D core: vectors, projection, software rasteriser                  */
/* ================================================================== */

typedef struct { float x, y, z; } V3;
static inline V3 v3(float x, float y, float z) { V3 a = { x, y, z }; return a; }

#define GAME_FOV (75.0f * 3.14159265f / 180.0f)
#define GAME_NEAR 0.08f
#define GAME_FAR  200.0f

static float FY;                 /* vertical fov scale          */
static float FX;                 /* horizontal fov scale        */

/* Camera basis (recomputed each frame) */
static V3 CAM_R, CAM_U, CAM_F;
static V3 CAM_P;                 /* eye position */
static float CAM_YAW, CAM_PITCH;

/* Depth buffer stores inverse-view-depth (larger = closer). */
static float ZBUF[GAME_W * GAME_H];

/* Light: fixed directional, roughly over-the-shoulder. */
static const float LDX = 0.42f, LDY = 0.80f, LDZ = 0.40f; /* unit-ish */
static const float AMBIENT = 0.44f;

static void cam_basis(void) {
    float cp = cosf(CAM_PITCH), sp = sinf(CAM_PITCH);
    float cy = cosf(CAM_YAW),   sy = sinf(CAM_YAW);
    CAM_F = v3(sy * cp, sp, -cy * cp);
    CAM_R = v3(cy, 0.0f, sy);
    CAM_U = v3(CAM_R.y * CAM_F.z - CAM_R.z * CAM_F.y,
               CAM_R.z * CAM_F.x - CAM_R.x * CAM_F.z,
               CAM_R.x * CAM_F.y - CAM_R.y * CAM_F.x);
}

/* transform a world point to view space (cx,cy = screen-ish, cz = depth) */
static inline void view_pt(V3 p, float *cx, float *cy, float *cz) {
    float dx = p.x - CAM_P.x, dy = p.y - CAM_P.y, dz = p.z - CAM_P.z;
    *cx = CAM_R.x * dx + CAM_R.y * dy + CAM_R.z * dz;
    *cy = CAM_U.x * dx + CAM_U.y * dy + CAM_U.z * dz;
    *cz = CAM_F.x * dx + CAM_F.y * dy + CAM_F.z * dz;
}

static inline void proj(float cx, float cy, float cz, int *sx, int *sy) {
    float inv = 1.0f / cz;
    float ndx = FX * cx * inv;
    float ndy = FY * cy * inv;
    *sx = (int)((ndx * 0.5f + 0.5f) * (float)GAME_W);
    *sy = (int)((0.5f - ndy * 0.5f) * (float)GAME_H);
}

/* shade a flat colour by the face normal (and optional emissive flag) */
static uint32_t shade(uint32_t c, V3 n, int emissive) {
    if (emissive) return c;
    float d = n.x * LDX + n.y * LDY + n.z * LDZ;
    if (d < 0) d = 0;
    if (d > 1) d = 1;
    float f = AMBIENT + (1.0f - AMBIENT) * d;
    int r = (int)(((c & 255) * f));
    int g = (int)((((c >> 8) & 255) * f));
    int b = (int)((((c >> 16) & 255) * f));
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* clip a triangle (in view space) against z>=NEAR, output up to 4 verts */
typedef struct { float x, y, z; } V4;
static int clip_near(const V4 in[3], V4 out[4]) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const V4 *a = &in[i];
        const V4 *b = &in[(i + 1) % 3];
        int ain = a->z >= GAME_NEAR;
        int bin = b->z >= GAME_NEAR;
        if (ain) out[n++] = *a;
        if (ain != bin) {
            float t = (GAME_NEAR - a->z) / (b->z - a->z);
            V4 m; m.x = a->x + (b->x - a->x) * t;
                  m.y = a->y + (b->y - a->y) * t;
                  m.z = GAME_NEAR;
            out[n++] = m;
        }
        if (n > 4) break;
    }
    return n;
}

/* rasterise one screen triangle with per-pixel z-buffer (flat colour). */
static void fill_tri(int x0, int y0, float iz0, int x1, int y1, float iz1,
                     int x2, int y2, float iz2, uint32_t col) {
    int minx = x0, maxx = x0;
    int miny = y0, maxy = y0;
    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= GAME_W) maxx = GAME_W - 1;
    if (maxy >= GAME_H) maxy = GAME_H - 1;
    if (minx > maxx || miny > maxy) return;

    int area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area < 0) area = -area;
    if (area == 0) return;

    for (int py = miny; py <= maxy; py++) {
        uint32_t *fbrow = FB + py * GAME_W;
        float *zrow = ZBUF + py * GAME_W;
        for (int px = minx; px <= maxx; px++) {
            int w0 = (x1 - px) * (y2 - py) - (x2 - px) * (y1 - py);
            int w1 = (x2 - px) * (y0 - py) - (x0 - px) * (y2 - py);
            int w2 = (x0 - px) * (y1 - py) - (x1 - px) * (y0 - py);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                float iz = (float)(w0 * iz0 + w1 * iz1 + w2 * iz2) / (float)area;
                int idx = px;
                if (iz > zrow[idx]) { zrow[idx] = iz; fbrow[idx] = col; }
            }
        }
    }
}

/* full pipeline: world triangle -> view -> clip -> project -> fill */
static void tri(V3 a, V3 b, V3 c, uint32_t col) {
    V4 v[4];
    V4 in[3];
    view_pt(a, &in[0].x, &in[0].y, &in[0].z); in[0].z = in[0].z; /* depth */
    view_pt(b, &in[1].x, &in[1].y, &in[1].z);
    view_pt(c, &in[2].x, &in[2].y, &in[2].z);

    /* if all behind the near plane, skip */
    if (in[0].z < GAME_NEAR && in[1].z < GAME_NEAR && in[2].z < GAME_NEAR) return;
    int n = clip_near(in, v);
    if (n < 3) return;

    int sx[5], sy[5];
    float iz[5];
    for (int i = 0; i < n; i++) {
        proj(v[i].x, v[i].y, v[i].z, &sx[i], &sy[i]);
        iz[i] = 1.0f / v[i].z;
        if (sx[i] < -1000 || sx[i] > GAME_W + 1000 || sy[i] < -1000 || sy[i] > GAME_H + 1000)
            iz[i] = 0.0f; /* clamp wildly off-screen verts to avoid huge spans */
    }

    /* fan-triangulate the clipped polygon */
    for (int i = 1; i + 1 < n; i++)
        fill_tri(sx[0], sy[0], iz[0], sx[i], sy[i], iz[i], sx[i + 1], sy[i + 1], iz[i + 1], col);
}

/* quad with an explicit normal (for lighting) and colour */
static void quad(V3 p0, V3 p1, V3 p2, V3 p3, V3 n, uint32_t col, int emissive) {
    uint32_t cc = shade(col, n, emissive);
    tri(p0, p1, p2, cc);
    tri(p0, p2, p3, cc);
}

/* axis-aligned box from min/max corners, colour per face */
static void box_bounds(V3 mn, V3 mx, uint32_t col) {
    V3 p0 = v3(mn.x, mn.y, mn.z), p1 = v3(mx.x, mn.y, mn.z);
    V3 p2 = v3(mx.x, mx.y, mn.z), p3 = v3(mn.x, mx.y, mn.z);
    V3 p4 = v3(mn.x, mn.y, mx.z), p5 = v3(mx.x, mn.y, mx.z);
    V3 p6 = v3(mx.x, mx.y, mx.z), p7 = v3(mn.x, mx.y, mx.z);
    uint32_t c = col;
    quad(p0, p1, p2, p3, v3(0, 0, -1), c, 0);  /* -Z */
    quad(p4, p5, p6, p7, v3(0, 0, 1),  c, 0);  /* +Z */
    quad(p0, p1, p5, p4, v3(0, -1, 0), c, 0);  /* -Y */
    quad(p3, p2, p6, p7, v3(0, 1, 0),  c, 0);  /* +Y */
    quad(p0, p4, p7, p3, v3(-1, 0, 0), c, 0);  /* -X */
    quad(p1, p2, p6, p5, v3(1, 0, 0),  c, 0);  /* +X */
}

/* box centred at c with size s */
static void box(V3 c, V3 s, uint32_t col) {
    box_bounds(v3(c.x - s.x * 0.5f, c.y - s.y * 0.5f, c.z - s.z * 0.5f),
               v3(c.x + s.x * 0.5f, c.y + s.y * 0.5f, c.z + s.z * 0.5f), col);
}

/* ring / disc on a wall plane (for portals and the exit) */
static void disc3d(V3 c, V3 n, V3 up, float r, uint32_t col, int emissive, int segs) {
    V3 right = v3(up.y * n.z - up.z * n.y,
                  up.z * n.x - up.x * n.z,
                  up.x * n.y - up.y * n.x);
    float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rl < 1e-4f) return;
    right.x /= rl; right.y /= rl; right.z /= rl;
    uint32_t cc = shade(col, n, emissive);
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 2 * 3.14159265f;
        float a1 = (float)(i + 1) / segs * 2 * 3.14159265f;
        V3 q0 = v3(c.x + right.x * cosf(a0) * r + up.x * sinf(a0) * r,
                   c.y + right.y * cosf(a0) * r + up.y * sinf(a0) * r,
                   c.z + right.z * cosf(a0) * r + up.z * sinf(a0) * r);
        V3 q1 = v3(c.x + right.x * cosf(a1) * r + up.x * sinf(a1) * r,
                   c.y + right.y * cosf(a1) * r + up.y * sinf(a1) * r,
                   c.z + right.z * cosf(a1) * r + up.z * sinf(a1) * r);
        tri(c, q0, q1, cc);
    }
}

/* weighted storage cube */
static void cube3d(V3 c, float half, uint32_t col) {
    box(v3(c.x, c.y, c.z), v3(half * 2, half * 2, half * 2), col);
    /* glowing top panel */
    box(v3(c.x, c.y + half * 0.98f, c.z), v3(half * 1.2f, half * 0.18f, half * 1.2f),
        shade(col, v3(0, 1, 0), 1));
    /* little handle accent */
    box(v3(c.x, c.y + half * 1.02f, c.z), v3(half * 0.5f, half * 0.4f, half * 0.5f),
        shade(COL(80, 90, 105), v3(0, 1, 0), 0));
}

/* ================================================================== */
/* Levels                                                             */
/* ================================================================== */

typedef struct { float x, y, z, sx, sy, sz; } Sol;

typedef struct {
    const char *name, *hint;
    float W, D, H;           /* interior chamber size (centred on origin) */
    V3 start; float start_yaw;
    V3 cube; int has_cube;
    V3 button; int has_button;
    float door[6]; int has_door;  /* cx, cy, cz, sx, sy, sz */
    V3 exit; float exit_r;
    const Sol *solids; int nsolids;
} LevelSpec;

#define A(x,y,z,sx,sy,sz) { x, y, z, sx, sy, sz }

static const Sol L0_S[] = {                 /* tutorial: two pillars */
    A(-3.5f, 1.5f, -2.0f, 2.0f, 3.0f, 2.0f),
    A( 3.0f, 1.5f, -4.0f, 2.0f, 3.0f, 2.0f),
    A( 0.0f, 1.5f,  3.5f, 6.0f, 3.0f, 2.0f),
};
static const Sol L1_S[] = {                 /* corridor + ledge */
    A( 0.0f, 1.5f,  0.0f, 2.0f, 3.0f, 9.0f),
    A(-5.0f, 0.6f, -4.0f, 3.0f, 1.2f, 6.0f),
};
static const Sol L2_S[] = {                 /* walls + a ramp of blocks */
    A(-4.0f, 1.5f, -1.0f, 2.0f, 3.0f, 5.0f),
    A( 1.0f, 0.75f, 2.0f, 4.0f, 1.5f, 2.0f),
    A( 2.0f, 0.5f, -4.0f, 2.0f, 1.0f, 2.0f),
    A(-2.0f, 2.5f, -5.0f, 2.0f, 5.0f, 2.0f),
};
static const Sol L3_S[] = {                 /* maze-ish */
    A(-4.0f, 1.5f,  4.0f, 9.0f, 3.0f, 1.5f),
    A(-4.0f, 1.5f, -1.0f, 9.0f, 3.0f, 1.5f),
    A( 3.0f, 1.5f,  2.0f, 2.0f, 3.0f, 6.0f),
    A(-1.0f, 0.75f, 0.0f, 2.0f, 1.5f, 2.0f),
};
static const Sol L4_S[] = {                 /* tall towers */
    A(-5.0f, 2.5f,  0.0f, 2.0f, 5.0f, 2.0f),
    A(-2.0f, 4.0f,  2.0f, 2.0f, 8.0f, 2.0f),
    A( 4.0f, 1.5f, -3.0f, 2.0f, 3.0f, 8.0f),
};
static const Sol L5_S[] = {                 /* final: big pillar room */
    A( 0.0f, 3.0f,  0.0f, 4.0f, 6.0f, 4.0f),
    A(-6.0f, 1.5f, -5.0f, 2.0f, 3.0f, 2.0f),
    A( 6.0f, 2.0f,  5.0f, 2.0f, 4.0f, 2.0f),
};

#define SMAX 64  /* max solids per level (rendering scratch buffer) */

static const LevelSpec LEVELS[] = {
    { "ТЕСТ-КАМЕРА 01", "Найди светящийся выход. Портал: тапни синий/оранжевый, наведись на стену.",
      16, 14, 6, { -6.0f, 1.6f, 6.0f }, 0.8f,
      { 2.5f, 1.0f, 2.5f }, 1, { -2.5f, 0.25f, -3.0f }, 0,
      { 0,0,0,0,0,0 }, 0, { 5.0f, 1.2f, -5.0f }, 1.4f, L0_S, 3 },

    { "ТЕСТ-КАМЕРА 02", "Стена преграждает путь. Перенеси куб на кнопку — дверь откроется.",
      20, 16, 6, { -7.0f, 1.6f, 6.5f }, 0.6f,
      { 5.0f, 1.0f, 5.0f }, 1, { -5.0f, 0.25f, 4.0f }, 1,
      { 8.0f, 2.5f, 0.0f, 0.8f, 5.0f, 8.0f }, 1, { 8.0f, 1.4f, -6.0f }, 1.5f, L1_S, 2 },

    { "ТЕСТ-КАМЕРА 03", "Два портала — твой единственный путь наверх.",
      20, 18, 9, { -8.0f, 1.6f, 8.0f }, 0.9f,
      { 5.0f, 1.0f, 5.0f }, 1, { 0.0f, 0.25f, 8.0f }, 1,
      { -8.0f, 2.0f, 4.0f, 9.0f, 4.0f, 0.8f }, 1, { 8.0f, 1.4f, -7.0f }, 1.5f, L2_S, 4 },

    { "ТЕСТ-КАМЕРА 04", "Лабиринт. Выход за дверью — открывай кнопкой.",
      20, 18, 6, { -8.0f, 1.6f, 7.0f }, 0.5f,
      { 6.0f, 1.0f, -6.0f }, 1, { -7.0f, 0.25f, -7.0f }, 1,
      { 8.0f, 2.5f, 2.0f, 0.8f, 5.0f, 10.0f }, 1, { 8.0f, 1.4f, -7.0f }, 1.5f, L3_S, 4 },

    { "ТЕСТ-КАМЕРА 05", "Высоко. Портал на башню — и вперёд к выходу.",
      20, 18, 12, { -8.0f, 1.6f, 7.0f }, 0.7f,
      { 0.0f, 1.0f, 3.0f }, 1, { 0.0f, 0.25f, 7.0f }, 1,
      { 0,0,0,0,0,0 }, 0, { 8.0f, 1.4f, -7.0f }, 1.5f, L4_S, 3 },

    { "ТЕСТ-КАМЕРА 06", "ФИНАЛ. Огромный зал. Выход за гигантской колонной.",
      24, 20, 10, { -9.0f, 1.6f, 8.5f }, 0.9f,
      { 7.0f, 1.0f, 6.0f }, 1, { -6.0f, 0.25f, -7.0f }, 1,
      { 9.0f, 3.0f, -4.0f, 0.9f, 6.0f, 9.0f }, 1, { 9.0f, 1.4f, -8.0f }, 1.6f, L5_S, 3 },
};
#define LEVEL_N ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

/* ================================================================== */
/* game state                                                         */
/* ================================================================== */

typedef enum { PH_MENU, PH_PLAY, PH_WIN, PH_LOSE } Phase;

static Phase phase;
static int level_idx;
static float global_t;
static float msg_t;
static const char *msg;

typedef struct { V3 c; V3 n; V3 up; float r; uint32_t col; int on; } Portal;
static Portal PA, PB;

/* movable storage cube */
typedef struct { V3 p, v; float half; int held; } Cube;
static Cube cube;

/* door */
static float door_open;          /* 0 closed .. 1 open */
static int button_held;

/* player */
static V3 pvel;
static float grounded;

/* input */
static int js_active, look_active;
static float js_ox, js_oy;        /* joystick origin */
static int last_lx, last_ly;
static float mvx, mvz;            /* desired move in local x/z */
static float cooldown_blue, cooldown_orange;
static int grab_pressed;

/* current level runtime */
static float RW, RD, RH;

static uint32_t RNG = 0xC0FFEE11u;

/* ------------------------------------------------------------------ */
/* level load + helpers                                               */
/* ------------------------------------------------------------------ */

static const LevelSpec *L(void) { return &LEVELS[level_idx]; }

static void load_level(int idx) {
    level_idx = idx;
    const LevelSpec *lv = L();
    RW = lv->W; RD = lv->D; RH = lv->H;

    CAM_P = lv->start;
    CAM_YAW = lv->start_yaw;
    CAM_PITCH = -0.05f;
    pvel = v3(0, 0, 0);
    grounded = 1;

    cube.p = lv->cube; cube.v = v3(0, 0, 0); cube.half = 0.5f; cube.held = 0;

    PA.n = v3(1, 0, 0); PA.up = v3(0, 1, 0); PA.r = 0.9f;
    PA.col = COL(90, 150, 255); PA.on = 1;
    PA.c = v3(lv->W * 0.5f - 0.05f, 2.2f, -lv->D * 0.25f);

    PB.n = v3(-1, 0, 0); PB.up = v3(0, 1, 0); PB.r = 0.9f;
    PB.col = COL(255, 150, 50); PB.on = 1;
    PB.c = v3(-lv->W * 0.5f + 0.05f, 2.2f, lv->D * 0.25f);

    door_open = 0; button_held = 0;
    msg = NULL; msg_t = 0;
    cooldown_blue = cooldown_orange = 0;
    phase = PH_PLAY;
}

/* ------------------------------------------------------------------ */
/* ray vs room interior surfaces, returns nearest hit + normal         */
/* ------------------------------------------------------------------ */

static int raycast_room(V3 o, V3 d, V3 *hp, V3 *hn) {
    /* the six room boundary planes (interior faces), plane: n·p = d */
    typedef struct { V3 n; float d; } Plane;
    Plane planes[6];
    int i = 0;
    float hw = RW * 0.5f, hd = RD * 0.5f, hh = RH;
    planes[i].n = v3(1, 0, 0);  planes[i++].d = hw;
    planes[i].n = v3(-1, 0, 0); planes[i++].d = hw;
    planes[i].n = v3(0, 0, 1);  planes[i++].d = hd;
    planes[i].n = v3(0, 0, -1); planes[i++].d = hd;
    planes[i].n = v3(0, 1, 0);  planes[i++].d = 0;     /* floor */
    planes[i].n = v3(0, -1, 0); planes[i++].d = -hh;   /* ceiling */

    float best = 1e9f; int found = 0; V3 bn = v3(0, 0, 0), bp = v3(0, 0, 0);
    for (int k = 0; k < 6; k++) {
        const V3 *n = &planes[k].n;
        float denom = n->x * d.x + n->y * d.y + n->z * d.z;
        if (denom > -1e-4f && denom < 1e-4f) continue;
        float no = n->x * o.x + n->y * o.y + n->z * o.z;
        float t = (planes[k].d - no) / denom; /* n·(o + t*d) = d_plane */
        if (t <= 0.05f || t >= best) continue;
        V3 hit = v3(o.x + d.x * t, o.y + d.y * t, o.z + d.z * t);
        /* make sure hit is inside the face bounds */
        float ax = fabsf(n->x), az = fabsf(n->z);
        if (ax > 0.5f) { if (hit.z < -hd || hit.z > hd || hit.y < 0 || hit.y > hh) continue; }
        else if (az > 0.5f) { if (hit.x < -hw || hit.x > hw || hit.y < 0 || hit.y > hh) continue; }
        else { if (hit.x < -hw || hit.x > hw || hit.z < -hd || hit.z > hd) continue; }
        best = t; found = 1; bn = *n; bp = hit;
    }
    if (!found) return 0;
    *hp = bp; *hn = bn;
    return 1;
}

static void shoot_portal(Portal *p) {
    V3 hp, hn;
    if (raycast_room(CAM_P, CAM_F, &hp, &hn)) {
        p->c = hp; p->n = hn;
        /* keep the portal upright on walls, flat on floor */
        if (fabsf(hn.y) > 0.5f) p->up = v3(0, 0, hn.y > 0 ? -1 : 1);
        else p->up = v3(0, 1, 0);
        p->on = 1;
    }
}

/* ------------------------------------------------------------------ */
/* physics                                                            */
/* ------------------------------------------------------------------ */

#include <stddef.h>
static int overlaps_box(V3 p, float hb, V3 mn, V3 mx) {
    return p.x + hb > mn.x && p.x - hb < mx.x &&
           p.y + hb > mn.y && p.y - hb < mx.y &&
           p.z + hb > mn.z && p.z - hb < mx.z;
}

static void collide_axis(V3 *p, V3 *v, float hb, V3 mn, V3 mx, int axis) {
    if (!overlaps_box(*p, hb, mn, mx)) return;
    if (axis == 0) { /* x */
        if (v->x > 0) p->x = mn.x - hb; else if (v->x < 0) p->x = mx.x + hb;
        v->x = 0;
    } else if (axis == 1) { /* y */
        if (v->y < 0) { p->y = mx.y + hb; v->y = 0; grounded = 1; }
        else if (v->y > 0) { p->y = mn.y - hb; v->y = 0; }
    } else {
        if (v->z > 0) p->z = mn.z - hb; else if (v->z < 0) p->z = mx.z + hb;
        v->z = 0;
    }
}

static void player_collide(void) {
    float hb = 0.45f; /* player half-*body* for collision (feet-ish) */
    /* room walls */
    float hw = RW * 0.5f, hd = RD * 0.5f;
    if (CAM_P.x > hw - hb) { CAM_P.x = hw - hb; pvel.x = 0; }
    if (CAM_P.x < -hw + hb) { CAM_P.x = -hw + hb; pvel.x = 0; }
    if (CAM_P.z > hd - hb) { CAM_P.z = hd - hb; pvel.z = 0; }
    if (CAM_P.z < -hd + hb) { CAM_P.z = -hd + hb; pvel.z = 0; }

    /* solids */
    const Sol *s = L()->solids;
    for (int i = 0; i < L()->nsolids; i++) {
        V3 mn = v3(s[i].x - s[i].sx * 0.5f, s[i].y - s[i].sy * 0.5f, s[i].z - s[i].sz * 0.5f);
        V3 mx = v3(s[i].x + s[i].sx * 0.5f, s[i].y + s[i].sy * 0.5f, s[i].z + s[i].sz * 0.5f);
        /* only collide if the player's feet are below the box top */
        if (CAM_P.y - 1.6f < mx.y - 0.05f)
            collide_axis(&CAM_P, &pvel, hb, mn, mx, 0);
        V3 p2 = CAM_P; V3 v2 = pvel;
        collide_axis(&p2, &v2, hb, mn, mx, 2);
        CAM_P = p2; pvel = v2;
        V3 p3 = CAM_P; V3 v3v = pvel;
        collide_axis(&p3, &v3v, hb, mn, mx, 1);
        CAM_P = p3; pvel = v3v;
    }

    /* door (closed = solid) */
    if (L()->has_door && door_open < 0.4f) {
        float *d = (float *)L()->door;
        V3 mn = v3(d[0] - d[3] * 0.5f, d[1] - d[4] * 0.5f, d[2] - d[5] * 0.5f);
        V3 mx = v3(d[0] + d[3] * 0.5f, d[1] + d[4] * 0.5f, d[2] + d[5] * 0.5f);
        collide_axis(&CAM_P, &pvel, hb, mn, mx, 0);
        V3 p2 = CAM_P; V3 v2 = pvel;
        collide_axis(&p2, &v2, hb, mn, mx, 2);
        CAM_P = p2; pvel = v2;
    }
}

static void cube_collide(void) {
    if (cube.held) return;
    /* gravity */
    cube.v.y -= 12.0f * 0.016f;
    cube.p.x += cube.v.x * 0.016f;
    cube.p.y += cube.v.y * 0.016f;
    cube.p.z += cube.v.z * 0.016f;
    /* floor */
    if (cube.p.y < cube.half) { cube.p.y = cube.half; cube.v.y = 0; }
    /* friction */
    cube.v.x *= 0.85f; cube.v.z *= 0.85f;
    /* room walls */
    float hw = RW * 0.5f, hd = RD * 0.5f;
    if (cube.p.x > hw - cube.half) { cube.p.x = hw - cube.half; cube.v.x = 0; }
    if (cube.p.x < -hw + cube.half) { cube.p.x = -hw + cube.half; cube.v.x = 0; }
    if (cube.p.z > hd - cube.half) { cube.p.z = hd - cube.half; cube.v.z = 0; }
    if (cube.p.z < -hd + cube.half) { cube.p.z = -hd + cube.half; cube.v.z = 0; }
    /* solids */
    const Sol *s = L()->solids;
    for (int i = 0; i < L()->nsolids; i++) {
        V3 mn = v3(s[i].x - s[i].sx * 0.5f, s[i].y - s[i].sy * 0.5f, s[i].z - s[i].sz * 0.5f);
        V3 mx = v3(s[i].x + s[i].sx * 0.5f, s[i].y + s[i].sy * 0.5f, s[i].z + s[i].sz * 0.5f);
        if (overlaps_box(cube.p, cube.half, mn, mx)) {
            /* push up onto the box top if falling onto it */
            if (cube.v.y <= 0 && cube.p.y - cube.half < mx.y && cube.p.y + cube.half > mx.y)
                { cube.p.y = mx.y + cube.half; cube.v.y = 0; }
            else {
                /* cheap nudge */
                float dx = cube.p.x - (s[i].x), dz = cube.p.z - (s[i].z);
                if (fabsf(dx) > fabsf(dz)) cube.p.x = (dx > 0 ? mx.x + cube.half : mn.x - cube.half);
                else cube.p.z = (dz > 0 ? mx.z + cube.half : mn.z - cube.half);
                cube.v.x = cube.v.z = 0;
            }
        }
    }
    /* player pushes the cube */
    float dx = cube.p.x - CAM_P.x, dz = cube.p.z - CAM_P.z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 1.0f && fabsf(CAM_P.y - 1.6f - cube.p.y) < 1.4f) {
        float push = (1.0f - dist) * 3.0f;
        if (dist > 1e-3f) { cube.v.x += dx / dist * push; cube.v.z += dz / dist * push; }
    }
}

static void update_lighting_buttons(void) {
    /* button pressed by cube? */
    button_held = 0;
    const LevelSpec *lv = L();
    if (lv->has_button) {
        V3 b = lv->button;
        V3 cc = cube.p;
        if (fabsf(cc.x - b.x) < 0.9f && fabsf(cc.z - b.z) < 0.9f && fabsf(cc.y - b.y) < 0.8f)
            button_held = 1;
    }
    /* door opens when button held */
    if (lv->has_door) {
        if (button_held) door_open += 0.016f * 1.5f; else door_open -= 0.016f * 1.5f;
        if (door_open < 0) door_open = 0;
        if (door_open > 1) door_open = 1;
    }
}

/* forward decls used above before their definitions */
static void check_teleport(Portal *src, Portal *dst);
static void level_complete(void);
static void draw_hud(void);

static void update_play(float dt) {
    /* move input -> world velocity */
    float speed = 5.0f;
    float fwd = -mvz, strafe = mvx;
    V3 wish = v3(CAM_R.x * strafe + CAM_F.x * fwd, 0,
                 CAM_R.z * strafe + CAM_F.z * fwd);
    float wl = sqrtf(wish.x * wish.x + wish.z * wish.z);
    if (wl > 1) { wish.x /= wl; wish.z /= wl; }
    pvel.x = wish.x * speed;
    pvel.z = wish.z * speed;

    /* gravity / jump */
    pvel.y -= 12.0f * dt;
    CAM_P.y += pvel.y * dt;
    if (CAM_P.y < 1.6f) { CAM_P.y = 1.6f; pvel.y = 0; grounded = 1; }
    else grounded = 0;

    /* integrate horizontal */
    CAM_P.x += pvel.x * dt;
    CAM_P.z += pvel.z * dt;
    player_collide();

    /* held cube follows in front of the camera */
    if (cube.held) {
        V3 in_front = v3(CAM_P.x + CAM_F.x * 1.4f, CAM_P.y + CAM_F.y * 1.4f - 1.1f,
                         CAM_P.z + CAM_F.z * 1.4f);
        cube.p = in_front;
    } else {
        cube_collide();
    }

    update_lighting_buttons();

    /* pickup / drop */
    if (grab_pressed) {
        grab_pressed = 0;
        if (cube.held) cube.held = 0;
        else {
            float dx = cube.p.x - CAM_P.x, dy = cube.p.y - (CAM_P.y - 0.8f), dz = cube.p.z - CAM_P.z;
            if (dx * dx + dy * dy + dz * dz < 3.5f) cube.held = 1;
        }
    }

    /* portal teleport */
    check_teleport(&PA, &PB);
    check_teleport(&PB, &PA);

    /* exit */
    const LevelSpec *lv = L();
    float ex = CAM_P.x - lv->exit.x, ez = CAM_P.z - lv->exit.z;
    if (ex * ex + ez * ez < lv->exit_r * lv->exit_r && CAM_P.y < lv->exit.y + 2.0f)
        level_complete();

    if (cooldown_blue > 0) cooldown_blue -= dt;
    if (cooldown_orange > 0) cooldown_orange -= dt;
    if (msg_t > 0) msg_t -= dt;
}

static void check_teleport(Portal *src, Portal *dst) {
    if (!src->on || !dst->on) return;
    V3 d = v3(CAM_P.x - src->c.x, CAM_P.y - src->c.y, CAM_P.z - src->c.z);
    float along = d.x * src->n.x + d.y * src->n.y + d.z * src->n.z;
    if (along > -0.6f && along < 0.6f) {
        /* inside the disc? */
        float side = d.x * src->up.x + d.y * src->up.y + d.z * src->up.z; /* vertical */
        if (fabsf(side) < 1.1f) {
            /* remove normal component to get in-plane distance */
            float px = d.x - src->n.x * along, py = d.y - src->n.y * along, pz = d.z - src->n.z * along;
            float rr = sqrtf(px * px + py * py + pz * pz);
            if (rr < src->r + 0.15f) {
                /* teleport out the other portal, keep momentum */
                float into = -(pvel.x * src->n.x + pvel.y * src->n.y + pvel.z * src->n.z);
                V3 tang = v3(pvel.x - src->n.x * (pvel.x * src->n.x + pvel.y * src->n.y + pvel.z * src->n.z),
                             pvel.y - src->n.y * (pvel.x * src->n.x + pvel.y * src->n.y + pvel.z * src->n.z),
                             pvel.z - src->n.z * (pvel.x * src->n.x + pvel.y * src->n.y + pvel.z * src->n.z));
                pvel = v3(dst->n.x * into + tang.x,
                          dst->n.y * into + tang.y,
                          dst->n.z * into + tang.z);
                CAM_P = v3(dst->c.x + dst->n.x * 0.35f,
                           dst->c.y + dst->n.y * 0.35f,
                           dst->c.z + dst->n.z * 0.35f);
            }
        }
    }
}

static void level_complete(void) {
    if (level_idx + 1 < LEVEL_N) { level_idx++; load_level(level_idx); }
    else { phase = PH_WIN; }
}

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */

void game_input_press(int x, int y) {
    if (phase == PH_MENU) { load_level(0); return; }
    if (phase == PH_WIN)  { load_level(0); return; }

    /* button zones on the right edge */
    if (x > GAME_W - 120) {
        if (y > GAME_H - 80)      { cooldown_blue = 0.5f; shoot_portal(&PA); return; }
        if (y > GAME_H - 160)     { cooldown_orange = 0.5f; shoot_portal(&PB); return; }
        if (y > GAME_H - 240)     { grab_pressed = 1; return; }
    }

    /* left half: move joystick */
    if (x < GAME_W / 2) {
        js_active = 1; js_ox = (float)x; js_oy = (float)y; mvx = mvz = 0;
        return;
    }
    /* right half: look drag */
    look_active = 1; last_lx = x; last_ly = y;
}

void game_input_move(int x, int y) {
    if (js_active) {
        float dx = x - js_ox, dy = y - js_oy;
        mvx = dx / 70.0f; mvz = dy / 70.0f;
        if (mvx > 1) mvx = 1;
        if (mvx < -1) mvx = -1;
        if (mvz > 1) mvz = 1;
        if (mvz < -1) mvz = -1;
    }
    if (look_active) {
        float dx = x - last_lx, dy = y - last_ly;
        CAM_YAW += dx * 0.005f;
        CAM_PITCH -= dy * 0.005f;
        if (CAM_PITCH > 1.5f) CAM_PITCH = 1.5f;
        if (CAM_PITCH < -1.5f) CAM_PITCH = -1.5f;
        last_lx = x; last_ly = y;
    }
}

void game_input_release(int x, int y) {
    (void)x; (void)y;
    js_active = 0; look_active = 0; mvx = mvz = 0;
}

/* ------------------------------------------------------------------ */
/* render                                                             */
/* ------------------------------------------------------------------ */

static void clear_bg(void) {
    /* soft bluish-grey "void" gradient so the chamber pops */
    for (int y = 0; y < GAME_H; y++) {
        float t = (float)y / GAME_H;
        int r = (int)(52 + 46 * t), g = (int)(56 + 46 * t), b = (int)(66 + 50 * t);
        uint32_t c = COL(r, g, b);
        uint32_t *row = FB + y * GAME_W;
        for (int x = 0; x < GAME_W; x++) row[x] = c;
    }
    for (int i = 0; i < GAME_W * GAME_H; i++) ZBUF[i] = 0.0f;
}

static void build_room(void) {
    float hw = RW * 0.5f, hd = RD * 0.5f, hh = RH;
    uint32_t wall = COL(214, 220, 228);
    uint32_t wallline = COL(150, 158, 168);
    uint32_t floorC = COL(188, 194, 202);
    uint32_t floorline = COL(128, 136, 146);
    uint32_t ceilC = COL(150, 158, 168);
    /* under the standing light the ceiling reads grey, but keep it soft */

    /* floor */
    quad(v3(-hw, 0, -hd), v3(hw, 0, -hd), v3(hw, 0, hd), v3(-hw, 0, hd), v3(0, 1, 0), floorC, 0);
    /* floor grid lines */
    for (float x = -hw; x <= hw; x += 1.5f)
        quad(v3(x, 0.006f, -hd), v3(x + 0.04f, 0.006f, -hd),
             v3(x + 0.04f, 0.006f, hd), v3(x, 0.006f, hd), v3(0, 1, 0), floorline, 1);
    for (float z = -hd; z <= hd; z += 1.5f)
        quad(v3(-hw, 0.006f, z), v3(hw, 0.006f, z),
             v3(hw, 0.006f, z + 0.04f), v3(-hw, 0.006f, z + 0.04f), v3(0, 1, 0), floorline, 1);

    /* ceiling */
    quad(v3(-hw, hh, -hd), v3(hw, hh, -hd), v3(hw, hh, hd), v3(-hw, hh, hd), v3(0, -1, 0), ceilC, 0);

    /* four walls (interior faces) */
    quad(v3(-hw, 0, -hd), v3(hw, 0, -hd), v3(hw, hh, -hd), v3(-hw, hh, -hd), v3(0, 0, 1), wall, 0);
    quad(v3(-hw, 0, hd), v3(hw, 0, hd), v3(hw, hh, hd), v3(-hw, hh, hd), v3(0, 0, -1), wall, 0);
    quad(v3(-hw, 0, -hd), v3(-hw, 0, hd), v3(-hw, hh, hd), v3(-hw, hh, -hd), v3(1, 0, 0), wall, 0);
    quad(v3(hw, 0, -hd), v3(hw, 0, hd), v3(hw, hh, hd), v3(hw, hh, -hd), v3(-1, 0, 0), wall, 0);

    /* accent vertical strips on walls */
    for (int i = -2; i <= 2; i++) {
        float t = i * 2.0f;
        quad(v3(-hw + 0.01f, 0, t - 0.06f), v3(-hw + 0.01f, 0, t + 0.06f),
             v3(-hw + 0.01f, hh, t + 0.06f), v3(-hw + 0.01f, hh, t - 0.06f), v3(1, 0, 0), wallline, 1);
        quad(v3(hw - 0.01f, 0, t - 0.06f), v3(hw - 0.01f, 0, t + 0.06f),
             v3(hw - 0.01f, hh, t + 0.06f), v3(hw - 0.01f, hh, t - 0.06f), v3(-1, 0, 0), wallline, 1);
    }
}

static void build_solids(void) {
    LevelSpec *lv = (LevelSpec *)L(); /* avoid const cast issue via memcpy below */
    static Sol sl[SMAX];
    memcpy(sl, lv->solids, sizeof(Sol) * lv->nsolids);
    uint32_t c = COL(172, 178, 190);
    for (int i = 0; i < lv->nsolids; i++)
        box(v3(sl[i].x, sl[i].y, sl[i].z), v3(sl[i].sx, sl[i].sy, sl[i].sz), c);
}

static void build_door(void) {
    LevelSpec *lv = (LevelSpec *)L();
    if (!lv->has_door) return;
    float *d = (float *)lv->door;
    float lift = door_open * (d[4] * 0.95f);
    box(v3(d[0], d[1] + lift, d[2]), v3(d[3], d[4], d[5]),
        shade(COL(120, 170, 205), v3(0, 0, 1), 1));
}

static void build_button(void) {
    LevelSpec *lv = (LevelSpec *)L();
    if (!lv->has_button) return;
    V3 b = lv->button;
    uint32_t col = button_held ? COL(90, 200, 90) : COL(200, 70, 70);
    box(v3(b.x, b.y, b.z), v3(1.6f, 0.3f, 1.6f), col);
    box(v3(b.x, b.y + 0.2f, b.z), v3(0.6f, 0.2f, 0.6f), shade(col, v3(0, 1, 0), 1));
}

static void build_exit(void) {
    LevelSpec *lv = (LevelSpec *)L();
    V3 e = lv->exit;
    /* glowing floor pad */
    disc3d(v3(e.x, e.y + 0.08f, e.z), v3(0, 1, 0), v3(0, 0, -1), lv->exit_r, COL(30, 40, 44), 1, 24);
    disc3d(v3(e.x, e.y + 0.1f, e.z), v3(0, 1, 0), v3(0, 0, -1), lv->exit_r * 0.9f, COL(90, 240, 225), 1, 24);
    /* a thin light pillar */
    box(v3(e.x, e.y + 1.3f, e.z), v3(0.08f, 2.6f, 0.08f), COL(170, 255, 245));
    /* floating beacon */
    box(v3(e.x, e.y + 2.6f, e.z), v3(0.5f, 0.25f, 0.5f), COL(200, 255, 250));
}

static void build_cube(void) {
    cube3d(cube.p, cube.half * 1.25f, COL(240, 165, 70));
}

static void build_portals(void) {
    /* dark outer ring + inner glow, like a real portal */
    if (PA.on) { disc3d(PA.c, PA.n, PA.up, PA.r * 1.25f, COL(10, 10, 14), 1, 24);
                 disc3d(PA.c, PA.n, PA.up, PA.r, PA.col, 1, 24);
                 disc3d(v3(PA.c.x + PA.n.x * 0.02f, PA.c.y + PA.n.y * 0.02f, PA.c.z + PA.n.z * 0.02f),
                        PA.n, PA.up, PA.r * 0.7f, COL(200, 225, 255), 1, 24); }
    if (PB.on) { disc3d(PB.c, PB.n, PB.up, PB.r * 1.25f, COL(10, 10, 14), 1, 24);
                 disc3d(PB.c, PB.n, PB.up, PB.r, PB.col, 1, 24);
                 disc3d(v3(PB.c.x + PB.n.x * 0.02f, PB.c.y + PB.n.y * 0.02f, PB.c.z + PB.n.z * 0.02f),
                        PB.n, PB.up, PB.r * 0.7f, COL(255, 225, 185), 1, 24); }
}

static void render(void) {
    cam_basis();
    clear_bg();
    build_room();
    build_solids();
    build_door();
    build_button();
    build_exit();
    build_cube();
    build_portals();
    draw_hud();
}

/* ------------------------------------------------------------------ */
/* HUD                                                                */
/* ------------------------------------------------------------------ */

static void draw_buttons(void) {
    /* three arcade buttons bottom-right */
    for (int i = 0; i < 3; i++) {
        int cx = GAME_W - 60, cy = GAME_H - 60 - i * 80;
        uint32_t c = i == 0 ? COL(90, 150, 255) : (i == 1 ? COL(255, 150, 50) : COL(230, 200, 80));
        disc(cx, cy, 26, COL(30, 30, 40));
        disc(cx, cy, 22, c);
        disc(cx, cy, 12, COL(20, 20, 28));
    }
    draw_text(GAME_W - 96, GAME_H - 74, 2, COL(120, 170, 255), "СИН");
    draw_text(GAME_W - 96, GAME_H - 150, 2, COL(255, 190, 110), "ОРН");
    draw_text(GAME_W - 96, GAME_H - 226, 2, COL(230, 220, 120), "ВЗЯТЬ");
}

static void draw_hud(void) {
    const LevelSpec *lv = L();

    if (phase == PH_MENU) {
        draw_text_c(GAME_W / 2, 90, 12, COL(120, 200, 255), "PvG3: ПОРТАЛ");
        draw_text_c(GAME_W / 2, 250, 5, COL(240, 240, 230), "3D-ГОЛОВОЛОМКА");
        draw_text_c(GAME_W / 2, 330, 3, COL(200, 210, 220), "НАЖМИ ЧТОБЫ НАЧАТЬ");
        return;
    }

    if (phase == PH_WIN) {
        draw_text_c(GAME_W / 2, 200, 10, COL(120, 255, 200), "ВСЕ ТЕСТЫ ПРОЙДЕНЫ!");
        draw_text_c(GAME_W / 2, 320, 4, COL(230, 230, 220), "НАЖМИ ЧТОБЫ НАЧАТЬ ЗАНОВО");
        return;
    }

    /* top banner */
    rect(0, 0, GAME_W, 52, COL(12, 14, 20));
    draw_text(16, 10, 3, COL(150, 200, 255), "УРОВЕНЬ");
    int x = 16 + text_w(3, "УРОВЕНЬ ") + 6;
    draw_int(x, 10, 3, COL(255, 220, 120), level_idx + 1);
    draw_text(GAME_W / 2 - text_w(3, lv->name) / 2, 10, 3, COL(240, 240, 230), lv->name);

    /* objective / hint */
    draw_text(GAME_W / 2 - text_w(2, lv->hint) / 2, 58, 2, COL(255, 235, 150), lv->hint);
    /* controls reminder */
    draw_text(GAME_W / 2 - text_w(2, "ЛЕВО — движение  ПРАВО — камера") / 2, 82, 2,
              COL(170, 200, 220), "ЛЕВО — движение  ПРАВО — камера");

    /* message centre */
    if (msg && msg_t > 0 && phase == PH_PLAY)
        draw_text_c(GAME_W / 2, 300, 4, COL(255, 240, 180), msg);

    draw_buttons();
}

/* ------------------------------------------------------------------ */
/* public API                                                         */
/* ------------------------------------------------------------------ */

void game_init(void) {
    RNG = 0xC0FFEE11u;
    memset(ZBUF, 0, sizeof ZBUF);
    FY = 1.0f / tanf(GAME_FOV * 0.5f);
    FX = FY * ((float)GAME_H / (float)GAME_W); /* aspect = w/h → x scale = f/(w/h) */
    CAM_PITCH = -0.05f;
    global_t = 0;
    grab_pressed = 0;
    msg = NULL; msg_t = 0;
    /* a nice chamber behind the title screen */
    load_level(0);
    phase = PH_MENU;
}

void game_debug_snapshot(void) {
    game_init();
    load_level(0);
    phase = PH_PLAY;
    /* a nice static vantage point showing the cube, a portal and the exit */
    CAM_P.x = -6.2f; CAM_P.z = 4.5f; CAM_P.y = 1.7f;
    CAM_YAW = 1.15f; CAM_PITCH = -0.02f;
    msg = "СИНИЙ И ОРАНЖЕВЫЙ ПОРТАЛЫ"; msg_t = 6.0f;
}

void game_tick(float dt, uint32_t *fb) {
    FB = fb;
    if (dt > 0.05f) dt = 0.05f;
    if (phase == PH_PLAY) update_play(dt);
    else global_t += dt;
    render();
}

int game_phase(void) { return (int)phase; }
