#!/usr/bin/env python3
"""Generates the authored map set in the CppGame text map format.

Five maps, each exercising the tile palette a different way:

    floresta.txt  64x48x1  grass, a dirt road, a meandering stream with a ford
    vila.txt      48x40x1  walled compound, houses, a well, a market
    caverna.txt   56x40x1  organic cellular-automata cave, lake, dark surround
    ilha.txt      48x48x1  island ringed by water, beach, ruin, causeway islet
    torre.txt     40x40x3  courtyard plus two tower floors (multi-floor)

Everything is deterministic from a seed, so re-running reproduces the committed
files byte for byte. Run from the repository root:

    python3 tools/gen_maps.py                # writes all five to assets/maps/
    python3 tools/gen_maps.py floresta vila  # just those

Why a generator instead of hand-typed grids: blocking tiles are easy to place in
a way that seals a corridor or strands a pocket, and neither the parser nor a
screenshot notices. Every placement that could block goes through
`place_guarded`, which reverts it if it shrinks the region reachable from the
spawn, and `validate` re-checks the finished grid. The output is plain text and
stays hand-editable (and editable in game_editor) afterwards.
"""
import os
import random
import sys
from collections import deque

# Tile ids from sim/tile_ids.hpp. Ground ids and object ids share the numbering
# space of the item catalogue; blocking is derived there, not stated here.
GRASS, DIRT, STONE, WATER = 1, 2, 3, 4
WALL, TREE, CRATE = 100, 101, 102
STAIRS_UP, STAIRS_DOWN = 103, 104

# The glyph vocabulary shared by every map below, so one legend reads the same
# way in all of them. VOID is a space: no ground at all, an unwalkable hole.
VOID = " "
LEGEND = {
    ".": (GRASS, 0),
    ",": (DIRT, 0),
    ":": (STONE, 0),
    "~": (WATER, 0),
    "#": (STONE, WALL),
    "T": (GRASS, TREE),
    "o": (STONE, CRATE),
    "O": (DIRT, CRATE),
    # Roguelike glyphs for the stairs. Walkable on purpose — arriving on one is
    # what moves the actor a floor, so a blocking stair is a stair nobody uses.
    "<": (STONE, STAIRS_UP),
    ">": (STONE, STAIRS_DOWN),
    "@": (DIRT, 0),  # spawn, always on plain walkable ground
}

# Which glyphs a walker cannot enter: water blocks as ground, the rest as object.
BLOCKING = {"~", "#", "T", "o", "O", VOID}

# Where a stair glyph sends an actor that walks onto it, in floors.
STAIR_DELTA = {"<": 1, ">": -1}

# Monster classes, from sim::monsters:: — ids are a contract, same as item ids.
RAT, SKELETON, OGRE = 1, 2, 3


# --------------------------------------------------------------------------- #
# Grid helpers
# --------------------------------------------------------------------------- #


def blank(w, h, fill=VOID):
    return [[fill] * w for _ in range(h)]


def inside(g, x, y):
    return 0 <= y < len(g) and 0 <= x < len(g[0])


def put(g, x, y, ch):
    if inside(g, x, y):
        g[y][x] = ch


def fill_rect(g, x0, y0, x1, y1, ch):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(g, x, y, ch)


def stroke_rect(g, x0, y0, x1, y1, ch):
    """The outline of a rectangle — a wall ring, a fence, a shoreline."""
    for x in range(x0, x1 + 1):
        put(g, x, y0, ch)
        put(g, x, y1, ch)
    for y in range(y0, y1 + 1):
        put(g, x0, y, ch)
        put(g, x1, y, ch)


def disc(g, cx, cy, r, ch):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                put(g, x, y, ch)


def reachable(g, spawn):
    """Flood fill of walkable tiles from `spawn`, 8-connected.

    Diagonals in the simulation are strict (both adjacent orthogonals must be
    walkable), so this is checked the same way — otherwise the flood claims a
    diagonal squeeze the actual movement rules refuse.
    """
    sx, sy = spawn
    seen = set()
    if not inside(g, sx, sy) or g[sy][sx] in BLOCKING:
        return seen
    queue = deque([(sx, sy)])
    seen.add((sx, sy))
    while queue:
        x, y = queue.popleft()
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                nx, ny = x + dx, y + dy
                if (nx, ny) in seen or not inside(g, nx, ny):
                    continue
                if g[ny][nx] in BLOCKING:
                    continue
                if dx != 0 and dy != 0:  # no corner cutting, same as can_traverse
                    if g[y][nx] in BLOCKING or g[ny][x] in BLOCKING:
                        continue
                seen.add((nx, ny))
                queue.append((nx, ny))
    return seen


