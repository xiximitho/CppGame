#!/usr/bin/env python3
"""Generates a placeholder sprite atlas PNG + binding metadata for CppGame.

Layout mirrors src/client/src/tileset.cpp so the existing id->sprite bindings
line up. This is throwaway tooling: the real pipeline (docs/content.md) bakes an
atlas from SQLite. Run:  python3 gen_atlas.py <out_dir>
"""
import math
import sys
from PIL import Image

ATLAS = 256
TW, TH = 64, 32          # tile diamond
HALF_W, HALF_H = 32, 16

GROUND_Y, HL_Y, BLOCK_Y, ACTOR_Y = 0, 32, 64, 128
AFW, AFH = 32, 48


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c) + (255,)


def rgba(c):
    return tuple(c) + (255,)


def diamond_span(row, w, h):
    half = h // 2
    step = w // half
    mirrored = row if row < half else h - 1 - row
    return step * (mirrored + 1)


def fill_diamond(px, ox, oy, w, h, fill, edge):
    for row in range(h):
        span = diamond_span(row, w, h)
        start = (w - span) // 2
        for col in range(start, start + span):
            on_edge = col == start or col == start + span - 1
            px[ox + col, oy + row] = edge if on_edge else fill


def speckle(px, ox, oy, w, h, color, seed, count):
    r = seed
    for _ in range(count):
        r = (r * 1103515245 + 12345) & 0x7FFFFFFF
        row = 1 + r % (h - 2)
        span = diamond_span(row, w, h)
        if span <= 2:
            continue
        start = (w - span) // 2
        r = (r * 1103515245 + 12345) & 0x7FFFFFFF
        col = start + 1 + r % max(1, span - 2)
        px[ox + col, oy + row] = color


def fill_block(px, ox, oy, top, left, right, wall_h):
    for col in range(TW):
        dist = abs(col - (TW - 1) * 0.5)
        face_top = TH - (dist / (TW * 0.5)) * TH * 0.5
        start = int(face_top)
        face = left if col < TW // 2 else right
        for row in range(start, start + wall_h):
            if oy + row < ATLAS:
                px[ox + col, oy + row] = face
    fill_diamond(px, ox, oy, TW, TH, top, shade(top[:3], 0.8))


def fill_rect(px, x, y, w, h, c):
    for j in range(h):
        for i in range(w):
            px[x + i, y + j] = c


def fill_circle(px, cx, cy, r, c):
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy <= r * r:
                px[cx + dx, cy + dy] = c


def fill_shadow(px, cx, cy, rx, ry):
    for dy in range(-ry, ry + 1):
        for dx in range(-rx, rx + 1):
            nx, ny = dx / rx, dy / ry
            if nx * nx + ny * ny <= 1.0:
                px[cx + dx, cy + dy] = (0, 0, 0, 70)


def diamond_outline(px, ox, oy, w, h, c):
    for row in range(h):
        span = diamond_span(row, w, h)
        start = (w - span) // 2
        for col in (start, start + span - 1, start + 1, start + span - 2):
            px[ox + col, oy + row] = c


DIRDELTA = [(0, -1), (1, -1), (1, 0), (1, 1),
            (0, 1), (-1, 1), (-1, 0), (-1, -1)]


def draw_actor(px, ox, oy, facing):
    # Grimhold knight: steel body, dark-red tabard, gold accent, broad shoulders
    # (pauldrons dominate the read from above). Palette from the design briefing.
    steel = rgba((143, 148, 156)); steel_lt = rgba((163, 168, 175))
    steel_dk = rgba((91, 96, 104)); helm = rgba((123, 131, 139))
    tabard = rgba((122, 38, 33)); tabard_lt = rgba((156, 53, 44))
    leather = rgba((91, 69, 48)); visor = rgba((61, 65, 71))
    gold = rgba((201, 162, 39)); gold_lt = rgba((231, 196, 85))
    outline = rgba((13, 11, 10))
    cx = ox + AFW // 2
    feet_y = oy + AFH - 2
    fill_shadow(px, cx, feet_y - 2, 11, 3)
    # legs
    fill_rect(px, cx - 5, feet_y - 13, 4, 12, steel_dk)
    fill_rect(px, cx + 1, feet_y - 13, 4, 12, steel_dk)
    # torso (breastplate) + dark-red tabard strip down the centre
    fill_rect(px, cx - 7, feet_y - 27, 14, 15, steel)
    fill_rect(px, cx - 2, feet_y - 27, 4, 15, tabard)
    fill_rect(px, cx - 2, feet_y - 27, 4, 3, tabard_lt)
    fill_rect(px, cx - 7, feet_y - 15, 14, 2, leather)   # belt
    # broad pauldrons — wider than the torso, the top-down silhouette cue
    fill_rect(px, cx - 9, feet_y - 28, 18, 4, steel_lt)
    # helm with a dark visor slit
    fill_circle(px, cx, feet_y - 33, 5, helm)
    fill_rect(px, cx - 3, feet_y - 33, 6, 2, visor)
    # 1px-ish outline touches so it reads against grass at 1x
    for oyp in range(feet_y - 31, feet_y - 1):
        px[cx - 8, oyp] = outline if px[cx - 8, oyp][3] == 0 else px[cx - 8, oyp]
    dx, dy = DIRDELTA[facing]
    sx, sy = (dx - dy), (dx + dy) * 0.5   # 2:1 iso screen direction
    length = math.hypot(sx, sy)
    if length > 0:                        # gold crest marks the facing
        mx = cx + round(sx / length * 6)
        my = feet_y - 33 + round(sy / length * 6)
        fill_circle(px, mx, my, 2, gold)
        px[mx, my] = gold_lt


