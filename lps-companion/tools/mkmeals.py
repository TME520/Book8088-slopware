#!/usr/bin/env python3
"""LPS-DOS-115-CHG07: MEALS.DAT, the Meal Planner pictures for the EGA screen.

Source: the user's own card sheets, assets/meal_deck_sheet1.png and
assets/meal_deck_sheet2.png (byte-for-byte copies of the PicoCalc
repository's assets/, "Daily Edible Deck"). Nothing is redrawn: for each of
the 13 distinct cards this tool does what the PicoCalc's tools/meal_sprites.py
does (same card bounds, same crop, same flood fill of the near-white
background, same border and ribbon sampling), then fits the illustration into
240 x 128 EGA pixels (640 x 350 pixels are 48/35 times taller than wide) and
picks its colours from the EGA's 64:

  palette index 2 card body (white)   3 ink (black)
                4 card border         5 portion ribbon   (nearest EGA colour)
                6-15 ten colours chosen for the illustration
  (indices 0 and 1 belong to the screen: background and text)

Every illustration pixel takes the nearest of indices 2-15 (no dithering: the
drawings are pixel art); the transparent background becomes the card body.

Python 3 standard library only (zlib for the PNG), integer and IEEE double
arithmetic: the same sheets give the same MEALS.DAT on every machine, so
`make test` rebuilds it and compares it with the committed ../MEALS.DAT.

    python3 tools/mkmeals.py MEALS.DAT [--preview DIR]

File layout (little-endian), read by src/meals.c:
   0  8 bytes  magic "LPSMEALS"
   8  u16      version (1)
  10  u16      picture count (13)
  12  u16      width in pixels (240)
  14  u16      height in pixels (128)
  16  u32      bytes per picture (width / 8 * height * 4 planes)
  20  u32      CRC-32 of the table that follows
  24  table: per picture 16 palette bytes (EGA colour values 0-63 for
      palette indices 0-15) and the u32 checksum of its pixels (below)
 284  pictures: per picture its 4 bit planes in order (plane 0 = bit 0 of
      the palette index), each plane height rows of width / 8 bytes, the
      leftmost pixel in the high bit

Picture checksum: two 16-bit running sums over the picture's little-endian
16-bit words, s1 += word, s2 += s1 (both modulo 65536), stored as s2 << 16 | s1.
A CRC-32 of 15 KB takes 1.2 s at 8088 speed; this takes about 0.1 s. The
table, which holds these checksums, has a CRC-32 like ROSARY.DAT.
"""
import argparse
import pathlib
import struct
import sys
import zlib
from collections import deque

W, H = 240, 128
ASPECT_NUM, ASPECT_DEN = 48, 35  # an EGA 640 x 350 pixel is 48/35 times taller than wide
ROOT = pathlib.Path(__file__).resolve().parent.parent
SHEETS = [ROOT / "assets" / "meal_deck_sheet1.png", ROOT / "assets" / "meal_deck_sheet2.png"]
# Card bounds on the 1055 x 1491 sheets (border included): PicoCalc tools/meal_sprites.py.
COLS = [(44, 515), (539, 1010)]
ROWS = [(108, 435), (459, 783), (807, 1122), (1145, 1454)]
# Picture id -> (sheet, card index 0-7 in reading order); the order of src/meals.h.
SPRITES = [
    ("Latte", 1, 0), ("Black", 1, 1), ("Tea", 2, 0), ("MintTea", 2, 3), ("Juice", 2, 5),
    ("Wrap", 1, 3), ("Salad", 1, 5), ("Biscuit", 1, 6), ("Cheese", 1, 7),
    ("Apple", 2, 1), ("Orange", 2, 2), ("Soup", 2, 4), ("Blank", 2, 6),
]
BACKGROUND = 232  # min(R,G,B) at or above this, reachable from the edge, is background
MAGIC, VERSION = b"LPSMEALS", 1
BODY, INK = 0x3F, 0x00     # white card, black ink
FIRST_ART, SLOTS = 6, 16   # palette indices 6-15 are picked per picture


def ega_rgb(v):
    """EGA colour value rgbRGB (bit 0 blue 2/3 ... bit 5 red 1/3) to RGB."""
    return tuple(170 * ((v >> hi) & 1) + 85 * ((v >> lo) & 1) for hi, lo in ((2, 5), (1, 4), (0, 3)))


EGA = [ega_rgb(v) for v in range(64)]


def distance(a, b):
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    return 3 * dr * dr + 4 * dg * dg + 2 * db * db


def nearest_ega(c):
    return min(range(64), key=lambda v: (distance(c, EGA[v]), v))


