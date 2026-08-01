#!/usr/bin/env python3
"""Generates a placeholder sprite atlas PNG + binding metadata for CppGame.

Layout mirrors src/client/src/tileset.cpp so the existing id->sprite bindings
line up. This is throwaway tooling: the real pipeline (docs/content.md) bakes an
atlas from SQLite.

    python3 gen_placeholder_atlas.py <out_dir>          # whole atlas from scratch
    python3 gen_placeholder_atlas.py --patch <out_dir>  # only the bands missing

The committed assets/tilesets/atlas.png is NOT this script's output any more (it
was edited afterwards), so regenerating it over the top would throw that art
away. `--patch` exists for that: it grows the existing PNG to the current canvas
size, draws only into the newly added band, and adds the missing atlas.txt lines.
Old pixels are never touched.
"""
import math
import os
import sys
from PIL import Image

ATLAS = 256      # width, and the height of everything that predates the stairs
ATLAS_H = 440    # grew twice: a 64px stair band, then one band per mob class
TW, TH = 64, 32          # tile diamond
HALF_W, HALF_H = 32, 16

GROUND_Y, HL_Y, BLOCK_Y, ACTOR_Y = 0, 32, 64, 128
AFW, AFH = 32, 48

# Stair band. The object row at y=64 had one free 64x64 slot and stairs need two,
# so the atlas grew instead of the pair being squeezed in — see docs/sprites.md.
STAIR_Y = 256
STAIR_IDS = (103, 104)  # up, down; sim::tiles::kStairsUp / kStairsDown

# The warp mouth goes in the third slot of the stair band, which was empty: the
# band is 64 tall and the row fits four 64x64 cells. Same walkable-object shape as
# a stair, so it belongs beside them rather than in a band of its own.
PORTAL_X, PORTAL_Y = 128, STAIR_Y
PORTAL_ID = 105          # sim::tiles::kPortal

# Monster bands: one row of 8 directions per class. Cell size is PER CLASS, not
# fixed at the player's 32x48 — a rat in a knight-sized cell leaves its health bar
# floating half a tile above it, because the bar hangs off the sprite's origin. The
# origin_y of each cell is chosen so every class's feet land on the tile centre.
# Appearance ids match sim::kAppearanceRat / Skeleton / Ogre; 0 is the player.
MOB_Y = 320
MOB_BANDS = (
    # appearance, cell w, cell h, origin_x, origin_y
    (1, 24, 24, -12, -8),    # rat: small and close to the ground
    (2, 32, 48, -16, -32),   # skeleton: player-sized
    (3, 32, 48, -16, -32),   # ogre: fills the cell
)

# --- bitmap font ------------------------------------------------------------
# The atlas had one free band left (y=224..248 -- items end at 192, effects at
# 224), and 95 printable ASCII glyphs in a 6x8 cell fit it exactly: 42 per row,
# three rows, 24px tall. That is why the font lives here and not somewhere
# tidier, and why growing the glyph set means growing the atlas.
#
# Glyphs are 5x7 drawn inside a 6x8 cell, so the 6th column and 8th row are the
# spacing -- the renderer samples the whole cell and advances by its width, with
# no kerning table anywhere. To fix or add a glyph, edit its 7 rows below: '#'
# is a lit pixel, anything else is transparent. Order IS ASCII order starting at
# FONT_FIRST; the C++ side indexes by `ascii - first`, so do not reorder.
FONT_Y = 224
FONT_CELL_W, FONT_CELL_H = 6, 8
FONT_GLYPH_W, FONT_GLYPH_H = 5, 7
FONT_PER_ROW = 42
FONT_FIRST = 32

