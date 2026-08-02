#!/usr/bin/env python3
"""Replace placeholder ground/objects/player outfit with OTSP (Tibia-like) art.

    python3 tools/import_otsp_world.py

Overwrites the ground/object/actor/outfit regions in assets/tilesets/atlas.png
and rewrites the matching lines in atlas.txt. Equipment bands and mobstrips are
left alone. Sheets live in assets/tibia_like/ (CC BY 4.0 — see CREDITS.md).
"""
from __future__ import annotations

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from import_otsp import KEY, next_pow2, read_png, write_png  # noqa: E402

CELL = 32
ACTOR_W, ACTOR_H = 32, 48


def get_cell(sheet_w: int, sheet_px: bytearray, col: int, row: int) -> bytearray:
    """32x32 RGBA cell with magenta keyed out."""
    out = bytearray(CELL * CELL * 4)
    for y in range(CELL):
        for x in range(CELL):
            sx = col * CELL + x
            sy = row * CELL + y
            si = (sy * sheet_w + sx) * 4
            r, g, b, a = sheet_px[si : si + 4]
            if (r, g, b) == KEY:
                r = g = b = a = 0
            di = (y * CELL + x) * 4
            out[di : di + 4] = bytes((r, g, b, a))
    return out


def scale2x(src: bytearray, w: int, h: int) -> tuple[bytearray, int, int]:
    nw, nh = w * 2, h * 2
    out = bytearray(nw * nh * 4)
    for y in range(h):
        for x in range(w):
            si = (y * w + x) * 4
            pix = src[si : si + 4]
            for dy in range(2):
                for dx in range(2):
                    di = ((y * 2 + dy) * nw + (x * 2 + dx)) * 4
                    out[di : di + 4] = pix
    return out, nw, nh


def diamond_span(row: int, width: int, height: int) -> int:
    half = height // 2
    step = width // half
    mirrored = row if row < half else height - 1 - row
    return step * (mirrored + 1)


def to_ground_diamond(cell: bytearray) -> bytearray:
    """32x32 flat OTSP tile → 64x32 isometric diamond (2x, masked)."""
    scaled, sw, sh = scale2x(cell, CELL, CELL)
    out_w, out_h = 64, 32
    out = bytearray(out_w * out_h * 4)
    for y in range(out_h):
        span = diamond_span(y, out_w, out_h)
        start = (out_w - span) // 2
        for x in range(start, start + span):
            # Sample the middle band of the scaled square so texture fills the diamond.
            sx = x
            sy = y + 16
            if 0 <= sx < sw and 0 <= sy < sh:
                si = (sy * sw + sx) * 4
                di = (y * out_w + x) * 4
                out[di : di + 4] = scaled[si : si + 4]
    return out


def paste(atlas: bytearray, aw: int, ah: int, x0: int, y0: int,
          src: bytearray, sw: int, sh: int) -> None:
    for y in range(sh):
        for x in range(sw):
            dx, dy = x0 + x, y0 + y
            if dx < 0 or dy < 0 or dx >= aw or dy >= ah:
                continue
            si = (y * sw + x) * 4
            if src[si + 3] == 0:
                continue
            di = (dy * aw + dx) * 4
            atlas[di : di + 4] = src[si : si + 4]


def clear_rect(atlas: bytearray, aw: int, x0: int, y0: int, w: int, h: int) -> None:
    for y in range(h):
        for x in range(w):
            di = ((y0 + y) * aw + (x0 + x)) * 4
            atlas[di : di + 4] = b"\x00\x00\x00\x00"


def grow_atlas(atlas: bytearray, aw: int, ah: int, need_w: int, need_h: int):
    new_w = max(aw, next_pow2(need_w) if need_w > aw else aw)
    new_h = max(ah, need_h)
    if new_w == aw and new_h == ah:
        return atlas, aw, ah
    grown = bytearray(new_w * new_h * 4)
    for y in range(ah):
        for x in range(aw):
            si = (y * aw + x) * 4
            di = (y * new_w + x) * 4
            grown[di : di + 4] = atlas[si : si + 4]
    return grown, new_w, new_h


def shade(rgb: tuple[int, int, int], factor: float) -> tuple[int, int, int, int]:
    return (
        max(0, min(255, int(rgb[0] * factor))),
        max(0, min(255, int(rgb[1] * factor))),
        max(0, min(255, int(rgb[2] * factor))),
        255,
    )


