#ifndef PVG3_FONT_H_INCLUDED
#define PVG3_FONT_H_INCLUDED

#include <stdint.h>

/* PT Sans Regular: OFL-licensed TrueType embedded at build time. Text size
 * uses the game's original 1..8 scale; uppercase letters are about 7*size px. */
int font_init(void);
int font_has_glyph(uint32_t codepoint);
int font_width(int size, const char *utf8);
void font_draw(uint32_t *rgba, int width, int height,
               int x, int y, int size, uint32_t color, const char *utf8);

#endif /* PVG3_FONT_H_INCLUDED */
