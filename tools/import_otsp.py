#!/usr/bin/env python3
"""Imports one creature's animation from a Tibia-style sprite sheet into the atlas.

    python3 tools/import_otsp.py --sheet assets/tibia_like/otsp_creatures_03.png \
        --at 0,47 --cell 32x32 --dirs 4 --frames 3 --appearance 2

What it does, in order: cuts `dirs * frames` cells out of the sheet, turns the
magenta colour key into real transparency, pastes them as ONE ROW into a fresh band
of assets/tilesets/atlas.png, and writes (or replaces) the matching `mobstrip` line
in assets/tilesets/atlas.txt. After that the client and the editor both show the
animation with no rebuild — atlas.png and atlas.txt are read at startup.

Three things about the source sheets that this encodes so nobody has to rediscover
them (docs/animation.md has the measurements):

  * transparency is the colour key #FF00FF, because the sheets have no alpha channel;
  * a creature is 4 directions x 3 frames, DIRECTION-MAJOR, in the sheet's reading
    order — which is also the order a `mobstrip` line expects, so the run is copied
    straight across;
  * 32px creatures are packed along rows (`--order rows`, the default) and the 64px
    ones down columns (`--order cols`). Neither is universal in the pack, which is
    why the block is given as a start cell and a count instead of being detected.

Why no Pillow: it is not installed everywhere (it is not a build dependency, and
tools/gen_placeholder_atlas.py fails on a fresh machine because of it), and reading
and writing a PNG needs nothing but zlib from the standard library.

Re-importing the same appearance with the same geometry reuses the band it already
has, so it is safe to run repeatedly while picking the right creature — the atlas
does not grow by 12 cells every attempt.
"""
import argparse
import os
import struct
import sys
import zlib

KEY = (255, 0, 255)  # the sheets' colour key

# From client/iso.hpp: a sprite's origin is the offset from the tile's TOP VERTEX,
# so feet land on the tile centre at (-w/2, kHalfTileHeight - h). The mob bands in
# atlas.txt already follow this: 24x24 -> (-12, -8), 32x48 -> (-16, -32).
HALF_TILE_HEIGHT = 16