FONT = [
    "...../...../...../...../...../...../.....",  # 32 space
    "..#../..#../..#../..#../..#../...../..#..",  # 33 !
    ".#.#./.#.#./...../...../...../...../.....",  # 34 "
    ".#.#./.#.#./#####/.#.#./#####/.#.#./.#.#.",  # 35 #
    "..#../.####/#.#../.###./..#.#/####./..#..",  # 36 $
    "##..#/##.#./...#./..#../.#.../#.##./#..##",  # 37 %
    ".##../#..#./#.#../.#.../#.#.#/#..#./.##.#",  # 38 &
    "..#../..#../...../...../...../...../.....",  # 39 '
    "...#./..#../.#.../.#.../.#.../..#../...#.",  # 40 (
    ".#.../..#../...#./...#./...#./..#../.#...",  # 41 )
    "...../..#../#.#.#/.###./#.#.#/..#../.....",  # 42 *
    "...../..#../..#../#####/..#../..#../.....",  # 43 +
    "...../...../...../...../..##./..#../.#...",  # 44 ,
    "...../...../...../#####/...../...../.....",  # 45 -
    "...../...../...../...../...../.##../.##..",  # 46 .
    "....#/...#./...#./..#../.#.../.#.../#....",  # 47 /
    ".###./#...#/#..##/#.#.#/##..#/#...#/.###.",  # 48 0
    "..#../.##../..#../..#../..#../..#../.###.",  # 49 1
    ".###./#...#/....#/...#./..#../.#.../#####",  # 50 2
    "#####/...#./..#../...#./....#/#...#/.###.",  # 51 3
    "...#./..##./.#.#./#..#./#####/...#./...#.",  # 52 4
    "#####/#..../####./....#/....#/#...#/.###.",  # 53 5
    "..##./.#.../#..../####./#...#/#...#/.###.",  # 54 6
    "#####/....#/...#./..#../.#.../.#.../.#...",  # 55 7
    ".###./#...#/#...#/.###./#...#/#...#/.###.",  # 56 8
    ".###./#...#/#...#/.####/....#/...#./.##..",  # 57 9
    "...../.##../.##../...../.##../.##../.....",  # 58 :
    "...../.##../.##../...../.##../..#../.#...",  # 59 ;
    "...#./..#../.#.../#..../.#.../..#../...#.",  # 60 <
    "...../...../#####/...../#####/...../.....",  # 61 =
    ".#.../..#../...#./....#/...#./..#../.#...",  # 62 >
    ".###./#...#/....#/...#./..#../...../..#..",  # 63 ?
    ".###./#...#/....#/.##.#/#.#.#/#.#.#/.####",  # 64 @
    "..#../.#.#./#...#/#...#/#####/#...#/#...#",  # 65 A
    "####./#...#/#...#/####./#...#/#...#/####.",  # 66 B
    ".###./#...#/#..../#..../#..../#...#/.###.",  # 67 C
    "###../#..#./#...#/#...#/#...#/#..#./###..",  # 68 D
    "#####/#..../#..../####./#..../#..../#####",  # 69 E
    "#####/#..../#..../####./#..../#..../#....",  # 70 F
    ".###./#...#/#..../#.###/#...#/#...#/.###.",  # 71 G
    "#...#/#...#/#...#/#####/#...#/#...#/#...#",  # 72 H
    ".###./..#../..#../..#../..#../..#../.###.",  # 73 I
    "....#/....#/....#/....#/#...#/#...#/.###.",  # 74 J
    "#...#/#..#./#.#../##.../#.#../#..#./#...#",  # 75 K
    "#..../#..../#..../#..../#..../#..../#####",  # 76 L
    "#...#/##.##/#.#.#/#.#.#/#...#/#...#/#...#",  # 77 M
    "#...#/#...#/##..#/#.#.#/#..##/#...#/#...#",  # 78 N
    ".###./#...#/#...#/#...#/#...#/#...#/.###.",  # 79 O
    "####./#...#/#...#/####./#..../#..../#....",  # 80 P
    ".###./#...#/#...#/#...#/#.#.#/#..#./.##.#",  # 81 Q
    "####./#...#/#...#/####./#.#../#..#./#...#",  # 82 R
    ".###./#...#/#..../.###./....#/#...#/.###.",  # 83 S
    "#####/..#../..#../..#../..#../..#../..#..",  # 84 T
    "#...#/#...#/#...#/#...#/#...#/#...#/.###.",  # 85 U
    "#...#/#...#/#...#/#...#/#...#/.#.#./..#..",  # 86 V
    "#...#/#...#/#...#/#.#.#/#.#.#/##.##/#...#",  # 87 W
    "#...#/#...#/.#.#./..#../.#.#./#...#/#...#",  # 88 X
    "#...#/#...#/.#.#./..#../..#../..#../..#..",  # 89 Y
    "#####/....#/...#./..#../.#.../#..../#####",  # 90 Z
    ".###./.#.../.#.../.#.../.#.../.#.../.###.",  # 91 [
    "#..../.#.../.#.../..#../...#./...#./....#",  # 92 \
    ".###./...#./...#./...#./...#./...#./.###.",  # 93 ]
    "..#../.#.#./#...#/...../...../...../.....",  # 94 ^
    "...../...../...../...../...../...../#####",  # 95 _
    ".#.../..#../...../...../...../...../.....",  # 96 `
    "...../...../.###./....#/.####/#...#/.####",  # 97 a
    "#..../#..../####./#...#/#...#/#...#/####.",  # 98 b
    "...../...../.###./#..../#..../#...#/.###.",  # 99 c
    "....#/....#/.####/#...#/#...#/#...#/.####",  # 100 d
    "...../...../.###./#...#/#####/#..../.###.",  # 101 e
    "..##./.#.../###../.#.../.#.../.#.../.#...",  # 102 f
    "...../...../.####/#...#/.####/....#/####.",  # 103 g (tail sweeps left, not a 9)
    "#..../#..../####./#...#/#...#/#...#/#...#",  # 104 h
    "..#../...../..#../..#../..#../..#../..#..",  # 105 i
    "...#./...../...#./...#./...#./#..#./.##..",  # 106 j
    "#..../#..../#..#./#.#../##.../#.#../#..#.",  # 107 k
    ".##../..#../..#../..#../..#../..#../.###.",  # 108 l
    "...../...../##.#./#.#.#/#.#.#/#.#.#/#.#.#",  # 109 m
    "...../...../####./#...#/#...#/#...#/#...#",  # 110 n
    "...../...../.###./#...#/#...#/#...#/.###.",  # 111 o
    "...../...../####./#...#/#...#/####./#....",  # 112 p
    "...../...../.####/#...#/#...#/.####/....#",  # 113 q
    "...../...../#.##./##.../#..../#..../#....",  # 114 r
    "...../...../.###./#..../.###./....#/.###.",  # 115 s
    ".#.../.#.../###../.#.../.#.../.#.../..##.",  # 116 t
    "...../...../#...#/#...#/#...#/#...#/.####",  # 117 u
    "...../...../#...#/#...#/#...#/.#.#./..#..",  # 118 v
    "...../...../#...#/#.#.#/#.#.#/#.#.#/.#.#.",  # 119 w
    "...../...../#...#/.#.#./..#../.#.#./#...#",  # 120 x
    "...../...../#...#/#...#/.####/....#/.###.",  # 121 y
    "...../...../#####/...#./..#../.#.../#####",  # 122 z
    "..##./..#../..#../.#.../..#../..#../..##.",  # 123 {
    "..#../..#../..#../..#../..#../..#../..#..",  # 124 |
    ".##../..#../..#../...#./..#../..#../.##..",  # 125 }
    "...../...../.#..#/#.#.#/#..#./...../.....",  # 126 ~
]
assert len(FONT) == 127 - FONT_FIRST, f"font table has {len(FONT)} glyphs"


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
    # Clipped: a sprite whose shadow reaches past the last band would otherwise
    # take the whole generator down with an IndexError.
    for dy in range(-ry, ry + 1):
        for dx in range(-rx, rx + 1):
            nx, ny = dx / rx, dy / ry
            if nx * nx + ny * ny <= 1.0 and 0 <= cx + dx < ATLAS \
                    and 0 <= cy + dy < ATLAS_H:
                px[cx + dx, cy + dy] = (0, 0, 0, 70)