def reachable3(floors, spawn):
    """Same flood fill across every floor, taking stairs.

    Mirrors sim::World::apply_tile_transition: arriving on a stair glyph moves the actor to
    the same x,y one floor up or down, and only when that tile is walkable. A
    stair whose destination is rock is a dead end here exactly like it is in game.
    """
    sx, sy = spawn
    if not inside(floors[0], sx, sy) or floors[0][sy][sx] in BLOCKING:
        return set()

    # The fourth element is "got here by stair". It matters: the simulation moves
    # an actor only when it arrives WALKING, so the tile you land on -- typically
    # the matching stair of the pair -- is one you can walk off instead of being
    # sent straight back. Dropping this flag makes the flood declare every floor
    # above the first unreachable.
    start = (sx, sy, 0, False)
    seen = {start}
    queue = deque([start])
    while queue:
        x, y, z, by_stair = queue.popleft()
        g = floors[z]

        def visit(node):
            if node not in seen:
                seen.add(node)
                queue.append(node)

        delta = 0 if by_stair else STAIR_DELTA.get(g[y][x], 0)
        nz = z + delta
        if delta and 0 <= nz < len(floors) and floors[nz][y][x] not in BLOCKING:
            visit((x, y, nz, True))
            continue  # moved off this floor; it never takes a step from here

        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                nx, ny = x + dx, y + dy
                if not inside(g, nx, ny) or g[ny][nx] in BLOCKING:
                    continue
                if dx != 0 and dy != 0:
                    if g[y][nx] in BLOCKING or g[ny][x] in BLOCKING:
                        continue
                visit((nx, ny, z, False))

    return {(x, y, z) for x, y, z, _ in seen}


def walkable_count(g):
    return sum(1 for row in g for ch in row if ch not in BLOCKING)


def place_guarded(g, x, y, ch, spawn):
    """Places a blocking glyph only if it strands nothing.

    Returns True when the placement stuck. A crate in a one-tile corridor or a
    tree closing the last gap in a grove looks fine in a screenshot and quietly
    cuts the map in half; this is the cheap way to never ship that.
    """
    if not inside(g, x, y):
        return False
    before = g[y][x]
    if before in BLOCKING or (x, y) == spawn:
        return False
    reach_before = len(reachable(g, spawn))
    g[y][x] = ch
    if len(reachable(g, spawn)) != reach_before - 1:
        g[y][x] = before
        return False
    return True


def put_if(g, x, y, ch, allowed):
    """Paints only over the given glyphs — used to keep a building on dry land."""
    if inside(g, x, y) and g[y][x] in allowed:
        g[y][x] = ch


def clear_around(g, spawn, radius=1, floor=","):
    """Removes scenery boxing the spawn in.

    Terrain (water, rock walls) is left alone: a cave spawn is meant to have rock
    around it. Only the loose objects that could wedge a fresh actor between two
    trees are taken out.
    """
    sx, sy = spawn
    for y in range(sy - radius, sy + radius + 1):
        for x in range(sx - radius, sx + radius + 1):
            if inside(g, x, y) and g[y][x] in ("T", "o", "O"):
                g[y][x] = floor if g[y][x] == "O" else ("." if g[y][x] == "T" else ":")


def seal_pockets(floors, spawn):
    """Turns walkable tiles cut off from the spawn into scenery.

    The tree band and the odd wall corner leave one- or two-tile pockets behind
    the blocking tiles. They are not reachable, so they are not part of the map:
    filling them in with whatever matches locally is honest, and it means the
    walkable area of a shipped map is always exactly one connected region --
    across floors, once stairs join them.
    """
    reach = reachable3(floors, spawn)
    sealed = 0
    for z, g in enumerate(floors):
        for y, row in enumerate(g):
            for x, ch in enumerate(row):
                if ch in BLOCKING or (x, y, z) in reach:
                    continue
                g[y][x] = "#" if ch in (":", "<", ">") else "T"
                sealed += 1
    return sealed


def scatter(g, rng, ch, count, spawn, on=(".", ",", ":"), tries=4000):
    """Sprinkles `count` blocking glyphs on the given floor types."""
    h, w = len(g), len(g[0])
    placed = 0
    for _ in range(tries):
        if placed >= count:
            break
        x, y = rng.randrange(1, w - 1), rng.randrange(1, h - 1)
        if g[y][x] in on and place_guarded(g, x, y, ch, spawn):
            placed += 1
    return placed


def grove(g, rng, cx, cy, radius, density, spawn, on=(".",)):
    """A cluster of trees, thinning out toward its edge."""
    for y in range(cy - radius, cy + radius + 1):
        for x in range(cx - radius, cx + radius + 1):
            if not inside(g, x, y) or g[y][x] not in on:
                continue
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d > radius:
                continue
            if rng.random() < density * (1.0 - d / (radius + 1)):
                place_guarded(g, x, y, "T", spawn)


def border_trees(g, rng, band, spawn):
    """A thicket around the map edge: the reason the player cannot walk off.

    Not placed through place_guarded — the band is grown from the outside in, so
    it never encloses anything that was reachable before it existed.
    """
    h, w = len(g), len(g[0])
    for y in range(h):
        for x in range(w):
            edge = min(x, y, w - 1 - x, h - 1 - y)
            if edge >= band:
                continue
            if g[y][x] not in (".", ","):
                continue
            if edge == 0 or rng.random() < 1.0 - 0.30 * edge:
                g[y][x] = "T"
    _ = spawn  # kept in the signature for symmetry with the other helpers


