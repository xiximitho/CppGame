#!/usr/bin/env python3
"""Generates a stone-dungeon text map in the CppGame map format.

Rooms carved out of solid rock, joined by corridors, with a little water and a
few crates. Deterministic from a seed. Output is plain text you (or a future
editor) can hand-edit. Run:  python3 gen_dungeon.py assets/maps/dungeon.txt
"""
import random
import sys

W, H = 56, 40
GROUND_STONE, GROUND_WATER = 3, 4
OBJ_WALL, OBJ_CRATE = 100, 102


def carve_rooms(seed):
    rng = random.Random(seed)
    # grid[y][x]: '#' rock/wall, '.' floor, '~' water, 'o' crate, '@' spawn
    g = [['#'] * W for _ in range(H)]
    rooms = []
    for _ in range(9):
        rw, rh = rng.randint(5, 10), rng.randint(4, 8)
        rx, ry = rng.randint(1, W - rw - 2), rng.randint(1, H - rh - 2)
        rooms.append((rx, ry, rw, rh))
        for y in range(ry, ry + rh):
            for x in range(rx, rx + rw):
                g[y][x] = '.'

    # Connect room centres with L-shaped corridors carved through the rock.
    def centre(r):
        return (r[0] + r[2] // 2, r[1] + r[3] // 2)

    for a, b in zip(rooms, rooms[1:]):
        (x1, y1), (x2, y2) = centre(a), centre(b)
        for x in range(min(x1, x2), max(x1, x2) + 1):
            g[y1][x] = '.' if g[y1][x] == '#' else g[y1][x]
        for y in range(min(y1, y2), max(y1, y2) + 1):
            g[y][x2] = '.' if g[y][x2] == '#' else g[y][x2]

    # A water pool in one room, crates scattered on floor, spawn in the first.
    px, py, pw, ph = rooms[3]
    for y in range(py + 1, py + ph - 1):
        for x in range(px + 1, px + pw - 1):
            if rng.random() < 0.8:
                g[y][x] = '~'
    placed = 0
    while placed < 14:
        x, y = rng.randint(1, W - 2), rng.randint(1, H - 2)
        if g[y][x] == '.':
            g[y][x] = 'o'
            placed += 1
    cx, cy = centre(rooms[0])
    g[cy][cx] = '@'
    return g


def main(out_path, seed=20260729):
    g = carve_rooms(seed)
    lines = [
        "# Grimhold - calabouco de pedra (mapa de exemplo, formato texto)",
        "# Edite a mão: '#' parede, '.' piso, '~' agua, 'o' caixa, '@' spawn.",
        f"size {W} {H} 1",
        f"legend . {GROUND_STONE}",
        f"legend # {GROUND_STONE} {OBJ_WALL}",
        f"legend ~ {GROUND_WATER}",
        f"legend o {GROUND_STONE} {OBJ_CRATE}",
        f"legend @ {GROUND_STONE}",
        "spawn @",
        "floor 0",
    ]
    lines += ["".join(row) for row in g]
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {out_path} ({W}x{H})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "dungeon.txt")
