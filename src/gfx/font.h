#ifndef FLUX_FONT_H
#define FLUX_FONT_H

#include "types.h"

#define FONT_W 8
#define FONT_H 16

void     font_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg, bool transparent_bg);
void     font_draw_str(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg, bool transparent_bg);
int32_t  font_str_width(const char *s);

#endif
