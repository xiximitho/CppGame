#!/usr/bin/env python3
"""Adds a two-way portal to a map .txt that its generator can no longer produce.

    python3 tools/add_portal.py assets/maps/dungeon.txt

Both assets/maps/dungeon.txt and assets/maps/vila.txt were edited in game_editor
after being generated (vila even gained a second floor and its stairs), so running
gen_maps.py over them destroys real work — see docs/maps.md. This applies the same
rule gen_maps.py uses, reading the finished file instead of the grid in memory:

  * the near mouth is the closest open tile at least 5 tiles from the spawn,
  * the far mouth is the open tile farthest from it on the TOP floor,
  * both need their four orthogonal neighbours walkable, so a mouth never walls off
    a 1-wide corridor (stepping onto one sends you away, so what is past it is not
    reachable through it),
  * two `portal` lines, one per direction: a warp with no way back is a trap.

Blocking is read from the map's own legend against the same item ids the
simulation uses, and the marker glyph is added to the legend if the file lacks it.
Idempotent: a file that already has a `portal` line is left alone.
"""

import sys

STONE = 3
PORTAL = 105
# Ids whose ItemType carries BlocksWalk in sim::build_default_registry.
BLOCKING_IDS = {4, 100, 101, 102}
STAIR_IDS = {103, 104}


def parse(path):
    with open(path) as f:
        lines = f.read().split("\n")

    legend = {}
    size = None
    spawn_char = None
    floors = []
    header = []
    i = 0
    while i < len(lines):
        line = lines[i]
        fields = line.split()
        keyword = fields[0] if fields else ""
        if keyword == "size":
            size = (int(fields[1]), int(fields[2]), int(fields[3]))
            header.append(line)
            i += 1
        elif keyword == "legend":
            glyph = fields[1]
            ground = int(fields[2])
            obj = int(fields[3]) if len(fields) > 3 else 0
            legend[glyph] = (ground, obj)
            header.append(line)
            i += 1
        elif keyword == "spawn":
            spawn_char = fields[1]
            header.append(line)
            i += 1
        elif keyword == "floor":
            z = int(fields[1])
            i += 1
            rows = []
            for _ in range(size[1]):
                rows.append(list(lines[i].ljust(size[0])))
                i += 1
            floors.append((z, rows))
        else:
            header.append(line)
            i += 1
    return header, legend, size, spawn_char, floors


def walkable(legend, ch):
    if ch == " ":
        return False
    ground, obj = legend.get(ch, (0, 0))
    if ground == 0 and obj == 0:
        return False
    return ground not in BLOCKING_IDS and obj not in BLOCKING_IDS


def find_spawn(floors, spawn_char):
    for z, rows in floors:
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch == spawn_char:
                    return (x, y, z)
    return None


def reachable(legend, floors, spawn):
    """Flood fill across floors, taking stairs, with the strict diagonal rule."""
    grids = {z: rows for z, rows in floors}
    sx, sy, sz = spawn
    start = (sx, sy, sz, False)
    seen = {start}
    queue = [start]
    while queue:
        x, y, z, by_stair = queue.pop()
        rows = grids[z]
        ground, obj = legend.get(rows[y][x], (0, 0))
        if not by_stair and obj in STAIR_IDS:
            nz = z + (1 if obj == 103 else -1)
            if nz in grids and walkable(legend, grids[nz][y][x]):
                node = (x, y, nz, True)
                if node not in seen:
                    seen.add(node)
                    queue.append(node)
                continue
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                nx, ny = x + dx, y + dy
                if not (0 <= ny < len(rows) and 0 <= nx < len(rows[ny])):
                    continue
                if not walkable(legend, rows[ny][nx]):
                    continue
                if dx and dy and not (walkable(legend, rows[y][nx])
                                      and walkable(legend, rows[ny][x])):
                    continue
                node = (nx, ny, z, False)
                if node not in seen:
                    seen.add(node)
                    queue.append(node)
    return {(x, y, z) for x, y, z, _ in seen}