def draw_iso_cube() -> bytearray:
    """Simple 64x64 isometric stone block (top + left + right faces)."""
    w, h, wall_h = 64, 32, 32
    out = bytearray(64 * 64 * 4)
    top = (168, 168, 176)
    left = (110, 110, 120)
    right = (140, 140, 150)

    def put(x: int, y: int, rgba: tuple[int, int, int, int]) -> None:
        if 0 <= x < 64 and 0 <= y < 64:
            di = (y * 64 + x) * 4
            out[di : di + 4] = bytes(rgba)

    for col in range(w):
        distance = abs(col - (w - 1) * 0.5)
        face_top = h - (distance / (w * 0.5)) * h * 0.5
        start = int(face_top)
        fr = left if col < w // 2 else right
        for row in range(start, start + wall_h):
            put(col, row, (*fr, 255))

    edge = shade(top, 0.8)
    for row in range(h):
        span = diamond_span(row, w, h)
        start = (w - span) // 2
        for col in range(start, start + span):
            on_edge = col == start or col == start + span - 1
            put(col, row, edge if on_edge else (*top, 255))
    return out


def pad_actor_cell(cell32: bytearray) -> bytearray:
    """32x32 sprite → 32x48 actor cell (feet toward the bottom)."""
    frame = bytearray(ACTOR_W * ACTOR_H * 4)
    paste(frame, ACTOR_W, ACTOR_H, 0, 16, cell32, CELL, CELL)
    return frame


def composite_cell(sheet_w: int, sheet: bytearray, row: int) -> bytearray:
    """Base body for one walk frame.

    Col 2 on this outfit template is NOT trim — it is leftover primary-mask
    pixels (pure yellow/red/…) that showed up as a cut-off yellow speck on the
    player. Skip it; cloth colour comes from the outfitstrip masks.
    """
    return get_cell(sheet_w, sheet, 1, row)