# --------------------------------------------------------------------------- #
# floresta — grass, a road, a stream, groves
# --------------------------------------------------------------------------- #


def gen_floresta(seed=20260730):
    rng = random.Random(seed)
    W, H = 64, 48
    g = blank(W, H, ".")

    # A stream meandering north to south. Drawn before the road so the road can
    # be given a ford where the two cross.
    stream_x = 20
    stream = []
    for y in range(H):
        stream_x += rng.choice((-1, 0, 0, 1))
        stream_x = max(8, min(W - 9, stream_x))
        width = 2 + (1 if rng.random() < 0.35 else 0)
        stream.append((stream_x, width))
        for x in range(stream_x, stream_x + width):
            put(g, x, y, "~")

    # A little pond where the stream widens, halfway down.
    disc(g, stream[30][0] + 1, 30, 4, "~")

    # The road: west edge to east edge, wandering, two tiles wide.
    road_y = 26
    road = []
    for x in range(W):
        road_y += rng.choice((-1, 0, 0, 0, 1))
        road_y = max(6, min(H - 8, road_y))
        road.append(road_y)
        for y in range(road_y, road_y + 2):
            put(g, x, y, ",")

    # The ford: wherever the road overlaps the stream, dirt wins. Water blocks,
    # so without this the road would be a road to a wall of water.
    for x in range(W):
        for y in range(road[x] - 1, road[x] + 3):
            if inside(g, x, y) and g[y][x] == "~":
                g[y][x] = ","

    # A camp in a clearing, off the road, with the spawn on it.
    camp_x, camp_y = 46, 14
    disc(g, camp_x, camp_y, 5, ",")
    for x in range(camp_x, camp_x + 1):  # a short track from camp to the road
        for y in range(camp_y, road[x] + 1):
            put(g, x, y, ",")
    spawn = (camp_x, camp_y)

    border_trees(g, rng, 3, spawn)

    # Groves on the grass, avoiding road, water and clearing (they are not ".").
    for _ in range(34):
        cx, cy = rng.randrange(4, W - 4), rng.randrange(4, H - 4)
        grove(g, rng, cx, cy, rng.randint(3, 6), 0.85, spawn)
    # Loose trees between the groves, so the grass reads as woodland floor rather
    # than as a field with a few bushes in it.
    scatter(g, rng, "T", 150, spawn, on=(".",), tries=1200)

    # Camp supplies, and a few crates fallen off a cart on the road.
    for dx, dy in ((-2, -1), (-1, -2), (2, 1), (1, 2), (-3, 2)):
        place_guarded(g, camp_x + dx, camp_y + dy, "O", spawn)
    scatter(g, rng, "O", 6, spawn, on=(",",))

    put(g, *spawn, "@")
    return g, spawn, "Floresta - estrada, riacho com vau e um acampamento"


# --------------------------------------------------------------------------- #
# vila — a walled compound
# --------------------------------------------------------------------------- #


def gen_vila(seed=20260731):
    rng = random.Random(seed)
    W, H = 48, 40
    g = blank(W, H, ".")

    # Compound wall with a gate on the south and a postern on the west.
    x0, y0, x1, y1 = 5, 4, 42, 35
    fill_rect(g, x0 + 1, y0 + 1, x1 - 1, y1 - 1, ":")
    stroke_rect(g, x0, y0, x1, y1, "#")
    gate_x = 22
    for x in (gate_x, gate_x + 1):  # main gate, two tiles wide
        put(g, x, y1, ",")
    put(g, x0, 20, ",")  # postern

    # The road outside, running south from the gate to the map edge.
    for y in range(y1, H):
        for x in (gate_x, gate_x + 1):
            put(g, x, y, ",")
    for x in range(0, x0 + 1):  # and west from the postern
        put(g, x, 20, ",")

    spawn = (gate_x, y1 + 2)

    # Houses: a wall ring with a dirt interior and a doorway onto the plaza.
    # (dx, dy) of the door is chosen so every house opens toward the middle.
    houses = [
        (8, 7, 15, 13, "S"),
        (18, 7, 25, 12, "S"),
        (30, 7, 38, 14, "W"),
        (8, 24, 16, 31, "N"),
        (21, 26, 29, 32, "N"),
        (32, 22, 39, 30, "W"),
    ]
    for hx0, hy0, hx1, hy1, door in houses:
        fill_rect(g, hx0 + 1, hy0 + 1, hx1 - 1, hy1 - 1, ",")
        stroke_rect(g, hx0, hy0, hx1, hy1, "#")
        mx, my = (hx0 + hx1) // 2, (hy0 + hy1) // 2
        if door == "S":
            put(g, mx, hy1, ",")
        elif door == "N":
            put(g, mx, hy0, ",")
        elif door == "W":
            put(g, hx0, my, ",")
        else:
            put(g, hx1, my, ",")

    # The well: water ringed by stone, in the middle of the plaza.
    fill_rect(g, 22, 18, 23, 19, "~")

    # Market stalls flanking the plaza, and stores stacked against a house wall.
    for x in range(17, 29, 2):
        place_guarded(g, x, 16, "o", spawn)
        place_guarded(g, x, 22, "o", spawn)
    for y in range(9, 12):
        place_guarded(g, 28, y, "o", spawn)

    # A garden in the north-east corner of the compound: grass and fruit trees.
    fill_rect(g, 30, 16, 39, 20, ".")
    grove(g, rng, 34, 18, 4, 0.55, spawn)

    # Outside: pasture with scattered trees, thicket at the map edge.
    border_trees(g, rng, 2, spawn)
    for _ in range(9):
        cx, cy = rng.randrange(2, W - 2), rng.randrange(2, H - 2)
        grove(g, rng, cx, cy, rng.randint(2, 4), 0.7, spawn)

    put(g, *spawn, "@")
    return g, spawn, "Vila murada - portao, pracas, casas, poco e horta"


