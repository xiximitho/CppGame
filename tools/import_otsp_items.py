#!/usr/bin/env python3
"""Paste static equipment icons from otsp_equipment_01 into the atlas.

    python3 tools/import_otsp_items.py

Reads a fixed catalogue of (item_id, sheet_col, sheet_row) cells, colour-keys
magenta to alpha, packs them into a fresh band of atlas.png, and rewrites the
matching `item <id> ...` lines in atlas.txt. Safe to re-run: the band is reused
when its geometry matches.
"""
from __future__ import annotations

import os
import struct
import sys
import zlib

# Reuse the creature importer's PNG helpers by importing the module path.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from import_otsp import (  # noqa: E402
    KEY,
    blit_cell,
    next_pow2,
    read_png,
    write_png,
)

# item_id -> (col, row) on otsp_equipment_01.png (32px cells).
# Picked by eye from the sheet; see docs/vocations.md for the class kits.
ITEMS = [
    # Knight
    (311, 9, 8, "knight sword"),
    (312, 6, 24, "steel shield"),
    (313, 1, 27, "steel helmet"),
    (314, 3, 36, "plate armor"),
    (315, 5, 39, "plate legs"),
    (316, 7, 41, "steel boots"),
    # Paladin
    (317, 0, 18, "wooden bow"),
    (318, 12, 33, "ranger tunic"),
    (319, 0, 40, "leather legs"),
    (320, 10, 41, "leather boots"),
    (321, 0, 45, "cross amulet"),
    (322, 5, 47, "quiver"),
    # Mage
    (323, 4, 21, "blue wand"),
    (324, 11, 33, "mage robe"),
    (325, 12, 30, "wizard hat"),
    (326, 2, 41, "blue boots"),
    (327, 7, 46, "sapphire ring"),
    # Druid
    (328, 6, 21, "nature staff"),
    (329, 13, 33, "leather armor"),
    (330, 8, 30, "wolf hood"),
    (331, 0, 41, "green boots"),
    (332, 7, 45, "moon amulet"),
    # Shared consumables / extras
    (333, 12, 43, "health potion"),
    (334, 13, 43, "mana potion"),
    (335, 10, 8, "longsword"),  # nicer default sword sprite upgrade target
    (336, 5, 24, "round shield"),
    (337, 3, 18, "sturdy bow"),
    (338, 0, 21, "wooden staff"),
]


def upsert_item_line(text: str, item_id: int, x: int, y: int, w: int, h: int) -> str:
    line = f"item        {item_id}     {x}   {y}  {w}  {h}\n"
    out = []
    replaced = False
    for raw in text.splitlines(keepends=True):
        parts = raw.split()
        if len(parts) >= 2 and parts[0] == "item" and parts[1].isdigit() and int(parts[1]) == item_id:
            out.append(line)
            replaced = True
        else:
            out.append(raw)
    if not replaced:
        if out and not out[-1].endswith("\n"):
            out[-1] += "\n"
        out.append(line)
    return "".join(out)


def main() -> int:
    sheet = os.path.join(ROOT, "assets/tibia_like/otsp_equipment_01.png")
    png_path = os.path.join(ROOT, "assets/tilesets/atlas.png")
    txt_path = os.path.join(ROOT, "assets/tilesets/atlas.txt")

    cell = 32
    sheet_w, sheet_h, sheet_px = read_png(sheet)
    atlas_w, atlas_h, atlas = read_png(png_path)
    meta = open(txt_path, encoding="utf-8").read()

    n = len(ITEMS)
    cols = 8
    rows = (n + cols - 1) // cols
    band_w = cols * cell
    band_h = rows * cell
    # Place below existing content, cell-aligned.
    dst_y = ((atlas_h + cell - 1) // cell) * cell
    # Reuse band if a previous import already owns these ids at the same place.
    reuse = False
    for item_id, *_rest in ITEMS:
        for raw in meta.splitlines():
            parts = raw.split()
            if len(parts) >= 6 and parts[0] == "item" and parts[1] == str(item_id):
                x, y = int(parts[2]), int(parts[3])
                if y >= 512:  # previous OTSP item band threshold
                    dst_y = (y // cell) * cell
                    reuse = True
                break
        if reuse:
            break

    new_w = max(atlas_w, next_pow2(band_w))
    new_h = max(atlas_h, dst_y + band_h)
    print(f"importing {n} icons -> atlas band ({0},{dst_y}) {band_w}x{band_h}"
          f"{' (reuse)' if reuse else ''}")
    if (new_w, new_h) != (atlas_w, atlas_h):
        print(f"atlas grows {atlas_w}x{atlas_h} -> {new_w}x{new_h}")
        grown = bytearray(new_w * new_h * 4)
        for y in range(atlas_h):
            src = y * atlas_w * 4
            dst = y * new_w * 4
            grown[dst:dst + atlas_w * 4] = atlas[src:src + atlas_w * 4]
        atlas, atlas_w, atlas_h = grown, new_w, new_h

    for i, (item_id, col, row, name) in enumerate(ITEMS):
        sx, sy = col * cell, row * cell
        if sx + cell > sheet_w or sy + cell > sheet_h:
            raise SystemExit(f"{name} ({col},{row}) outside sheet")
        dx = (i % cols) * cell
        dy = dst_y + (i // cols) * cell
        opaque = blit_cell(atlas, atlas_w, dx, dy, sheet_px, sheet_w, sx, sy,
                           cell, cell)
        if opaque == 0:
            raise SystemExit(f"{name} at ({col},{row}) is fully transparent")
        meta = upsert_item_line(meta, item_id, dx, dy, cell, cell)
        print(f"  {item_id} {name:16s} sheet {col},{row} -> atlas {dx},{dy}")

    write_png(png_path, atlas_w, atlas_h, atlas)
    open(txt_path, "w", encoding="utf-8").write(meta)
    print(f"wrote {png_path} and {txt_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
