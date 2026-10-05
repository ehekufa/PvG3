#ifndef PVG3_FONT_H_INCLUDED
#define PVG3_FONT_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

/* The same OFL-licensed embedded PT Sans is shared with LVGL's Tiny TTF. */
const unsigned char *font_ttf_data(size_t *length);

/* PT Sans Regular: OFL-licensed TrueType embedded at build time. Text size
 * uses the game's original 1..8 scale; uppercase letters are about 7*size px. */
enum { FONT_LANG_RU = 0, FONT_LANG_EN = 1 };
int font_init(void);
int font_language(void);
void font_set_language(int language);
/* Set a per-install preferences file path; NULL disables persistence. */
void font_set_language_path(const char *path);
const char *font_translate(const char *text);
int font_has_glyph(uint32_t codepoint);
int font_width(int size, const char *utf8);
void font_draw(uint32_t *rgba, int width, int height,
               int x, int y, int size, uint32_t color, const char *utf8);

#endif /* PVG3_FONT_H_INCLUDED */
