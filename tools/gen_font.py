import os
from PIL import Image, ImageDraw, ImageFont

font_path = r"C:\Windows\Fonts\segoeui.ttf"
if not os.path.exists(font_path):
    font_path = r"C:\Windows\Fonts\arial.ttf"
fnt = ImageFont.truetype(font_path, 13)

MAX_W = 11
advs = []
lines = [
    '#include "font.h"',
    '#include "fb.h"',
    '',
    '/* Proportional 8-bit anti-aliased Sans-Serif UI font (Fedora Cantarell/Inter style) */',
    f'static const uint8_t font_aa[95][16][{MAX_W}] = {{'
]

for code in range(32, 127):
    ch = chr(code)
    canvas = Image.new("L", (MAX_W, 16), 0)
    if ch == " ":
        adv = 4
    else:
        tmp = Image.new("L", (32, 32), 0)
        drw = ImageDraw.Draw(tmp)
        drw.text((2, 20), ch, fill=255, font=fnt, anchor="ls")
        band = tmp.crop((0, 8, 32, 24))
        bbox = band.getbbox()
        if bbox:
            gx0, gy0, gx1, gy1 = bbox
            gw = min(MAX_W - 1, gx1 - gx0)
            glyph_strip = band.crop((gx0, 0, gx0 + gw, 16))
            canvas.paste(glyph_strip, (0, 0))
            adv = gw + 1
            if ch in ".,:;!|'iIl1":
                adv = max(3, gw + 1)
        else:
            adv = 4
    advs.append(adv)

    rows_str = []
    for r in range(16):
        vals = []
        for c in range(MAX_W):
            v = canvas.getpixel((c, r))
            v = min(255, int((v / 255.0) ** 0.70 * 255.0)) if v > 12 else 0
            vals.append(f"0x{v:02X}")
        rows_str.append("{" + ",".join(vals) + "}")
    lines.append("    {" + ", ".join(rows_str) + f"}}, /* {code} */")

lines += [
    '};',
    '',
    'static const uint8_t font_adv[95] = {',
    '    ' + ', '.join(str(a) for a in advs),
    '};',
    '',
    'int32_t font_str_width(const char *s) {',
    '    if (!s) return 0;',
    '    int32_t w = 0;',
    '    for (; *s && *s != \'\\n\'; s++) {',
    '        uint8_t idx = (*s >= 32 && *s <= 126) ? (uint8_t)(*s - 32) : 0;',
    '        w += (int32_t)font_adv[idx];',
    '    }',
    '    return w;',
    '}',
    '',
    'void font_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg, bool transparent_bg) {',
    '    uint8_t idx = (c >= 32 && c <= 126) ? (uint8_t)(c - 32) : 0;',
    '    int32_t adv = (int32_t)font_adv[idx];',
    '    if (!transparent_bg) {',
    '        fb_fill_rect(x, y, adv, FONT_H, bg);',
    '    }',
    '    for (int32_t row = 0; row < FONT_H; row++) {',
    '        const uint8_t *prow = font_aa[idx][row];',
    f'        for (int32_t col = 0; col < {MAX_W}; col++) {{',
    '            uint8_t a = prow[col];',
    '            if (a) fb_put_pixel_alpha(x + col, y + row, fg, a);',
    '        }',
    '    }',
    '}',
    '',
    'void font_draw_str(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg, bool transparent_bg) {',
    '    if (!s) return;',
    '    int32_t cx = x;',
    '    int32_t cy = y;',
    '    for (; *s; s++) {',
    "        if (*s == '\\n') {",
    '            cx = x;',
    '            cy += FONT_H;',
    '            continue;',
    '        }',
    '        uint8_t idx = (*s >= 32 && *s <= 126) ? (uint8_t)(*s - 32) : 0;',
    '        font_draw_char(cx, cy, *s, fg, bg, transparent_bg);',
    '        cx += (int32_t)font_adv[idx];',
    '    }',
    '}',
    ''
]

with open(r"F:\flux-os\src\gfx\font.c", "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print("Generated proportional Fedora-style AA font.c OK")
