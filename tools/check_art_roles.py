#!/usr/bin/env python3
"""Protect the author's confirmed drawing-to-character assignments.

A generated header matching its generator is not enough: an accidental swap in
that generator would still build. Keep these expectations independent of it.
"""
import sys
sys.dont_write_bytecode = True
from pack_sprites import IMAGES, ROOT, png_pixels

EXPECTED = {
    "KHLEBUSHEK": "assets/art/khlebushek.png",
    "MASK": "assets/art/dima-mask.png",
    "KIRILL": "assets/art/kirill.png",
    "DUCK": "assets/art/zombie-duck.png",
    "ROBOT": "assets/art/queen-robot.png",
    "PEA": "assets/art/peashooter.png",
    "WALL": "assets/art/walnut.png",
    "SUNFLOWER": "assets/art/coin-sunflower.png",
    "JUMPER": "assets/art/jumper-fighter.png",
    "LILY": "assets/art/lily-pad.png",
    "MAP": "assets/art/lawn-map.png",
    "WATER_MAP": "assets/art/water-map.png",
    "MOWER": "assets/art/lawnmower.png",
}

assert len(IMAGES) == len(EXPECTED) == len(dict(IMAGES)), "Unexpected extra/missing sprite"
assert dict(IMAGES) == EXPECTED, "The author's drawing roles were swapped"
for name, filename in EXPECTED.items():
    w, h, pixels = png_pixels(ROOT / filename)
    assert w > 0 and h > 0 and any(p >> 24 for p in pixels), (name, filename)

print("OK: thirteen named drawings, including updated Kirill/duck, canal and lily pad")