# --------------------------------------------------------------------------- #
# caverna — cellular automata
# --------------------------------------------------------------------------- #


def gen_caverna(seed=20260732):
    rng = random.Random(seed)
    W, H = 56, 40

    # Rock/floor noise, smoothed. `solid[y][x]` is rock.
    solid = [[rng.random() < 0.45 for _ in range(W)] for _ in range(H)]
    for _ in range(5):
        nxt = [[True] * W for _ in range(H)]
        for y in range(1, H - 1):
            for x in range(1, W - 1):
                n = sum(
                    solid[y + dy][x + dx]
                    for dy in (-1, 0, 1)
                    for dx in (-1, 0, 1)
                    if not (dx == 0 and dy == 0)
                )
                nxt[y][x] = n >= 4 if solid[y][x] else n >= 5
        solid = nxt

    # Components of open floor. Small ones become rock; the rest are joined by
    # carved tunnels, which is what makes this read as a cave system rather than
    # as a handful of unrelated blobs.
    seen = [[False] * W for _ in range(H)]
    comps = []
    for y in range(H):
        for x in range(W):
            if solid[y][x] or seen[y][x]:
                continue
            queue, cells = deque([(x, y)]), []
            seen[y][x] = True
            while queue:
                cx, cy = queue.popleft()
                cells.append((cx, cy))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] and not solid[ny][nx]:
                        seen[ny][nx] = True
                        queue.append((nx, ny))
            comps.append(cells)

    comps.sort(key=len, reverse=True)
    keep = [c for c in comps if len(c) >= 28]
    for c in comps:
        if c not in keep:
            for x, y in c:
                solid[y][x] = True

    def centre(cells):
        return (
            sum(x for x, _ in cells) // len(cells),
            sum(y for _, y in cells) // len(cells),
        )

    def tunnel(a, b):
        (ax, ay), (bx, by) = a, b
        for x in range(min(ax, bx), max(ax, bx) + 1):
            solid[ay][x] = False
        for y in range(min(ay, by), max(ay, by) + 1):
            solid[y][bx] = False

    for a, b in zip(keep, keep[1:]):
        tunnel(centre(a), centre(b))

    g = blank(W, H, "#")
    for y in range(H):
        for x in range(W):
            if not solid[y][x]:
                g[y][x] = ":"

    # Rock more than two tiles from any floor is not drawn at all: void reads as
    # "outside the cave" and keeps the map from being a field of wall sprites.
    floor_cells = [(x, y) for y in range(H) for x in range(W) if g[y][x] == ":"]
    near = set()
    for x, y in floor_cells:
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                near.add((x + dx, y + dy))
    for y in range(H):
        for x in range(W):
            if g[y][x] == "#" and (x, y) not in near:
                g[y][x] = VOID

    # Spawn in the biggest chamber; the lake goes as far from it as possible, so
    # it drowns a dead end instead of the middle of the cave.
    spawn = min(floor_cells, key=lambda p: (p[0] - centre(keep[0])[0]) ** 2
                + (p[1] - centre(keep[0])[1]) ** 2)
    dist = {spawn: 0}
    queue = deque([spawn])
    while queue:
        x, y = queue.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if inside(g, *n) and n not in dist and g[n[1]][n[0]] == ":":
                dist[n] = dist[(x, y)] + 1
                queue.append(n)

    far = sorted(dist, key=lambda p: dist[p], reverse=True)
    for anchor in far[:12]:
        added, frontier, budget = [], deque([anchor]), 70
        while frontier and len(added) < budget:
            x, y = frontier.popleft()
            if g[y][x] != ":" or (x, y) == spawn:
                continue
            g[y][x] = "~"
            added.append((x, y))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                frontier.append((x + dx, y + dy))
        # A lake may not cut the cave in two. Shrink it from the last tile added
        # until everything that was walkable is walkable again.
        while added and len(reachable(g, spawn)) != len(dist) - len(added):
            x, y = added.pop()
            g[y][x] = ":"
        if len(added) >= 25:
            break
        for x, y in added:  # too small to be a lake; try the next dead end
            g[y][x] = ":"

    scatter(g, rng, "o", 18, spawn, on=(":",))
    put(g, *spawn, "@")
    return g, spawn, "Caverna - galerias organicas, lago subterraneo e caixotes"