def diamond_outline(px, ox, oy, w, h, c):
    for row in range(h):
        span = diamond_span(row, w, h)
        start = (w - span) // 2
        for col in (start, start + span - 1, start + 1, start + span - 2):
            px[ox + col, oy + row] = c


def draw_font(px):
    """Blits the glyph table into the font band, in ASCII order."""
    for index, glyph in enumerate(FONT):
        rows = glyph.split("/")
        assert len(rows) == FONT_GLYPH_H, f"glyph {index} has {len(rows)} rows"
        cell_x = (index % FONT_PER_ROW) * FONT_CELL_W
        cell_y = FONT_Y + (index // FONT_PER_ROW) * FONT_CELL_H
        for row_i, row in enumerate(rows):
            assert len(row) == FONT_GLYPH_W, f"glyph {index} row {row_i}"
            for col_i, cell in enumerate(row):
                if cell == "#":
                    px[cell_x + col_i, cell_y + row_i] = (255, 255, 255, 255)


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


def draw_stairs_up(px, ox, oy):
    """Stairs up, as a stepped platform seen from above.

    Three slabs, each narrower and 7px higher than the one below, which in 2:1
    projection reads as rising toward the back of the tile. It has to be legible
    at 1x and it must not overflow the 64x64 cell, hence the small offsets.
    """
    top = (125, 116, 106)   # the wall block's stone, so a stair matches masonry
    for i, (w, h, lift) in enumerate(((TW, TH, 0), (52, 26, 7), (40, 20, 14),
                                      (28, 14, 21))):
        x = ox + (TW - w) // 2
        # Extruded down to the base before its top face is drawn: without the
        # side, the slabs read as plates floating over the tile instead of as
        # steps cut from one block.
        for drop in range(lift, 0, -1):
            fill_diamond(px, x, oy + TH - lift + drop, w, h, shade(top, 0.55),
                         shade(top, 0.48))
        fill_diamond(px, x, oy + TH - lift, w, h, shade(top, 1.0 + 0.07 * i),
                     shade(top, 0.62))


def draw_stairs_down(px, ox, oy):
    """Stairs down, as a pit with steps sinking into the tile.

    Nested diamonds getting darker inward. The darkness is the whole read: a hole
    is the one thing a flat tile can say without leaving its own diamond.
    """
    top = (125, 116, 106)
    fill_diamond(px, ox, oy + TH, TW, TH, rgba(top), shade(top, 0.62))
    for i, (w, h, sink) in enumerate(((48, 24, 4), (32, 16, 8), (16, 8, 12))):
        fill = shade(top, 0.60 - 0.16 * i)
        fill_diamond(px, ox + (TW - w) // 2, oy + TH + sink, w, h, fill,
                     shade(top, 0.45))


def draw_portal(px, ox, oy):
    """A warp mouth: masonry rim around a violet gulf with a cyan spiral.

    Deliberately NOT the stair-down pit, which is this same nest of diamonds in
    stone greys. A pit reads as "lower" and a portal has to read as "somewhere
    else", so the ramp goes violet instead of grey and gets the one spiral in the
    tileset. It is the only floor tile with a colour nothing else uses, which is
    the point: an author must be able to spot one across the map.
    """
    rim = (125, 116, 106)   # the wall block's stone, like the stairs
    fill_diamond(px, ox, oy + TH, TW, TH, rgba(rim), shade(rim, 0.62))

    # Nested diamonds, centred rather than sinking: a gulf, not a staircase.
    gulf = ((92, 62, 148), (66, 42, 116), (38, 24, 74), (16, 10, 34))
    for c, (w, h) in zip(gulf, ((52, 26), (40, 20), (28, 14), (16, 8))):
        fill_diamond(px, ox + (TW - w) // 2, oy + TH + (TH - h) // 2, w, h,
                     rgba(c), shade(c, 0.72))

    # The spiral. y radius is halved because everything here is 2:1 — a circular
    # swirl would read as standing upright instead of lying on the floor.
    glow = (120, 220, 235)
    cx, cy = ox + HALF_W, oy + TH + HALF_H
    for step in range(26):
        angle = step * 0.48
        radius = 2.0 + step * 0.55
        x = cx + int(round(math.cos(angle) * radius))
        y = cy + int(round(math.sin(angle) * radius * 0.5))
        px[x, y] = shade(glow, 1.0 - 0.015 * step)
    fill_circle(px, cx, cy, 1, rgba((238, 248, 255)))


def draw_stairs(px):
    draw_stairs_up(px, 0, STAIR_Y)
    draw_stairs_down(px, TW, STAIR_Y)
    draw_portal(px, PORTAL_X, PORTAL_Y)


def facing_marker(facing, reach=3):
    """Screen-space offset of the 'front' of a sprite, for the facing cue.

    The same 2:1 conversion draw_actor uses: grid direction -> screen direction, so
    a mob looking north-west looks UP on screen like everything else. `reach` is
    small on purpose — a head shifted five pixels stops looking attached.
    """
    dx, dy = DIRDELTA[facing]
    sx, sy = (dx - dy), (dx + dy) * 0.5
    length = math.hypot(sx, sy)
    if length == 0:
        return 0, 0
    return round(sx / length * reach), round(sy / length * reach)


def draw_rat(px, ox, oy, facing, cell_h=24):
    """Giant rat: low, long, brown. Reads as 'small and fast' by silhouette.

    Sits in the bottom third of the 32x48 cell on purpose — height in the cell is
    the only cue this projection gives for how big something is, and a rat drawn
    at knight height stops being a rat.
    """
    body = rgba((104, 82, 64)); back = rgba((124, 100, 78))
    belly = rgba((142, 120, 96)); ear = rgba((150, 112, 112))
    eye = rgba((214, 74, 60)); tail = rgba((146, 124, 104))
    cx = ox + 12
    feet_y = oy + cell_h - 3
    fill_shadow(px, cx, feet_y, 8, 3)
    mx, my = facing_marker(facing, 3)
    # Tail: a solid run of pixels trailing straight out behind it, stepped one at a
    # time so it never breaks into dashes.
    tx, ty = float(cx), float(feet_y - 5)
    for _ in range(9):
        tx -= mx / 3.0
        ty -= my / 3.0
        px[round(tx), round(ty)] = tail
    fill_circle(px, cx, feet_y - 6, 6, body)          # haunches
    fill_circle(px, cx + mx, feet_y - 7, 5, back)     # shoulders
    fill_rect(px, cx - 4, feet_y - 4, 9, 3, belly)
    hx, hy = cx + mx + mx // 2, feet_y - 8 + my       # head
    fill_circle(px, hx, hy, 3, back)
    px[hx - 1, hy - 3] = ear
    px[hx + 1, hy - 3] = ear
    px[hx + (1 if mx >= 0 else -1), hy] = eye


def draw_skeleton(px, ox, oy, facing, cell_h=AFH):
    """Skeleton: bone white, thin, dark sockets. The player's height, half its mass."""
    bone = rgba((222, 218, 198)); bone_dk = rgba((166, 160, 140))
    socket = rgba((28, 26, 24)); rust = rgba((96, 74, 52))
    cx = ox + AFW // 2
    feet_y = oy + cell_h - 2
    fill_shadow(px, cx, feet_y - 2, 8, 3)
    fill_rect(px, cx - 4, feet_y - 12, 3, 11, bone)   # legs
    fill_rect(px, cx + 2, feet_y - 12, 3, 11, bone)
    fill_rect(px, cx - 4, feet_y - 25, 9, 13, bone_dk)  # ribcage
    for ry in range(feet_y - 24, feet_y - 14, 3):       # ribs
        fill_rect(px, cx - 4, ry, 9, 1, bone)
    fill_rect(px, cx - 7, feet_y - 25, 15, 2, bone)     # collarbone
    fill_rect(px, cx - 8, feet_y - 24, 2, 10, bone)     # arms
    fill_rect(px, cx + 7, feet_y - 24, 2, 10, bone)
    fill_circle(px, cx, feet_y - 30, 4, bone)           # skull
    mx, my = facing_marker(facing, 2)
    px[cx + mx - 1, feet_y - 30 + my] = socket          # eye sockets face forward
    px[cx + mx + 1, feet_y - 30 + my] = socket
    px[cx + mx, feet_y - 27 + my] = rust                # jaw


def draw_ogre(px, ox, oy, facing, cell_h=AFH):
    """Ogre: fills the cell, green-grey, tiny head, club. Slow and heavy by shape."""
    hide = rgba((104, 126, 84)); hide_lt = rgba((126, 148, 100))
    hide_dk = rgba((72, 90, 60)); gut = rgba((140, 156, 116))
    wood = rgba((92, 68, 44)); eye = rgba((236, 226, 140))
    cx = ox + AFW // 2
    feet_y = oy + cell_h - 4       # kept inside the cell, shadow included
    fill_shadow(px, cx, feet_y + 1, 13, 3)
    fill_rect(px, cx - 7, feet_y - 14, 6, 13, hide_dk)   # legs
    fill_rect(px, cx + 2, feet_y - 14, 6, 13, hide_dk)
    fill_rect(px, cx - 9, feet_y - 32, 18, 19, hide)     # torso
    fill_circle(px, cx, feet_y - 18, 7, gut)             # belly
    fill_rect(px, cx - 12, feet_y - 33, 24, 6, hide_lt)  # shoulders, very wide
    mx, my = facing_marker(facing, 2)
    fill_circle(px, cx + mx, feet_y - 36 + my, 4, hide_lt)  # small head
    px[cx + mx - 1, feet_y - 36 + my] = eye
    px[cx + mx + 2, feet_y - 36 + my] = eye
    # club, held on the side it is facing
    club_x = cx + (10 if mx >= 0 else -11)
    fill_rect(px, club_x, feet_y - 30, 3, 16, wood)
    fill_circle(px, club_x + 1, feet_y - 32, 4, wood)


MOB_DRAWERS = (draw_rat, draw_skeleton, draw_ogre)


def mob_band_y(index):
    """Top of a class's band: bands are stacked by their own heights."""
    return MOB_Y + sum(MOB_BANDS[i][2] for i in range(index))


def draw_mobs(px):
    for index, (_, cw, ch, _, _) in enumerate(MOB_BANDS):
        y = mob_band_y(index)
        for facing in range(8):
            MOB_DRAWERS[index](px, facing * cw, y, facing, ch)


def mob_meta_lines():
    lines = []
    for index, (appearance, cw, ch, ox, oy) in enumerate(MOB_BANDS):
        y = mob_band_y(index)
        for facing in range(8):
            lines.append(f"mob         {appearance}  {facing}  {facing*cw:<4} "
                         f"{y}  {cw}  {ch}  {ox}      {oy}")
    return lines


def stair_meta_lines():
    return [f"object      {STAIR_IDS[0]}     0    {STAIR_Y}  64  64  -32      -32",
            f"object      {STAIR_IDS[1]}     64   {STAIR_Y}  64  64  -32      -32",
            f"object      {PORTAL_ID}     {PORTAL_X}  {PORTAL_Y}  64  64  "
            f"-32      -32"]


def already_bound(text, line):
    """Whether atlas.txt already binds what `line` binds.

    Compared on the identifying fields only (kind + id, kind + appearance + dir for
    a mob), never on the whole line: a region someone moved by hand must not be
    quietly re-added at the old coordinates.

    A `mobstrip` line counts as binding every `mob <appearance> <dir>` of that
    appearance. The two kinds must not coexist for one appearance — atlas.txt is
    read in order and the last binding wins — so re-adding the eight static lines
    of an animated class silently un-animates it. Not hypothetical: it is what this
    function did before this check existed, and the symptom is a mob that stops
    walking while still moving.
    """
    fields = line.split()
    existing = [row.split() for row in text.splitlines() if row.strip()]
    if fields[0] == "mob" and any(row[:2] == ["mobstrip", fields[1]]
                                  for row in existing if len(row) >= 2):
        return True
    key = fields[:3] if fields[0] == "mob" else fields[:2]
    return any(row[:len(key)] == key for row in existing)


def patch(out_dir):
    """Adds what the existing atlas is missing, touching nothing that is there.

    Used because the committed art diverged from this script: regenerating would
    silently replace hand-made pixels, and a tool that destroys art to add a
    sprite is a tool nobody runs twice.
    """
    png_path = os.path.join(out_dir, "atlas.png")
    txt_path = os.path.join(out_dir, "atlas.txt")

    existing = Image.open(png_path).convert("RGBA")
    if existing.height >= ATLAS_H:
        print(f"{png_path} is already {existing.width}x{existing.height}")
        img = existing
    else:
        # NEVER narrower than what is already there. tools/import_otsp.py widens the
        # atlas to fit a 12-cell animation strip (384px does not go into 256), and a
        # canvas of ATLAS width would crop every imported creature off the right-hand
        # side — the exact "tool that destroys art" this function exists to avoid.
        width = max(existing.width, ATLAS)
        img = Image.new("RGBA", (width, ATLAS_H), (0, 0, 0, 0))
        img.paste(existing, (0, 0))
        print(f"grew {png_path} to {width}x{ATLAS_H}")

    px = img.load()
    draw_stairs(px)
    draw_mobs(px)
    img.save(png_path)

    with open(txt_path) as f:
        text = f.read()
    wanted = stair_meta_lines() + mob_meta_lines()
    added = [line for line in wanted if not already_bound(text, line)]
    if added:
        if not text.endswith("\n"):
            text += "\n"
        text += "\n".join(added) + "\n"
        with open(txt_path, "w") as f:
            f.write(text)
    print(f"patched {png_path} and added {len(added)} line(s) to {txt_path}")


def main(out_dir):
    img = Image.new("RGBA", (ATLAS, ATLAS_H), (0, 0, 0, 0))
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

    def rct(x0, y0, w, h, c):
        for yy in range(y0, y0 + h):
            for xx in range(x0, x0 + w):
                if 0 <= xx < ATLAS and 0 <= yy < ATLAS:
                    px[xx, yy] = c

    # --- inventory item icons, 16x16 at y=176 (ids 300..308) ---
    IY = 176

    def cell(slot):
        return slot * 16

    steel = rgba((198, 203, 210)); dk = rgba((120, 124, 132))
    wood = rgba((124, 92, 52)); gold = rgba((214, 170, 60))
    s = cell(0)  # 300 sword
    rct(s + 7, IY + 2, 2, 9, steel); rct(s + 5, IY + 10, 6, 2, wood)
    s = cell(1)  # 301 bow
    fill_circle(px, s + 4, IY + 8, 6, wood)
    for yy in range(IY + 2, IY + 15):
        px[s + 4, yy] = (0, 0, 0, 0)
        px[s + 3, yy] = (0, 0, 0, 0)
    rct(s + 9, IY + 2, 1, 13, rgba((210, 210, 210)))  # string
    s = cell(2)  # 302 shield
    rct(s + 3, IY + 2, 10, 9, rgba((70, 110, 180))); rct(s + 6, IY + 11, 4, 3, rgba((70, 110, 180)))
    s = cell(3)  # 303 helmet
    fill_circle(px, s + 8, IY + 8, 5, dk); rct(s + 3, IY + 8, 11, 3, dk)
    s = cell(4)  # 304 armor (body)
    rct(s + 4, IY + 3, 8, 10, steel); rct(s + 2, IY + 3, 3, 4, dk); rct(s + 11, IY + 3, 3, 4, dk)
    s = cell(5)  # 305 legs
    rct(s + 4, IY + 3, 3, 11, dk); rct(s + 9, IY + 3, 3, 11, dk)
    s = cell(6)  # 306 boots
    rct(s + 3, IY + 8, 4, 6, wood); rct(s + 9, IY + 8, 4, 6, wood)
    s = cell(7)  # 307 ring
    fill_circle(px, s + 8, IY + 9, 5, gold); fill_circle(px, s + 8, IY + 9, 3, (0, 0, 0, 0))
    s = cell(8)  # 308 amulet
    px_line = rgba((180, 150, 60))
    for i in range(6):
        px[s + 3 + i, IY + 3 + i] = px_line
        px[s + 13 - i, IY + 3 + i] = px_line
    fill_circle(px, s + 8, IY + 11, 3, gold)

    # --- attack effects at y=200 ---
    # effect 1: melee glow (24x24 radial), effect 2: ranged shot (8x8 dot)
    for dy in range(-11, 12):
        for dx in range(-11, 12):
            d = (dx * dx + dy * dy) ** 0.5
            if d <= 11:
                a = int(max(0, 255 * (1.0 - d / 11.0)))
                px[12 + dx, 200 + 12 + dy] = (255, 236, 150, a)
    fill_circle(px, 24 + 4, 200 + 4, 3, rgba((235, 245, 255)))
    fill_circle(px, 24 + 4, 200 + 4, 2, rgba((120, 200, 255)))

    draw_stairs(px)
    draw_mobs(px)

    # Glyphs are drawn white so the renderer can tint them per call.
    draw_font(px)
    assert FONT_Y + ((len(FONT) - 1) // FONT_PER_ROW + 1) * FONT_CELL_H <= ATLAS

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
        *stair_meta_lines(),
        *mob_meta_lines(),
        "highlight           0    32   64  32  -32      0",
        "solid               234  42   4   4",
        "item        300     0    176  16  16",
        "item        301     16   176  16  16",
        "item        302     32   176  16  16",
        "item        303     48   176  16  16",
        "item        304     64   176  16  16",
        "item        305     80   176  16  16",
        "item        306     96   176  16  16",
        "item        307     112  176  16  16",
        "item        308     128  176  16  16",
        "effect      1       0    200  24  24",
        "effect      2       24   200  8   8",
        "# font: first_ascii count x y cell_w cell_h per_row",
        f"font        {FONT_FIRST}      {len(FONT)}   0    {FONT_Y}  "
        f"{FONT_CELL_W}   {FONT_CELL_H}   {FONT_PER_ROW}",
    ]
    for i in range(8):
        meta.append(f"actor       {i}       {i*AFW:<4} 128  32  48  -16      -32")
    with open(f"{out_dir}/atlas.txt", "w") as f:
        f.write("\n".join(meta) + "\n")
    print("wrote", out_dir + "/atlas.png and atlas.txt")


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--patch":
        patch(args[1] if len(args) > 1 else ".")
    else:
        main(args[0] if args else ".")
