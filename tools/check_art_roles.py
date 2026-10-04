#!/usr/bin/env python3
"""Protect the expected image-to-role assignments used by both renderers."""
import sys
sys.dont_write_bytecode = True
from pack_sprites import IMAGES, ROOT, png_pixels

EXPECTED = {
    "KHLEBUSHEK": "assets/art/khlebushek.png",
    "MASK": "assets/art/dima-mask.png",
    "KIRILL": "assets/art/kirill.png",
    "DUCK": "assets/art/zombie-duck.png",
    "DUCK_CONE": "assets/art/duck-cone.png",
    "DUCK_BUCKET": "assets/art/duck-bucket.png",
    "ROBOT": "assets/art/queen-robot.png",
    "PEA": "assets/art/peashooter.png",
    "WALL": "assets/art/walnut.png",
    "SUNFLOWER": "assets/art/coin-sunflower.png",
    "JUMPER": "assets/art/jumper-fighter.png",
    "LILY": "assets/art/lily-pad.png",
    "MAP": "assets/art/lawn-map.png",
    "WATER_MAP": "assets/art/water-map.png",
    "MOWER": "assets/art/lawnmower.png",
    "COIN": "assets/art/coin-token.png",
    "LEVEL_BLOCK": "assets/art/Блок.png",
    "LEVEL_PLATFORM": "assets/art/Платформа.png",
    "LEVEL_TRIGGER": "assets/art/Триггер-движения.png",
    "LEVEL_TRIGGER_ROTATE": "assets/art/Триггер-вращения.png",
    "LEVEL_TRIGGER_FOREVER": "assets/art/Триггер-вечно.png",
    "LEVEL_FLAG": "assets/art/Флажок - финиш.png",
    "LEVEL_SPIKE": "assets/art/Шип.png",
}

assert len(IMAGES) == len(EXPECTED) == len(dict(IMAGES)), "Unexpected extra/missing sprite"
assert dict(IMAGES) == EXPECTED, "The assigned image roles were swapped"
for name, filename in EXPECTED.items():
    w, h, pixels = png_pixels(ROOT / filename)
    assert w > 0 and h > 0 and any(p >> 24 for p in pixels), (name, filename)

# The two new duck drawings and the coin must remain transparent at the corners,
# so they can be composited over the author's map and UI without a black box.
for name in ("DUCK_CONE", "DUCK_BUCKET", "COIN"):
    _, _, pixels = png_pixels(ROOT / EXPECTED[name])
    assert pixels[0] == 0, (name, "expected transparent top-left corner")

print("OK: twenty-three named sprites, including platformer blocks, flag, spike and all three trigger variants")