# --------------------------------------------------------------------------- #
# ilha — ringed by water
# --------------------------------------------------------------------------- #


def gen_ilha(seed=20260733):
    rng = random.Random(seed)
    W, H = 48, 48
    g = blank(W, H, "~")

    # A blob: radius per angle, wobbled by a few harmonics so the coast is not a
    # circle. Water blocks, so the coast is also the map boundary.
    cx, cy = 23, 25
    phase = [rng.uniform(0, 6.283) for _ in range(4)]
    amp = [3.1, 2.0, 1.4, 0.9]

    def radius(angle):
        import math

        r = 15.5
        for k in range(4):
            r += amp[k] * math.sin((k + 2) * angle + phase[k])
        return r

    import math

    for y in range(H):
        for x in range(W):
            dx, dy = x - cx, y - cy
            d = (dx * dx + dy * dy) ** 0.5
            r = radius(math.atan2(dy, dx))
            if d <= r - 2.2:
                g[y][x] = "."
            elif d <= r:
                g[y][x] = ","  # beach

    spawn = None
    for y in range(cy, H):  # first beach tile due south of centre
        if g[y][cx] == ",":
            spawn = (cx, y - 1)
            break

    # A causeway east to a small islet — an islet you cannot reach is scenery
    # nobody ever sees, so it gets a sandbar.
    ix, iy = 42, 20
    disc(g, ix, iy, 4, ",")
    disc(g, ix, iy, 3, ".")
    bar_x = cx
    while g[iy][bar_x] != "~":  # walk east off the main island first
        bar_x += 1
    for x in range(bar_x - 1, ix + 1):
        if g[iy][x] == "~":
            g[iy][x] = ","

    # A ruin on the north of the island: broken walls on a stone slab.
    # Painted on dry land only (put_if): a slab spilling into the sea would give
    # the coast a suspiciously straight edge right where the eye is drawn.
    land = (".", ",")
    rx0, ry0, rx1, ry1 = 17, 10, 27, 17
    for y in range(ry0, ry1 + 1):
        for x in range(rx0, rx1 + 1):
            put_if(g, x, y, ":", land)
    for x in range(rx0, rx1 + 1):
        put_if(g, x, ry0, "#", (":",))
        put_if(g, x, ry1, "#", (":",))
    for y in range(ry0, ry1 + 1):
        put_if(g, rx0, y, "#", (":",))
        put_if(g, rx1, y, "#", (":",))
    for _ in range(14):  # knock gaps in the walls
        side = rng.randrange(4)
        if side == 0:
            put_if(g, rng.randint(rx0, rx1), ry0, ":", ("#",))
        elif side == 1:
            put_if(g, rng.randint(rx0, rx1), ry1, ":", ("#",))
        elif side == 2:
            put_if(g, rx0, rng.randint(ry0, ry1), ":", ("#",))
        else:
            put_if(g, rx1, rng.randint(ry0, ry1), ":", ("#",))
    for x in (21, 22):  # a doorway that is certainly open
        put_if(g, x, ry1, ":", ("#",))

    # Groves inland, cargo washed up on the south beach.
    for _ in range(12):
        gx, gy = rng.randrange(6, W - 10), rng.randrange(6, H - 6)
        grove(g, rng, gx, gy, rng.randint(2, 5), 0.8, spawn)
    scatter(g, rng, "O", 8, spawn, on=(",",))
    scatter(g, rng, "o", 5, spawn, on=(":",))

    put(g, *spawn, "@")
    return g, spawn, "Ilha - praia, mata, ruina de pedra e um banco de areia"


# --------------------------------------------------------------------------- #
# torre — three floors
# --------------------------------------------------------------------------- #


