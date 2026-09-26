#!/usr/bin/env python3
"""Protect the author's confirmed drawing-to-character assignments.

A generated header matching its generator is not enough: an accidental swap in
that generator would still build. Keep these expectations independent of it.
"""
import sys
sys.dont_write_bytecode = True
from pack_sprites import IMAGES, ROOT, png_pixels

EXPECTED = {
    "KHLEBUSHEK": "Без названия680_20260926092924.png",
    "MASK": "Без названия681_20260926093006.png",
    "KIRILL": "Без названия681_20260926093232.png",
    "DUCK": "Без названия681_20260926093504.png",
    "ROBOT": "Без названия682_20260926094249.png",
    "PEA": "Без названия684_20260926094420.png",
    "WALL": "Без названия685_20260926094527.png",
    "SUNFLOWER": "Без названия685_20260926094634.png",
    "MAP": "Без названия687_20260926095025.png",
    "MOWER": "Без названия688_20260926095109.png",
}

assert len(IMAGES) == len(EXPECTED) == len(dict(IMAGES)), "Unexpected extra/missing sprite"
assert dict(IMAGES) == EXPECTED, "The author's drawing roles were swapped"
for name, filename in EXPECTED.items():
    w, h, pixels = png_pixels(ROOT / filename)
    assert w > 0 and h > 0 and any(p >> 24 for p in pixels), (name, filename)

print("OK: ten original drawings, including Kirill's coin sunflower and the duck")
