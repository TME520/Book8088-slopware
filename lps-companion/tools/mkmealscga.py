#!/usr/bin/env python3
"""LPS-DOS-115-CHG14: MEALSCGA.DAT, the Meal Planner pictures for the CGA screen.

Source: the same user sheets and the same crop as tools/mkmeals.py (imported:
the PicoCalc meal_sprites.py card bounds, crop and flood fill of the near-white
background). Nothing is redrawn. Conversion "B", chosen by the user from three
trial sheets (Atkinson error diffusion):

  1. Fit the drawing into the CHG13 card's picture box, 160 x 96 CGA pixels,
     keeping its proportions on a 4:3 screen (a 320 x 200 pixel is 1.2 times
     taller than wide): premultiplied-alpha box filter; alpha < 1/2 is the
     card background.
  2. Saturation x1.35 around the pixel's RGB mean, then contrast x1.15
     around 128, clipped to 0-255.
  3. Serpentine Atkinson error diffusion in linear light (6 neighbours, 1/8
     each, 3/4 of the error), each pixel taking the palette colour nearest in
     CIELAB; the error is clamped to +-0.5 and never spreads to the card
     background, so the drawing's edge stays clean.

Palette: mode 04h, palette 0 high intensity: 0 the background register (black),
1 light green, 2 light red, 3 yellow. The background is always 0, never
dithered.

Python 3 standard library only, IEEE double arithmetic: the same sheets give
the same MEALSCGA.DAT on every machine, so `make test` rebuilds it and compares
it with the committed ../MEALSCGA.DAT.

    python3 tools/mkmealscga.py MEALSCGA.DAT [--preview DIR]

File layout (little-endian), read by src/meals.c:
   0  8 bytes  magic "LPSMEALC"
   8  u16      version (1)
  10  u16      picture count (13, the order of src/meals.h)
  12  u16      width in pixels (160)
  14  u16      height in pixels (96)
  16  u32      bytes per picture (width / 4 * height = 3840)
  20  u32      CRC-32 of the table that follows
  24  table: per picture the u32 checksum of its bytes (as MEALS.DAT: two
      16-bit running sums over little-endian words, s2 << 16 | s1)
  76  pictures: height rows of width / 4 bytes, 4 pixels a byte, the leftmost
      pixel in bits 7-6 (the CGA's own layout, so a row is one copy)
"""
import argparse
import pathlib
import struct
import sys
import zlib

sys.dont_write_bytecode = True  # no __pycache__ in tools/
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mkmeals  # noqa: E402  (the crop, the PNG reader/writer, the checksum)

W, H = 160, 96
ASPECT = 1.2               # a CGA 320 x 200 pixel is 1.2 times taller than wide
SATURATION, CONTRAST = 1.35, 1.15
CLAMP = 0.5
MAGIC, VERSION = b"LPSMEALC", 1
PALETTE = [(0, 0, 0), (85, 255, 85), (255, 85, 85), (255, 255, 85)]
ATKINSON = [(1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)]


