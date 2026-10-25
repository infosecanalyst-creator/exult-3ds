#!/usr/bin/env python3
"""Build fonts_modern.vga for Exult-3DS: a modern, readable replacement for
Ultima VII's blackletter text fonts.

Input:  Exult's own data/fonts_serif.vga (GPL, part of Exult) and DejaVu Sans
        Bold (Bitstream Vera / DejaVu license).
Output: a 22-entry font flex with the same layout as fonts_serif.vga; the
        main text fonts (0 and 7) are replaced by DejaVu Sans Bold 10 px in a
        neutral light grey with a 1-pixel black outline. Runic/Ophidian scripts
        and the small book/label fonts are kept as they are.

usage: make_fonts.py fonts_serif.vga DejaVuSans-Bold.ttf fonts_modern.vga
       make_fonts.py fonts_serif.vga pixel fonts_modern.vga   (U7 Pixel Sans)
"""
import struct, sys
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, __import__('os').path.dirname(__file__))
from u7shape import flex_entries, decode_shape, encode_frame, encode_shape

FILL, OUTLINE = 118, 0          # U7 palette: (236,236,236) and black
YA, YB = 10, 3                  # same cell as the original font (14 px lines)
PX = 10

def glyph(font, ch):
    adv = max(1, round(font.getlength(ch)))
    w, h = adv + 4, YA + YB + 1
    m = Image.new('1', (w, h), 0)
    d = ImageDraw.Draw(m); d.fontmode = '1'
    d.text((1, YA), ch, font=font, fill=1, anchor='ls')
    px = m.load()
    on = [[px[x, y] for x in range(w)] for y in range(h)]
    img = [[None] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if on[y][x]:
                img[y][x] = FILL
            elif any(0 <= y + dy < h and 0 <= x + dx < w and on[y + dy][x + dx]
                     for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                img[y][x] = OUTLINE
    # trim to advance + outline (left col 0 is outline, ink starts at 1)
    width = adv + 2
    img = [r[:width] for r in img]
    return encode_frame(img, 0, YA)

def pixel_glyph(rows):
    """Frame from a hand-drawn U7 Pixel Sans glyph (rows 0..10, baseline row 8)."""
    inkw = len(rows[0])
    w, h = inkw + 3, YA + YB + 1          # outline | ink | outline | gap
    on = [[False] * w for _ in range(h)]
    for gy, r in enumerate(rows):
        for gx, c in enumerate(r):
            if c == '#':
                on[gy + 2][gx + 1] = True   # glyph row 8 -> frame row 10 (baseline)
    img = [[None] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if on[y][x]:
                img[y][x] = FILL
            elif any(0 <= y + dy < h and 0 <= x + dx < w and on[y + dy][x + dx]
                     for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                img[y][x] = OUTLINE
    return encode_frame(img, 0, YA)

def main(src, ttf, dst):
    d = open(src, 'rb').read()
    E = flex_entries(d)
    pixel = ttf == 'pixel'                # hand-drawn U7 Pixel Sans instead of a TTF
    if pixel:
        from u7pixel_glyphs import G
    else:
        font = ImageFont.truetype(ttf, PX)
    out = []
    for i, (o, l) in enumerate(E):
        blob = d[o:o + l]
        if i in (0, 7):
            old = decode_shape(blob)
            frames = []
            for code in range(len(old)):
                ch = chr(code)
                if 32 <= code < 127:
                    frames.append(pixel_glyph(G[ch]) if pixel else glyph(font, ch))
                else:   # keep Exult's glyph (bullets etc.) but drop the yellow
                    f = old[code]
                    if f:
                        img = [[None if v is None else (OUTLINE if v == 0 else FILL) for v in r] for r in f['img']]
                        frames.append(encode_frame(img, f['xl'], f['ya']))
                    else:
                        frames.append(encode_frame([[None]], 0, 0))
            blob = encode_shape(frames)
        out.append(blob)
    # same 0x80-byte header as the source, entry table rebuilt
    n = len(out)
    hdr = bytearray(d[:0x80]); hdr[:0x50] = b'Exult-3DS modern fonts'.ljust(0x50, b'\0')
    table = bytearray(); data = bytearray(); base = 0x80 + 8 * n
    for b in out:
        table += struct.pack('<II', base + len(data) if b else 0, len(b)); data += b
    open(dst, 'wb').write(bytes(hdr) + bytes(table) + bytes(data))
    print('wrote', dst, len(hdr) + len(table) + len(data), 'bytes')

if __name__ == '__main__':
    main(*sys.argv[1:4])