def open_tiles(legend, floors, reach, z, spawn_char):
    grids = {fz: rows for fz, rows in floors}
    rows = grids[z]
    out = []
    for (x, y, tz) in sorted(reach):
        if tz != z:
            continue
        ch = rows[y][x]
        ground, obj = legend.get(ch, (0, 0))
        if ch == spawn_char or obj in STAIR_IDS or obj == PORTAL:
            continue
        if not walkable(legend, ch):
            continue
        if any(not (0 <= y + dy < len(rows) and 0 <= x + dx < len(rows[y + dy]))
               or not walkable(legend, rows[y + dy][x + dx])
               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            continue
        out.append((x, y, tz))
    return out


def glyph_for_portal(legend, header):
    """The glyph bound to (STONE, PORTAL), adding a legend line if needed."""
    for glyph, pair in legend.items():
        if pair == (STONE, PORTAL):
            return glyph, header
    for candidate in "PWXY":
        if candidate not in legend:
            legend[candidate] = (STONE, PORTAL)
            index = max((i for i, line in enumerate(header)
                         if line.startswith("legend ")), default=0)
            header.insert(index + 1, f"legend {candidate} {STONE} {PORTAL}")
            return candidate, header
    raise SystemExit("no free glyph for the portal marker")


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    path = argv[0]
    header, legend, size, spawn_char, floors = parse(path)
    if any(line.startswith("portal ") for line in header):
        print(f"{path} already has a portal; nothing to do")
        return 0
    if spawn_char is None:
        raise SystemExit(f"{path} has no 'spawn' line to measure from")

    spawn = find_spawn(floors, spawn_char)
    if spawn is None:
        raise SystemExit(f"{path}: spawn glyph '{spawn_char}' is not in the grid")

    # The authored spawn is only a preference in game too: dungeon.txt has its '@'
    # painted over with water and wall, so the server falls back to a random
    # walkable tile (docs/maps.md). Measuring "near the spawn" from a tile nobody
    # can stand on would put both mouths wherever the flood happened to start, so
    # fall back to the middle of the LARGEST connected region.
    #
    # Largest, not merely nearest the centre: dungeon.txt has isolated one-tile
    # pockets of floor, and the tile closest to the map centre is one of them. A
    # portal anchored there would be a warp nobody can ever reach — and the flood
    # from it reports one reachable tile, which is how this was caught.
    grids = {z: rows for z, rows in floors}
    if not walkable(legend, grids[spawn[2]][spawn[1]][spawn[0]]):
        best = None
        seen = set()
        for y, row in enumerate(grids[0]):
            for x, ch in enumerate(row):
                if (x, y, 0) in seen or not walkable(legend, ch):
                    continue
                region = reachable(legend, floors, (x, y, 0))
                seen |= region
                if best is None or len(region) > len(best):
                    best = region
        if not best:
            raise SystemExit(f"{path}: no walkable tile at all")
        rows = grids[0]
        centre = (len(rows[0]) // 2, len(rows) // 2)
        spawn = min(best, key=lambda t: (abs(t[0] - centre[0])
                                         + abs(t[1] - centre[1]), t))
        print(f"{path}: authored spawn is not walkable; measuring from {spawn}, "
              f"the middle of the largest region ({len(best)} tiles)")

    reach = reachable(legend, floors, spawn)

    top = max(z for z, _ in floors)
    home = open_tiles(legend, floors, reach, spawn[2], spawn_char)
    away = open_tiles(legend, floors, reach, top, spawn_char)
    if not home or not away:
        raise SystemExit(f"{path}: no open tile for a portal mouth")

    def dist(tile):
        return abs(tile[0] - spawn[0]) + abs(tile[1] - spawn[1])

    near = min((t for t in home if dist(t) >= 5), key=dist, default=None)
    far = max(away, key=dist, default=None)
    if near is None or far is None or near == far:
        raise SystemExit(f"{path}: could not pick two distinct mouths")

    glyph, header = glyph_for_portal(legend, header)
    grids = {z: rows for z, rows in floors}
    for x, y, z in (near, far):
        grids[z][y][x] = glyph

    lines = list(header)
    while lines and not lines[-1].strip():   # the split's trailing empty line
        lines.pop()
    lines.append(f"portal {near[0]} {near[1]} {near[2]} "
                 f"{far[0]} {far[1]} {far[2]}")
    lines.append(f"portal {far[0]} {far[1]} {far[2]} "
                 f"{near[0]} {near[1]} {near[2]}")
    for z, rows in floors:
        lines.append(f"floor {z}")
        lines += ["".join(row).rstrip() for row in rows]

    with open(path, "w") as f:
        f.write("\n".join(line for line in lines if line is not None) + "\n")
    print(f"{path}: portal {near} <-> {far} (spawn {spawn}, top floor {top})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