def to_linear(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def to_srgb(c):
    return (c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055) * 255.0


def lab(r, g, b):
    """CIELAB (D65) of an sRGB colour given as 0-255 floats."""
    lr, lg, lb = to_linear(r), to_linear(g), to_linear(b)
    x = (0.4124 * lr + 0.3576 * lg + 0.1805 * lb) / 0.9505
    y = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb
    z = (0.0193 * lr + 0.1192 * lg + 0.9505 * lb) / 1.089
    fx, fy, fz = (v ** (1 / 3) if v > 0.008856 else 7.787 * v + 16 / 116 for v in (x, y, z))
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


PAL_LIN = [tuple(to_linear(float(v)) for v in c) for c in PALETTE]
PAL_LAB = [lab(*map(float, c)) for c in PALETTE]


def fit(px):
    """Premultiplied box filter into W x H; (rgb floats or None) per pixel."""
    ch, cw = len(px), len(px[0])
    s = min(W / cw, H * ASPECT / ch)
    ow, oh = max(1, round(cw * s)), max(1, round(ch * s / ASPECT))
    pre = [[(0.0, 0.0, 0.0, 0.0) if c is None else (float(c[0]), float(c[1]), float(c[2]), 1.0) for c in row] for row in px]
    hw = mkmeals.box_weights(cw, ow)
    rows = []
    for row in pre:
        line = []
        for taps in hw:
            r = g = b = a = 0.0
            for x, w in taps:
                p = row[x]
                r += p[0] * w; g += p[1] * w; b += p[2] * w; a += p[3] * w
            line.append((r, g, b, a))
        rows.append(line)
    out = [[None] * W for _ in range(H)]
    ox, oy = (W - ow) // 2, (H - oh) // 2
    for y, taps in enumerate(mkmeals.box_weights(ch, oh)):
        for x in range(ow):
            r = g = b = a = 0.0
            for sy, w in taps:
                p = rows[sy][x]
                r += p[0] * w; g += p[1] * w; b += p[2] * w; a += p[3] * w
            if a >= 0.5:
                out[oy + y][ox + x] = (min(255.0, r / a), min(255.0, g / a), min(255.0, b / a))
    return out


def adjust(c):
    m = (c[0] + c[1] + c[2]) / 3.0
    return tuple(min(255.0, max(0.0, 128.0 + ((m + (v - m) * SATURATION) - 128.0) * CONTRAST)) for v in c)


def nearest(c):
    """Palette index nearest in CIELAB to a linear-light colour (first on ties)."""
    l = lab(*(to_srgb(v) for v in c))
    best, pick = None, 0
    for i, p in enumerate(PAL_LAB):
        d = (l[0] - p[0]) ** 2 + (l[1] - p[1]) ** 2 + (l[2] - p[2]) ** 2
        if best is None or d < best:
            best, pick = d, i
    return pick


def convert(art):
    """Atkinson error diffusion, serpentine; returns H rows of W indices 0-3."""
    work = [[None if c is None else [to_linear(v) for v in adjust(c)] for c in row] for row in art]
    out = [[0] * W for _ in range(H)]
    for y in range(H):
        d = 1 if y % 2 == 0 else -1
        for x in (range(W) if d == 1 else range(W - 1, -1, -1)):
            c = work[y][x]
            if c is None:
                continue
            c = [min(1.0, max(0.0, v)) for v in c]
            k = nearest(c)
            out[y][x] = k
            err = [min(CLAMP, max(-CLAMP, c[i] - PAL_LIN[k][i])) for i in range(3)]
            for dx, dy in ATKINSON:
                nx, ny = x + dx * d, y + dy
                if 0 <= nx < W and ny < H and work[ny][nx] is not None:
                    n = work[ny][nx]
                    for i in range(3):
                        n[i] += err[i] / 8
    return out


def pack(idx):
    data = bytearray()
    for row in idx:
        for xb in range(0, W, 4):
            data.append((row[xb] << 6) | (row[xb + 1] << 4) | (row[xb + 2] << 2) | row[xb + 3])
    return bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=pathlib.Path)
    parser.add_argument("--preview", type=pathlib.Path)
    args = parser.parse_args()
    sheets = [mkmeals.Sheet(p) for p in mkmeals.SHEETS]
    table, pictures, previews = b"", b"", []
    for name, sheet_no, index in mkmeals.SPRITES:
        art = fit(mkmeals.illustration(sheets[sheet_no - 1], mkmeals.card_box(index), name == "Blank"))
        idx = convert(art)
        data = pack(idx)
        table += struct.pack("<I", mkmeals.picture_sum(data))
        pictures += data
        previews.append(idx)
    header = MAGIC + struct.pack("<HHHHII", VERSION, len(mkmeals.SPRITES), W, H, W // 4 * H, zlib.crc32(table))
    args.out.write_bytes(header + table + pictures)
    if args.preview:
        # The pictures as a CGA monitor shows them: pixels 1.2 times taller, x2.
        args.preview.mkdir(parents=True, exist_ok=True)
        cols, cw, chh = 5, W * 2 + 16, round(H * 2 * ASPECT) + 16
        rows_n = (len(previews) + cols - 1) // cols
        canvas = [[(40, 40, 40)] * (cols * cw) for _ in range(rows_n * chh)]
        for n, idx in enumerate(previews):
            ox, oy = (n % cols) * cw + 8, (n // cols) * chh + 8
            for y in range(chh - 16):
                src = idx[min(H - 1, int(y / (2 * ASPECT)))]
                for x in range(W * 2):
                    canvas[oy + y][ox + x] = PALETTE[src[x // 2]]
        mkmeals.write_png(args.preview / "mealscga_preview.png", cols * cw, rows_n * chh,
                          [bytes(v for c in row for v in c) for row in canvas])


if __name__ == "__main__":
    main()