def color_dist(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


def extract_mask_layer(mask: bytearray, w: int, h: int,
                       target: tuple[int, int, int], tol: int = 80) -> bytearray:
    """Pixels near `target` become opaque white (for tinting)."""
    out = bytearray(w * h * 4)
    for i in range(w * h):
        si = i * 4
        if mask[si + 3] == 0:
            continue
        rgb = (mask[si], mask[si + 1], mask[si + 2])
        if color_dist(rgb, target) <= tol:
            out[si : si + 4] = b"\xff\xff\xff\xff"
    return out


def upsert_line(text: str, kind_prefix: str, match_pred, new_line: str) -> str:
    out = []
    replaced = False
    for raw in text.splitlines(keepends=True):
        parts = raw.split()
        if parts and match_pred(parts):
            out.append(new_line if new_line.endswith("\n") else new_line + "\n")
            replaced = True
        else:
            out.append(raw)
    if not replaced:
        if out and not out[-1].endswith("\n"):
            out[-1] += "\n"
        out.append(new_line if new_line.endswith("\n") else new_line + "\n")
    return "".join(out)


def main() -> int:
    tiles_path = os.path.join(ROOT, "assets/tibia_like/otsp_tiles_01.png")
    nature_path = os.path.join(ROOT, "assets/tibia_like/otsp_nature_01.png")
    walls_path = os.path.join(ROOT, "assets/tibia_like/otsp_walls_01.png")
    town_path = os.path.join(ROOT, "assets/tibia_like/otsp_town_01.png")
    creat_path = os.path.join(ROOT, "assets/tibia_like/otsp_creatures_03.png")
    png_path = os.path.join(ROOT, "assets/tilesets/atlas.png")
    txt_path = os.path.join(ROOT, "assets/tilesets/atlas.txt")

    for p in (tiles_path, nature_path, walls_path, town_path, creat_path, png_path):
        if not os.path.isfile(p):
            print(f"missing {p}", file=sys.stderr)
            return 1

    tw, th, tiles = read_png(tiles_path)
    nw, nh, nature = read_png(nature_path)
    ww, wh, walls = read_png(walls_path)
    tow, toh, town = read_png(town_path)
    cw, ch, creat = read_png(creat_path)
    aw, ah, atlas = read_png(png_path)
    meta = open(txt_path, encoding="utf-8").read()

    # Band for world art replacements — keep clear of equipment (y>=512) and
    # outfit (y>=640). Reuse the original placeholder slots where possible.
    # Ground stays at y=0 (4x 64x32). Objects at y=64. Actors at y=128.
    # Outfit layers stay at y=640 but are redrawn from the colour mask.

    # --- Ground: grass / dirt / stone / water ---
    # (col, row) on otsp_tiles_01
    grounds = [
        (1, 10, 5),   # grass — mottled OTSP turf
        (2, 7, 8),    # dirt
        (3, 8, 17),   # stone pavement
        (4, 4, 49),   # water
    ]
    for gid, col, row in grounds:
        cell = get_cell(tw, tiles, col, row)
        diamond = to_ground_diamond(cell)
        x = (gid - 1) * 64
        clear_rect(atlas, aw, x, 0, 64, 32)
        paste(atlas, aw, ah, x, 0, diamond, 64, 32)
        meta = upsert_line(
            meta, "ground",
            lambda p, g=gid: len(p) >= 2 and p[0] == "ground" and p[1] == str(g),
            f"ground      {gid}       {x}    0    64  32  -32      0\n",
        )
        print(f"ground {gid} <- tiles ({col},{row})")

    # ground 301 (portal pad / special) — reuse a mossy grass variant
    cell = get_cell(tw, tiles, 10, 5)
    diamond = to_ground_diamond(cell)
    clear_rect(atlas, aw, 192, 0, 64, 32)
    paste(atlas, aw, ah, 192, 0, diamond, 64, 32)
    meta = upsert_line(
        meta, "ground",
        lambda p: len(p) >= 2 and p[0] == "ground" and p[1] == "301",
        "ground      301     192  0    64  32  -32      0\n",
    )

    # --- Wall 100: plain isometric cube (no OTSP wall-segment mess) ---
    wall = draw_iso_cube()
    clear_rect(atlas, aw, 0, 64, 64, 64)
    paste(atlas, aw, ah, 0, 64, wall, 64, 64)
    meta = upsert_line(
        meta, "object",
        lambda p: len(p) >= 2 and p[0] == "object" and p[1] == "100",
        "object      100     0    64   64  64  -32      -32\n",
    )
    print("object 100 wall <- iso cube")

    # --- Tree 101: left tree from nature 2x2 @ (7,33) — crop left 40px, center ---
    tree_src = bytearray(64 * 64 * 4)
    for dy in range(2):
        for dx in range(2):
            part = get_cell(nw, nature, 7 + dx, 33 + dy)
            paste(tree_src, 64, 64, dx * 32, dy * 32, part, 32, 32)
    tree = bytearray(64 * 64 * 4)
    for y in range(64):
        for x in range(40):
            si = (y * 64 + x) * 4
            if tree_src[si + 3] == 0:
                continue
            dx = x + 12
            if dx >= 64:
                continue
            di = (y * 64 + dx) * 4
            tree[di : di + 4] = tree_src[si : si + 4]
    clear_rect(atlas, aw, 64, 64, 64, 64)
    paste(atlas, aw, ah, 64, 64, tree, 64, 64)
    meta = upsert_line(
        meta, "object",
        lambda p: len(p) >= 2 and p[0] == "object" and p[1] == "101",
        "object      101     64   64   64  64  -32      -32\n",
    )
    print("object 101 tree <- nature (7,33) left canopy")

    # --- Crate 102: town wooden crate ---
    crate_cell = get_cell(tow, town, 6, 1)
    crate64, _, _ = scale2x(crate_cell, CELL, CELL)
    clear_rect(atlas, aw, 128, 64, 64, 64)
    paste(atlas, aw, ah, 128, 64, crate64, 64, 64)
    meta = upsert_line(
        meta, "object",
        lambda p: len(p) >= 2 and p[0] == "object" and p[1] == "102",
        "object      102     128  64   64  64  -32      -32\n",
    )
    print("object 102 crate <- town (6,1)")

    # --- Player walk anim: 4 dirs × 3 frames (OTSP outfit template) ---
    # Sheet blocks on odd rows; atlas order is back,right,front,left.
    # Sheet block order → atlas dir via the same remap as import_otsp.py.
    sheet_blocks = [
        [1, 3, 5],
        [7, 9, 11],
        [13, 15, 17],
        [19, 21, 23],
    ]
    # Measured, not guessed. The four poses in this template are the four ISO
    # DIAGONALS, not front/left/back/right: none of them is a side profile, two
    # face the camera and two face away. Reading each block's face-skin position in
    # screen space (rotate the cell 45 degrees, look at which side of the head the
    # skin sits on) gives sheet order away-right, away-left, camera-left,
    # camera-right, while the atlas wants away-left, away-right, camera-right,
    # camera-left — mirrored in both halves. Hence [3, 2, 1, 0]: with [2, 3, 0, 1]
    # the character walked facing the opposite diagonal from the one it moved
    # along, which reads as sliding sideways.
    dir_order = [3, 2, 1, 0]
    dirs, frames = 4, 3
    strip_cells = dirs * frames  # 12
    strip_w = strip_cells * ACTOR_W  # 384
    band_y = 688
    # 1 base strip + 4 outfit strips
    need_h = band_y + 5 * ACTOR_H
    atlas, aw, ah = grow_atlas(atlas, aw, ah, max(aw, strip_w), need_h)

    layer_targets = [
        (0, (0, 0, 255)),     # feet
        (1, (0, 255, 0)),     # legs
        (2, (255, 0, 0)),     # body
        (3, (255, 255, 0)),   # head
    ]

    def pack_strip(dst_y: int, make_cell) -> None:
        clear_rect(atlas, aw, 0, dst_y, strip_w, ACTOR_H)
        for block, rows in enumerate(sheet_blocks):
            atlas_dir = dir_order[block]
            for fi, row in enumerate(rows):
                cell = pad_actor_cell(make_cell(row))
                x = (atlas_dir * frames + fi) * ACTOR_W
                paste(atlas, aw, ah, x, dst_y, cell, ACTOR_W, ACTOR_H)

    pack_strip(band_y, lambda row: composite_cell(cw, creat, row))
    meta = upsert_line(
        meta, "playerstrip",
        lambda p: len(p) >= 1 and p[0] == "playerstrip",
        f"playerstrip  0  0    {band_y}  {ACTOR_W}  {ACTOR_H}  {dirs}  {frames}"
        f"  -16  -32  30\n",
    )
    print(f"playerstrip <- creatures 4x3 @ y={band_y}")

    # Static actor[] fallback = front-facing frame 0 of the strip (atlas dir 2).
    still = pad_actor_cell(composite_cell(cw, creat, 13))
    for d in range(8):
        x = d * ACTOR_W
        clear_rect(atlas, aw, x, 128, ACTOR_W, ACTOR_H)
        paste(atlas, aw, ah, x, 128, still, ACTOR_W, ACTOR_H)
        meta = upsert_line(
            meta, "actor",
            lambda p, dd=d: len(p) >= 2 and p[0] == "actor" and p[1] == str(dd),
            f"actor       {d}       {x}    128  {ACTOR_W}  {ACTOR_H}  -16      -32\n",
        )

    for layer_id, target in layer_targets:
        dst_y = band_y + (1 + layer_id) * ACTOR_H

        def make_mask(row: int, t=target) -> bytearray:
            return extract_mask_layer(get_cell(cw, creat, 3, row), CELL, CELL, t, 90)

        pack_strip(dst_y, make_mask)
        meta = upsert_line(
            meta, "outfitstrip",
            lambda p, lid=layer_id: len(p) >= 2 and p[0] == "outfitstrip"
            and p[1] == str(lid),
            f"outfitstrip {layer_id}       0    {dst_y}  {ACTOR_W}  {ACTOR_H}  "
            f"{dirs}  {frames}  -16  -32\n",
        )
        # Keep a static outfit line (frame 0, front) for simple lookups / preview.
        still_mask = pad_actor_cell(make_mask(13))
        x = layer_id * ACTOR_W
        clear_rect(atlas, aw, x, 640, ACTOR_W, ACTOR_H)
        paste(atlas, aw, ah, x, 640, still_mask, ACTOR_W, ACTOR_H)
        meta = upsert_line(
            meta, "outfit",
            lambda p, lid=layer_id: len(p) >= 2 and p[0] == "outfit"
            and p[1] == str(lid),
            f"outfit      {layer_id}       {x}    640  {ACTOR_W}  {ACTOR_H}  "
            f"-16     -32\n",
        )
    print("outfitstrip 0..3 <- colour masks animated")

    write_png(png_path, aw, ah, atlas)
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write(meta)
    print(f"wrote {png_path} ({aw}x{ah}) and updated {txt_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