def gen_torre(seed=20260734):
    rng = random.Random(seed)
    W, H = 40, 40
    floors = [blank(W, H, VOID) for _ in range(3)]

    # Tower footprint, the same columns on every floor: a floor plan that shifts
    # between levels would look wrong the moment stairs exist.
    tx0, ty0, tx1, ty1 = 13, 12, 26, 25

    # --- floor 0: courtyard with the tower base in it ---
    g0 = floors[0]
    fill_rect(g0, 0, 0, W - 1, H - 1, ".")
    cx0, cy0, cx1, cy1 = 4, 3, 35, 34
    fill_rect(g0, cx0 + 1, cy0 + 1, cx1 - 1, cy1 - 1, ":")
    stroke_rect(g0, cx0, cy0, cx1, cy1, "#")
    for x in (19, 20):  # courtyard gate, south
        put(g0, x, cy1, ",")
        for y in range(cy1, H):
            put(g0, x, y, ",")
    spawn = (19, cy1 + 2)

    fill_rect(g0, tx0 + 1, ty0 + 1, tx1 - 1, ty1 - 1, ":")
    stroke_rect(g0, tx0, ty0, tx1, ty1, "#")
    for x in (19, 20):  # tower door, also south
        put(g0, x, ty1, ":")

    # Lean-tos against the inside of the courtyard wall, a garden, a thicket
    # outside: the courtyard should not read as an empty stone box.
    for y in range(6, 14):
        place_guarded(g0, cx0 + 1, y, "o", spawn)
    for x in range(8, 16):
        place_guarded(g0, x, cy1 - 1, "o", spawn)
    fill_rect(g0, 28, 6, 33, 12, ".")
    grove(g0, rng, 30, 9, 3, 0.6, spawn)
    # The south half of the courtyard: a cistern, a woodpile and a second garden,
    # so the approach from the gate is not a blank stone floor.
    fill_rect(g0, 10, 29, 11, 30, "~")
    for x in range(24, 31):
        place_guarded(g0, x, 28, "o", spawn)
    place_guarded(g0, 24, 29, "o", spawn)
    fill_rect(g0, 6, 20, 11, 25, ".")
    grove(g0, rng, 8, 22, 3, 0.5, spawn)
    border_trees(g0, rng, 2, spawn)
    for _ in range(6):
        gx, gy = rng.randrange(2, W - 2), rng.randrange(2, H - 2)
        grove(g0, rng, gx, gy, rng.randint(2, 3), 0.7, spawn)
    put(g0, *spawn, "@")

    # --- the stairs ---
    # Two flights, in opposite corners, so climbing the tower means crossing each
    # floor instead of standing on one tile. Each pair is symmetric: '<' on the
    # lower floor, '>' at the same x,y above, which is what lets the player come
    # back down. Placed before the crates so nothing can be stacked onto them.
    lower, upper = (16, 15), (23, 22)

    # --- floor 1: two rooms and a landing ---
    g1 = floors[1]
    fill_rect(g1, tx0 + 1, ty0 + 1, tx1 - 1, ty1 - 1, ":")
    stroke_rect(g1, tx0, ty0, tx1, ty1, "#")
    for x in range(tx0 + 1, tx1):  # partition wall with a doorway
        put(g1, x, 19, "#")
    put(g1, 19, 19, ":")
    put(g1, 20, 19, ":")

    # --- floor 2: the roof ---
    g2 = floors[2]
    fill_rect(g2, tx0 + 1, ty0 + 1, tx1 - 1, ty1 - 1, ":")
    stroke_rect(g2, tx0, ty0, tx1, ty1, "#")

    put(g0, *lower, "<")   # ground floor, north-west corner of the tower
    put(g1, *lower, ">")   # ... lands here, and goes back down
    put(g1, *upper, "<")   # first floor, south-east corner, through the doorway
    put(g2, *upper, ">")

    # Furniture, skipping the stair tiles.
    for x, y in ((tx0 + 2, ty0 + 2), (tx1 - 2, ty0 + 2), (tx1 - 2, ty1 - 2),
                 (tx0 + 4, ty0 + 2)):
        if (x, y) not in (lower, upper):
            g1[y][x] = "o"
    for x, y in ((tx0 + 2, ty0 + 2), (tx1 - 2, ty0 + 2),
                 (tx0 + 2, ty1 - 2), (tx1 - 2, ty1 - 2)):
        if (x, y) not in (lower, upper):
            g2[y][x] = "o"

    note = ("Torre - patio no andar 0, dois andares de torre acima.\n"
            "# Escadas: '<' sobe, '>' desce. Entre pela porta sul da torre.")
    return floors, spawn, note


# --------------------------------------------------------------------------- #
# Writing and validation
# --------------------------------------------------------------------------- #


def used_glyphs(floors):
    seen = set()
    for g in floors:
        for row in g:
            seen.update(row)
    seen.discard(VOID)
    return seen


def populate(floors, rng, spawn, plan):
    """Places authored monsters and returns them as (x, y, z, class) tuples.

    `plan` is a list of (class, count, floor, region) where region is a predicate
    over (x, y). Placement is rejected on anything not walkable, on the spawn, and
    within three tiles of it — a mob standing on top of a fresh player is not
    difficulty, it is a bad first second.
    """
    placed = []
    taken = set()
    for cls, count, z, region in plan:
        g = floors[z]
        h, w = len(g), len(g[0])
        done = 0
        for _ in range(4000):
            if done >= count:
                break
            x, y = rng.randrange(1, w - 1), rng.randrange(1, h - 1)
            if g[y][x] in BLOCKING or (x, y, z) in taken or not region(x, y):
                continue
            if z == 0 and abs(x - spawn[0]) <= 3 and abs(y - spawn[1]) <= 3:
                continue
            taken.add((x, y, z))
            placed.append((x, y, z, cls))
            done += 1
    return placed