def main(out_dir):
    img = Image.new("RGBA", (ATLAS, ATLAS), (0, 0, 0, 0))
    px = img.load()

    # Grimhold palette: muted moss grass, warm brown dirt, warm-grey stone
    # cobbles, deep blue water with a cyan glint.
    grounds = [((111, 174, 79), 1.14, 11, 60),   # grass  id 1  #6fae4f
               ((91, 69, 48), 1.20, 22, 45),     # dirt   id 2  #5b4530
               ((107, 99, 90), 1.12, 33, 45),    # stone  id 3  warm grey
               ((47, 109, 148), 1.35, 44, 30)]   # water  id 4  deep blue
    for slot, (c, sf, seed, n) in enumerate(grounds):
        ox = slot * TW
        fill_diamond(px, ox, GROUND_Y, TW, TH, rgba(c), shade(c, 0.85))
        speckle(px, ox, GROUND_Y, TW, TH, shade(c, sf), seed, n)

    diamond_outline(px, 0, HL_Y, TW, TH, (231, 196, 85, 230))   # gold cursor

    # stone wall: warm grey, Grimhold cobble tone
    fill_block(px, 0, BLOCK_Y, rgba((125, 116, 106)),
               rgba((59, 54, 48)), rgba((74, 68, 60)), 32)
    tx = TW
    fill_rect(px, tx + 29, BLOCK_Y + 30, 6, 18, rgba((70, 52, 31)))
    fill_circle(px, tx + 32, BLOCK_Y + 24, 14, rgba((62, 107, 52)))
    fill_circle(px, tx + 23, BLOCK_Y + 30, 9, rgba((50, 90, 44)))
    fill_circle(px, tx + 41, BLOCK_Y + 30, 9, rgba((74, 125, 62)))
    fill_circle(px, tx + 30, BLOCK_Y + 19, 7, rgba((90, 140, 74)))

    # crate  id 102  -- worked example of binding a NEW object (see docs/sprites.md)
    cx = 2 * TW
    fill_block(px, cx, BLOCK_Y, rgba((124, 96, 62)),
               rgba((58, 44, 28)), rgba((91, 69, 48)), 32)
    for gx in (cx + 16, cx + 32, cx + 48):          # vertical plank seams
        for gy in range(BLOCK_Y + 20, BLOCK_Y + 60):
            if 0 <= gx < ATLAS and gy < ATLAS and px[gx, gy][3]:
                px[gx, gy] = rgba((36, 31, 24))

    for i in range(8):
        draw_actor(px, i * AFW, ACTOR_Y, i)

    # solid white swatch for UI fills (editor menu backgrounds and frames)
    for yy in range(40, 48):
        for xx in range(232, 240):
            px[xx, yy] = (255, 255, 255, 255)

    img.save(f"{out_dir}/atlas.png")

    meta = [
        "# atlas.txt - binds a sprite region to a sim TileId (see docs/content.md)",
        "# kind      id/dir  x    y    w   h   origin_x origin_y",
        "ground      1       0    0    64  32  -32      0",
        "ground      2       64   0    64  32  -32      0",
        "ground      3       128  0    64  32  -32      0",
        "ground      4       192  0    64  32  -32      0",
        "object      100     0    64   64  64  -32      -32",
        "object      101     64   64   64  64  -32      -32",
        "object      102     128  64   64  64  -32      -32",
        "highlight           0    32   64  32  -32      0",
        "solid               234  42   4   4",
    ]
    for i in range(8):
        meta.append(f"actor       {i}       {i*AFW:<4} 128  32  48  -16      -32")
    with open(f"{out_dir}/atlas.txt", "w") as f:
        f.write("\n".join(meta) + "\n")
    print("wrote", out_dir + "/atlas.png and atlas.txt")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