def _unfilter(raw, width, height, bpp):
    """Reverses the PNG scanline filters. Returns one bytearray of `bpp`-byte pixels."""
    stride = width * bpp
    out = bytearray(height * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        kind = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                b = prev[i]
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif kind != 0:
            raise ValueError(f"unsupported PNG filter {kind}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return out


def read_png(path):
    """(width, height, RGBA bytearray). Handles the 8-bit RGB and RGBA cases, which
    is what both the sheets and the atlas are; anything else is a hard error rather
    than a silent conversion."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    idat = bytearray()
    width = height = depth = colour = None
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", chunk[:10])
            if chunk[12] != 0:
                raise ValueError(f"{path}: interlaced PNGs are not supported")
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    if depth != 8 or colour not in (2, 6):
        raise ValueError(f"{path}: expected 8-bit RGB or RGBA (got depth {depth}, "
                         f"colour type {colour})")
    channels = 3 if colour == 2 else 4
    pixels = _unfilter(zlib.decompress(bytes(idat)), width, height, channels)
    if channels == 4:
        return width, height, pixels
    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        rgba[i * 4:i * 4 + 3] = pixels[i * 3:i * 3 + 3]
        rgba[i * 4 + 3] = 255
    return width, height, rgba


def write_png(path, width, height, rgba):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none. The atlas is small; smaller is not worth it.
        raw += rgba[y * stride:(y + 1) * stride]
    body = (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))
    open(path, "wb").write(body)


def blit_cell(dst, dst_w, dx, dy, src, src_w, sx, sy, w, h):
    """Copies one cell, turning the colour key into transparency."""
    kept = 0
    for row in range(h):
        for col in range(w):
            s = ((sy + row) * src_w + sx + col) * 4
            pixel = (src[s], src[s + 1], src[s + 2])
            d = ((dy + row) * dst_w + dx + col) * 4
            if pixel == KEY:
                dst[d:d + 4] = b"\x00\x00\x00\x00"
            else:
                dst[d:d + 3] = src[s:s + 3]
                dst[d + 3] = 255
                kept += 1
    return kept


def next_pow2(value):
    result = 1
    while result < value:
        result *= 2
    return result


def find_strip_line(text, appearance):
    """The existing `mobstrip` line for `appearance` as a field list, or None.
    Matched on parsed fields, the way src/editor/atlas_meta.cpp does it, so spacing
    in a hand-edited file does not decide whether the line is found."""
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) >= 10 and fields[0] == "mobstrip":
            try:
                if int(fields[1]) == appearance:
                    return [int(float(f)) for f in fields[2:10]]
            except ValueError:
                continue
    return None


def upsert_strip_line(text, appearance, x, y, cell_w, cell_h, dirs, frames, ox, oy,
                      tilt):
    line = ("mobstrip    %-3d %-4d %-4d %-3d %-3d %-2d %-2d %-4d %-4d %d" %
            (appearance, x, y, cell_w, cell_h, dirs, frames, ox, oy, tilt))
    out = []
    replaced = False
    for existing in text.splitlines():
        fields = existing.split()
        mine = (len(fields) >= 2 and fields[0] in ("mob", "mobstrip") and
                fields[1].isdigit() and int(fields[1]) == appearance)
        if mine and fields[0] == "mobstrip":
            out.append(line)
            replaced = True
        elif mine:
            # Drop the old one-line-per-direction binding for this appearance. Two
            # kinds for the same appearance parse in file order, so leaving them
            # would make which art wins depend on where in the file they sit.
            continue
        else:
            out.append(existing)
    if not replaced:
        out.append(line)
    return "\n".join(out) + "\n", line


def parse_pair(text, sep, what):
    try:
        a, b = text.replace(sep, ",").split(",")
        return int(a), int(b)
    except ValueError:
        raise SystemExit(f"cannot parse {what} '{text}'")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sheet", required=True, help="source sprite sheet PNG")
    ap.add_argument("--at", required=True, metavar="COL,ROW",
                    help="first cell of the creature's block, in cells")
    ap.add_argument("--cell", default="32x32", metavar="WxH", help="cell size in px")
    ap.add_argument("--dirs", type=int, default=4, choices=(4, 8))
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--appearance", type=int, required=True,
                    help="sim appearance id (see assets/monsters.txt)")
    ap.add_argument("--order", choices=("rows", "cols"), default="rows",
                    help="how the block is packed in the sheet")
    ap.add_argument("--dir-order", default="2,3,0,1", metavar="LIST",
                    help="art direction of each block in the sheet, in sheet order. "
                         "The default says the sheets run front, left, back, right "
                         "while the atlas runs back, right, front, left (art "
                         "direction 0 is away from the camera — see "
                         "client/animation.hpp). Set by eye on the creatures in "
                         "assets/tibia_like: front and back are unmistakable, left "
                         "and right are a coin toss on a creature with no face, so "
                         "this is the knob to flip when a mob walks sideways wrong.")
    ap.add_argument("--assets", default="assets", help="asset root to write into")
    ap.add_argument("--tilt", type=int, default=30, metavar="DEG",
                    help="clockwise lean in degrees, about the sprite's feet "
                         "(default 30). These sheets draw creatures for a world whose "
                         "grid is axis-aligned on screen; on an isometric diamond they "
                         "land looking like they fell over, and ~30 stands them up. "
                         "Pass 0 for art that is already upright, or tune it by eye in "
                         "the editor's F4 preview.")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would happen and write nothing")
    opt = ap.parse_args(argv)

    cell_w, cell_h = parse_pair(opt.cell, "x", "--cell")
    start_col, start_row = parse_pair(opt.at, ",", "--at")
    count = opt.dirs * opt.frames

    dir_order = [int(v) for v in opt.dir_order.split(",")]
    if sorted(dir_order) != list(range(opt.dirs)):
        raise SystemExit(f"--dir-order must be a permutation of 0..{opt.dirs - 1}")

    sheet_w, sheet_h, sheet = read_png(opt.sheet)
    per_row = sheet_w // cell_w

    # Where each cell of the atlas strip comes from in the sheet. The strip is always
    # written in the atlas' own direction order, so the parser stays a straight
    # index = dir * frames + frame and nothing downstream knows a sheet had a
    # different idea about which way is away from the camera.
    cells = [None] * count
    for block in range(opt.dirs):
        for frame in range(opt.frames):
            i = block * opt.frames + frame
            if opt.order == "rows":
                index = (start_row * per_row) + start_col + i
                col, row = index % per_row, index // per_row
            else:
                col, row = start_col, start_row + i
            if (col + 1) * cell_w > sheet_w or (row + 1) * cell_h > sheet_h:
                raise SystemExit(f"cell {col},{row} is outside {opt.sheet}")
            cells[dir_order[block] * opt.frames + frame] = (col * cell_w,
                                                            row * cell_h)

    png_path = os.path.join(opt.assets, "tilesets", "atlas.png")
    txt_path = os.path.join(opt.assets, "tilesets", "atlas.txt")
    atlas_w, atlas_h, atlas = read_png(png_path)
    meta = open(txt_path, "r", encoding="utf-8").read()

    strip_w = count * cell_w
    existing = find_strip_line(meta, opt.appearance)
    reuse = (existing is not None and existing[2] == cell_w and
             existing[3] == cell_h and existing[4] == opt.dirs and
             existing[5] == opt.frames and existing[0] + strip_w <= atlas_w and
             existing[1] + cell_h <= atlas_h)

    if reuse:
        dst_x, dst_y = existing[0], existing[1]
        new_w, new_h = atlas_w, atlas_h
    else:
        # A new band at the bottom, ALIGNED to a multiple of cell_h: the editor's
        # picker lays a cell grid over the whole sheet, so a band starting at y=440
        # with 32px cells is a band no click can ever land on. Costs at most cell_h-1
        # rows of transparent atlas.
        dst_x = 0
        dst_y = ((atlas_h + cell_h - 1) // cell_h) * cell_h
        # Width grows to a power of two because a 12-cell strip of 32px cells is 384px
        # and the atlas had been 256 wide since it was generated art; powers of two
        # also keep uv coordinates exact.
        new_w = max(atlas_w, next_pow2(strip_w))
        new_h = dst_y + cell_h

    origin_x = -cell_w // 2
    origin_y = HALF_TILE_HEIGHT - cell_h

    print(f"{opt.sheet}: {count} cells of {cell_w}x{cell_h} from "
          f"{start_col},{start_row} ({opt.order}) -> atlas {dst_x},{dst_y}")
    if not reuse and (new_w, new_h) != (atlas_w, atlas_h):
        print(f"atlas grows {atlas_w}x{atlas_h} -> {new_w}x{new_h}")
    if opt.dry_run:
        _, line = upsert_strip_line(meta, opt.appearance, dst_x, dst_y, cell_w,
                                    cell_h, opt.dirs, opt.frames, origin_x, origin_y,
                                    opt.tilt)
        print(f"would write: {line}")
        return 0

    if (new_w, new_h) != (atlas_w, atlas_h):
        grown = bytearray(new_w * new_h * 4)
        for y in range(atlas_h):
            src = y * atlas_w * 4
            dst = y * new_w * 4
            grown[dst:dst + atlas_w * 4] = atlas[src:src + atlas_w * 4]
        atlas, atlas_w, atlas_h = grown, new_w, new_h

    opaque = 0
    for i, (sx, sy) in enumerate(cells):
        opaque += blit_cell(atlas, atlas_w, dst_x + i * cell_w, dst_y, sheet,
                            sheet_w, sx, sy, cell_w, cell_h)
    if opaque == 0:
        # Every cell was pure colour key: the block is in the wrong place, and a
        # silently invisible mob is exactly the bug this tool should not ship.
        raise SystemExit("all cells are fully transparent — wrong --at or --cell?")

    write_png(png_path, atlas_w, atlas_h, atlas)
    meta, line = upsert_strip_line(meta, opt.appearance, dst_x, dst_y, cell_w, cell_h,
                                   opt.dirs, opt.frames, origin_x, origin_y, opt.tilt)
    open(txt_path, "w", encoding="utf-8").write(meta)
    print(f"wrote {png_path} and {txt_path}")
    print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