def monster_plan(name, floors, spawn):
    """One-off mobs: killed once, gone for good.

    Kept small on purpose. A population that has to stay populated is a `spawner`
    (see spawner_plan); a `monster` line is for the thing that is meant to be a
    one-time encounter — the ogre guarding the top of the tower, the skeleton
    standing in the ruin.
    """
    h, w = len(floors[0]), len(floors[0][0])
    anywhere = lambda x, y: True                      # noqa: E731
    far = lambda x, y: abs(x - spawn[0]) + abs(y - spawn[1]) > 18   # noqa: E731

    if name == "floresta":
        return [(SKELETON, 1, 0, far)]
    if name == "ilha":
        return [(SKELETON, 1, 0, far)]
    if name == "torre":
        return [(OGRE, 1, 2, anywhere)]     # the thing at the top of the tower
    _ = (h, w, anywhere, far)
    return []


def spawner_plan(name, floors, spawn):
    """Nests: what repopulates, how many, how wide, how often.

    (class, nests, max_alive, radius, respawn_seconds, floor, region). The respawn
    times are deliberately long — a nest that refills in five seconds is not a
    place you cleared, it is a faucet.
    """
    h, w = len(floors[0]), len(floors[0][0])
    anywhere = lambda x, y: True                                    # noqa: E731
    far = lambda x, y: abs(x - spawn[0]) + abs(y - spawn[1]) > 16    # noqa: E731

    if name == "floresta":
        return [(RAT, 3, 2, 3, 25, 0, anywhere),
                (SKELETON, 2, 2, 4, 45, 0, far),
                (OGRE, 1, 1, 2, 120, 0, far)]
    if name == "vila":
        outside = lambda x, y: x < 5 or x > 42 or y < 4 or y > 35    # noqa: E731
        return [(RAT, 2, 3, 3, 20, 0, lambda x, y: not outside(x, y)),
                (SKELETON, 2, 1, 4, 60, 0, outside),
                (OGRE, 1, 1, 2, 150, 0, lambda x, y: outside(x, y) and far(x, y))]
    if name == "caverna":
        return [(RAT, 4, 3, 3, 20, 0, anywhere),
                (SKELETON, 3, 2, 4, 40, 0, far),
                (OGRE, 2, 1, 2, 90, 0, far)]
    if name == "ilha":
        return [(RAT, 2, 2, 3, 25, 0, anywhere),
                (SKELETON, 2, 2, 4, 45, 0, far),
                (OGRE, 1, 1, 2, 120, 0, far)]
    if name == "torre":
        courtyard = lambda x, y: 5 <= x <= 34 and 4 <= y <= 33       # noqa: E731
        return [(RAT, 2, 2, 3, 25, 0, courtyard),
                (SKELETON, 2, 2, 4, 40, 0, courtyard),
                (SKELETON, 1, 3, 4, 40, 1, anywhere),   # first floor, both rooms
                (RAT, 1, 2, 3, 30, 2, anywhere)]        # the roof
    _ = (h, w)
    return []


def populate_spawners(floors, rng, spawn, plan, taken):
    """Places nest anchors, reusing the same walkability rules as populate()."""
    placed = []
    for cls, nests, max_alive, radius, seconds, z, region in plan:
        g = floors[z]
        h, w = len(g), len(g[0])
        done = 0
        for _ in range(4000):
            if done >= nests:
                break
            x, y = rng.randrange(1, w - 1), rng.randrange(1, h - 1)
            if g[y][x] in BLOCKING or (x, y, z) in taken or not region(x, y):
                continue
            if z == 0 and abs(x - spawn[0]) <= 4 and abs(y - spawn[1]) <= 4:
                continue
            taken.add((x, y, z))
            placed.append((x, y, z, cls, max_alive, radius, seconds))
            done += 1
    return placed


def render(floors, spawn, title, monsters=(), spawners=()):
    lines = [f"# {title}",
             "# gerado por tools/gen_maps.py -- regere em vez de editar a mao,",
             "# ou edite com o game_editor (--map) e deixe este arquivo de lado.",
             f"size {len(floors[0][0])} {len(floors[0])} {len(floors)}"]
    for ch in sorted(used_glyphs(floors)):
        ground, obj = LEGEND[ch]
        lines.append(f"legend {ch} {ground}" + (f" {obj}" if obj else ""))
    if spawn is not None:
        lines.append("spawn @")
    for x, y, z, cls in monsters:
        lines.append(f"monster {x} {y} {z} {cls}")
    for x, y, z, cls, count, radius, seconds in spawners:
        lines.append(f"spawner {x} {y} {z} {cls} {count} {radius} {seconds}")
    for z, g in enumerate(floors):
        lines.append(f"floor {z}")
        lines += ["".join(row).rstrip() for row in g]
    return "\n".join(lines) + "\n"