# ---- PNG (8-bit RGB or RGBA, not interlaced) --------------------------------
def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, header = 8, [], None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    width, height, depth, ctype, _, _, interlace = header
    if depth != 8 or ctype not in (2, 6) or interlace:
        sys.exit(f"{path}: needs 8-bit RGB/RGBA, not interlaced")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    prev = bytearray(stride)
    rows = []
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                if i >= bpp:
                    a, c = line[i - bpp], prev[i - bpp]
                else:
                    a = c = 0
                b = prev[i]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif f != 0:
            sys.exit(f"{path}: bad filter {f}")
        rows.append(bytes(line) if bpp == 3 else bytes(b for i, b in enumerate(line) if i % 4 != 3))
        prev = line
    return width, height, rows


def write_png(path, width, height, rgb_rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rgb_rows)
    chunk = lambda kind, body: struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


class Sheet:
    def __init__(self, path):
        self.width, self.height, self.rows = read_png(path)

    def pixel(self, x, y):
        r = self.rows[y]
        return r[3 * x], r[3 * x + 1], r[3 * x + 2]


# ---- The PicoCalc's crop and samples ------------------------------------------
def card_box(index):
    (x0, x1), (y0, y1) = COLS[index % 2], ROWS[index // 2]
    return x0, y0, x1, y1


def sample(sheet, box):
    """Median colour of a region (per channel)."""
    x0, y0, x1, y1 = box
    px = [sheet.pixel(x, y) for y in range(y0, y1) for x in range(x0, x1)]
    return tuple(sorted(c[i] for c in px)[len(px) // 2] for i in range(3))


def border_colour(sheet, box):
    x0, y0, x1, y1 = box
    y = (y0 + y1) // 2
    run = [sheet.pixel(x, y) for x in range(x0 - 2, x0 + 14)]
    return max(run, key=lambda c: max(c) - min(c))  # the first most saturated pixel of the border line


def illustration(sheet, box, blank):
    """The crop with its background flood-filled away: rows of (r, g, b) or None."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    top, bottom = (0.12, 0.88) if blank else (0.03, 0.66)
    cx0, cy0, cx1, cy1 = x0 + int(w * 0.12), y0 + int(h * top), x1 - int(w * 0.12), y0 + int(h * bottom)
    cw, ch = cx1 - cx0, cy1 - cy0
    px = [[sheet.pixel(cx0 + x, cy0 + y) for x in range(cw)] for y in range(ch)]
    seen = bytearray(cw * ch)
    queue = deque((x, y) for x in range(cw) for y in (0, ch - 1))
    queue.extend((x, y) for y in range(ch) for x in (0, cw - 1))
    while queue:
        x, y = queue.popleft()
        if seen[y * cw + x]:
            continue
        seen[y * cw + x] = 1
        c = px[y][x]
        if c is None or min(c) < BACKGROUND:
            continue
        px[y][x] = None
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < cw and 0 <= ny < ch and not seen[ny * cw + nx]:
                queue.append((nx, ny))
    ys = [y for y in range(ch) if any(c is not None for c in px[y])]
    xs = [x for x in range(cw) if any(px[y][x] is not None for y in range(ch))]
    return [row[xs[0]:xs[-1] + 1] for row in px[ys[0]:ys[-1] + 1]]


# ---- Fit into W x H EGA pixels -------------------------------------------------
def box_weights(src, dst):
    """For each output index, the (source index, weight) pairs of a box filter."""
    out = []
    for i in range(dst):
        a, b = i * src / dst, (i + 1) * src / dst
        taps, s = [], int(a)
        while s < b and s < src:
            w = min(b, s + 1) - max(a, s)
            if w > 0:
                taps.append((s, w))
            s += 1
        total = sum(w for _, w in taps)
        out.append([(s, w / total) for s, w in taps])
    return out


def fit(px):
    """Premultiplied-alpha box filter into W x H; None where alpha < 1/2."""
    ch, cw = len(px), len(px[0])
    # Square source pixels: cw x ch shows as cw*s x ch*s/aspect EGA pixels.
    s = min(W / cw, H * ASPECT_NUM / (ch * ASPECT_DEN))
    ow, oh = max(1, round(cw * s)), max(1, round(ch * s * ASPECT_DEN / ASPECT_NUM))
    pre = [[(0.0, 0.0, 0.0, 0.0) if c is None else (float(c[0]), float(c[1]), float(c[2]), 1.0) for c in row] for row in px]
    hw = box_weights(cw, ow)
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
    vw = box_weights(ch, oh)
    out = [[None] * W for _ in range(H)]
    ox, oy = (W - ow) // 2, (H - oh) // 2
    for y, taps in enumerate(vw):
        for x in range(ow):
            r = g = b = a = 0.0
            for sy, w in taps:
                p = rows[sy][x]
                r += p[0] * w; g += p[1] * w; b += p[2] * w; a += p[3] * w
            if a >= 0.5:
                out[oy + y][ox + x] = (min(255, round(r / a)), min(255, round(g / a)), min(255, round(b / a)))
    return out


def choose_palette(pixels, fixed):
    """fixed (palette indices 2-5) plus 10 EGA colours that serve the picture best."""
    hist = {}
    for c in pixels:
        hist[c] = hist.get(c, 0) + 1
    colours = sorted(hist)
    counts = [hist[c] for c in colours]
    dist = [[distance(c, EGA[v]) for v in range(64)] for c in colours]
    best = [min(d[v] for v in fixed) for d in dist]
    chosen = []
    for _ in range(SLOTS - FIRST_ART):
        total, pick = None, None
        for v in range(64):
            if v in fixed or v in chosen:
                continue
            t = sum(n * min(b, d[v]) for n, b, d in zip(counts, best, dist))
            if total is None or t < total:
                total, pick = t, v
        chosen.append(pick)
        best = [min(b, d[pick]) for b, d in zip(best, dist)]
    return list(fixed) + chosen


FLAT = 24  # a pixel within this of its 4 neighbours (each channel) is "flat"


def flat_pixels(art):
    """Opaque pixels inside an area of one colour, not on the soft edges the
    sheet's anti-aliasing leaves between two colours."""
    out = []
    for y in range(H):
        for x in range(W):
            c = art[y][x]
            if c is None:
                continue
            ok = True
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                n = art[ny][nx] if 0 <= nx < W and 0 <= ny < H else None
                if n is not None and max(abs(n[i] - c[i]) for i in range(3)) > FLAT:
                    ok = False
                    break
            if ok:
                out.append((y, x))
    return out


def index_picture(art, palette):
    """Palette index (2-15) of every pixel: the nearest colour. The
    transparent background is the card body (2)."""
    cache = {}

    def nearest(c):
        if c not in cache:
            cache[c] = 2 + min(range(len(palette)), key=lambda i: (distance(c, EGA[palette[i]]), i))
        return cache[c]

    return [[2 if c is None else nearest(c) for c in row] for row in art]

def picture_sum(data):
    s1 = s2 = 0
    for i in range(0, len(data), 2):
        s1 = (s1 + data[i] + (data[i + 1] << 8)) & 0xFFFF
        s2 = (s2 + s1) & 0xFFFF
    return (s2 << 16) | s1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=pathlib.Path)
    parser.add_argument("--preview", type=pathlib.Path)
    args = parser.parse_args()
    sheets = [Sheet(p) for p in SHEETS]
    table, pictures, previews = b"", b"", []
    for name, sheet_no, index in SPRITES:
        sheet, box = sheets[sheet_no - 1], card_box(index)
        x0, y0, x1, y1 = box
        w, h = x1 - x0, y1 - y0
        blank = name == "Blank"
        edge = nearest_ega(border_colour(sheet, box))
        ry = y0 + int(h * (0.47 if blank else 0.875))
        ribbon = nearest_ega(sample(sheet, (x0 + int(w * 0.29), ry - 3, x0 + int(w * 0.33), ry + 3)))
        art = fit(illustration(sheet, box, blank))
        flat = flat_pixels(art)
        palette = choose_palette([art[y][x] for y, x in flat], [BODY, INK, edge, ribbon])
        idx = index_picture(art, palette)
        planes = bytearray()
        for plane in range(4):
            for row in idx:
                for xb in range(0, W, 8):
                    byte = 0
                    for k in range(8):
                        byte = (byte << 1) | ((row[xb + k] >> plane) & 1)
                    planes.append(byte)
        regs = bytes([0, 0] + palette)
        table += regs + struct.pack("<I", picture_sum(planes))
        pictures += planes
        if args.preview:
            previews.append((name, idx, regs))
    header = MAGIC + struct.pack("<HHHHII", VERSION, len(SPRITES), W, H, W // 8 * H * 4, zlib.crc32(table))
    args.out.write_bytes(header + table + pictures)
    if args.preview:
        # Each picture as the EGA shows it (pixels 48/35 tall), on its card colours.
        args.preview.mkdir(parents=True, exist_ok=True)
        cols, cw, chh = 5, W * 2 + 16, H * 2 * ASPECT_NUM // ASPECT_DEN + 16
        rows_n = (len(previews) + cols - 1) // cols
        canvas = [[(40, 40, 40)] * (cols * cw) for _ in range(rows_n * chh)]
        for n, (name, idx, regs) in enumerate(previews):
            ox, oy = (n % cols) * cw + 8, (n // cols) * chh + 8
            for y in range(chh - 16):
                src = idx[y * ASPECT_DEN // (2 * ASPECT_NUM)]
                for x in range(W * 2):
                    canvas[oy + y][ox + x] = EGA[regs[src[x // 2]]]
        write_png(args.preview / "meals_preview.png", cols * cw, rows_n * chh,
                  [bytes(v for c in row for v in c) for row in canvas])


if __name__ == "__main__":
    main()