def validate(name, floors, spawn, monsters=(), spawners=()):
    """Reports what a screenshot cannot: strandings and unknown glyphs."""
    ok = True
    for ch in used_glyphs(floors):
        if ch not in LEGEND:
            print(f"  !! {name}: glyph '{ch}' has no legend entry")
            ok = False

    reach = reachable3(floors, spawn)
    if not reach:
        print(f"  !! {name}: spawn {spawn} is not walkable")
        return False

    for z, g in enumerate(floors):
        total = walkable_count(g)
        got = sum(1 for _, _, rz in reach if rz == z)
        if got == total:
            print(f"     floor {z}: {total} walkable, all reachable")
        else:
            print(f"  !! {name} floor {z}: {total - got} of {total} walkable "
                  f"tiles unreachable from the spawn")
            ok = False

    # A stair with nothing walkable on the other side is content that looks
    # finished and silently does nothing when stepped on.
    for z, g in enumerate(floors):
        for y, row in enumerate(g):
            for x, ch in enumerate(row):
                delta = STAIR_DELTA.get(ch, 0)
                if not delta:
                    continue
                nz = z + delta
                if not (0 <= nz < len(floors)) or floors[nz][y][x] in BLOCKING:
                    print(f"  !! {name}: stair '{ch}' at ({x},{y},{z}) leads "
                          f"nowhere")
                    ok = False

    # A monster on a tile the player can never stand next to is a monster nobody
    # meets; one inside rock is skipped at spawn time and silently absent.
    for x, y, z, cls in monsters:
        if floors[z][y][x] in BLOCKING:
            print(f"  !! {name}: monster class {cls} at ({x},{y},{z}) is not on "
                  f"walkable ground")
            ok = False
        elif (x, y, z) not in reach:
            print(f"  !! {name}: monster class {cls} at ({x},{y},{z}) is "
                  f"unreachable")
            ok = False
    # A nest inside rock never produces anything, and one the player cannot reach
    # produces mobs that mill about forever behind a wall.
    for x, y, z, cls, count, radius, seconds in spawners:
        if floors[z][y][x] in BLOCKING:
            print(f"  !! {name}: spawner class {cls} at ({x},{y},{z}) is not on "
                  f"walkable ground")
            ok = False
        elif (x, y, z) not in reach:
            print(f"  !! {name}: spawner class {cls} at ({x},{y},{z}) is "
                  f"unreachable")
            ok = False
        elif count > (2 * radius + 1) ** 2:
            print(f"  !! {name}: spawner at ({x},{y},{z}) wants {count} alive in "
                  f"radius {radius}, which does not fit")
            ok = False
        _ = seconds

    if monsters:
        by_class = {}
        for _, _, _, cls in monsters:
            by_class[cls] = by_class.get(cls, 0) + 1
        summary = ", ".join(f"{n}x class {c}" for c, n in sorted(by_class.items()))
        print(f"     one-off monsters: {summary}")
    if spawners:
        alive = sum(count for _, _, _, _, count, _, _ in spawners)
        print(f"     spawners: {len(spawners)} nest(s), up to {alive} mobs alive")
    return ok


def spawn_floor(floors, spawn):
    _ = floors
    _ = spawn
    return 0  # every map here spawns on floor 0


GENERATORS = {
    "floresta": gen_floresta,
    "vila": gen_vila,
    "caverna": gen_caverna,
    "ilha": gen_ilha,
    "torre": gen_torre,
}


def main(argv):
    out_dir = "assets/maps"
    wanted = [a for a in argv if not a.startswith("-")] or list(GENERATORS)
    unknown = [w for w in wanted if w not in GENERATORS]
    if unknown:
        print(f"unknown map(s): {', '.join(unknown)}\n"
              f"available: {', '.join(GENERATORS)}")
        return 2

    failed = False
    for name in wanted:
        result = GENERATORS[name]()
        floors, spawn, title = result
        if not isinstance(floors[0][0], list):  # single-floor generators
            floors = [floors]
        clear_around(floors[spawn_floor(floors, spawn)], spawn)
        sealed = seal_pockets(floors, spawn)
        if sealed:
            print(f"  .. {name}: sealed {sealed} unreachable tile(s)")

        # Monsters are placed after the terrain is final (sealing can turn a tile
        # into a tree) and with their own RNG stream, so adding a class to one map
        # does not reshuffle another.
        mob_rng = random.Random(0xB0B5 + sum(ord(c) for c in name))
        monsters = populate(floors, mob_rng, spawn, monster_plan(name, floors,
                                                                spawn))
        taken = {(x, y, z) for x, y, z, _ in monsters}
        spawners = populate_spawners(floors, mob_rng, spawn,
                                     spawner_plan(name, floors, spawn), taken)

        path = os.path.join(out_dir, f"{name}.txt")
        with open(path, "w") as f:
            f.write(render(floors, spawn, title, monsters, spawners))
        w, h, z = len(floors[0][0]), len(floors[0]), len(floors)
        print(f"wrote {path} ({w}x{h}x{z})")
        if not validate(name, floors, spawn, monsters, spawners):
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
