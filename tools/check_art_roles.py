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
    "LEVEL_TRIGGER_INVISIBILITY": "assets/art/Триггер-невидимости.png",
    "LEVEL_TRIGGER_NO_COLLISION": "assets/art/Триггер-нет столкновения.png",
    "LEVEL_FLAG": "assets/art/Флажок - финиш.png",
    "LEVEL_SPIKE": "assets/art/Шип.png",
    "LEVEL_SLOPE": "assets/art/Склон.png",
    "LEVEL_TRIGGER_GRAVITY": "assets/art/Триггер-гравитации.png",
    "LEVEL_ORB_ORANGE": "assets/art/Оранжевый opб.png",
    "LEVEL_ORB_YELLOW": "assets/art/Жёлтый орб.png",
    "LEVEL_CHECKPOINT_INACTIVE": "assets/art/Чекпоинт-выключен.png",
    "LEVEL_CHECKPOINT_ACTIVE": "assets/art/Чекпоинт-включён.png",
    "LEVEL_PORTAL_NORMAL": "assets/art/Портал-обычный.png",
    "LEVEL_PORTAL_JETPACK": "assets/art/Портал-джетпака.png",
    "JETPACK_ACTIVE": "assets/art/джетпак-активен.png",
    "JETPACK_INACTIVE": "assets/art/Джетпак-отключён.png",
    "LEVEL_TRIGGER_COLOR": "assets/art/Триггер-цвет.png",
}

assert len(IMAGES) == len(EXPECTED) == len(dict(IMAGES)), "Unexpected extra/missing sprite"
assert dict(IMAGES) == EXPECTED, "The assigned image roles were swapped"
for name, filename in EXPECTED.items():
    w, h, pixels = png_pixels(ROOT / filename)
    assert w > 0 and h > 0 and any(p >> 24 for p in pixels), (name, filename)

# These hitbox rectangles are the exact non-transparent bounds used by the
# browser and native preview. Keep them synchronized with the source artwork.
EXPECTED_ALPHA_BOUNDS = {
    "KHLEBUSHEK": (21, 2, 80, 96),
    "DUCK": (20, 21, 99, 99),
    "COIN": (5, 3, 96, 97),
    "LEVEL_FLAG": (2, 3, 50, 100),
    "LEVEL_CHECKPOINT_INACTIVE": (14, 0, 75, 100),
    "LEVEL_CHECKPOINT_ACTIVE": (14, 0, 75, 100),
    "LEVEL_PORTAL_NORMAL": (16, 0, 77, 100),
    "LEVEL_PORTAL_JETPACK": (17, 0, 76, 100),
    "JETPACK_ACTIVE": (10, 10, 85, 97),
    "JETPACK_INACTIVE": (10, 10, 85, 85),
}
for name, expected in EXPECTED_ALPHA_BOUNDS.items():
    w, h, pixels = png_pixels(ROOT / EXPECTED[name])
    opaque = [(i % w, i // w) for i, pixel in enumerate(pixels) if pixel >> 24]
    bounds = (min(x for x, _ in opaque), min(y for _, y in opaque),
              max(x for x, _ in opaque) + 1, max(y for _, y in opaque) + 1)
    assert bounds == expected, (name, bounds, expected)

# Author drawings and all trigger icons remain transparent at the corners, so
# they can be composited over the map and UI without a black box.
for name in ("DUCK_CONE", "DUCK_BUCKET", "COIN", "LEVEL_TRIGGER",
             "LEVEL_TRIGGER_ROTATE", "LEVEL_TRIGGER_FOREVER",
             "LEVEL_TRIGGER_INVISIBILITY", "LEVEL_TRIGGER_NO_COLLISION",
             "LEVEL_TRIGGER_GRAVITY", "LEVEL_TRIGGER_COLOR",
             "LEVEL_ORB_ORANGE", "LEVEL_ORB_YELLOW",
             "LEVEL_CHECKPOINT_INACTIVE", "LEVEL_CHECKPOINT_ACTIVE",
             "LEVEL_PORTAL_NORMAL", "LEVEL_PORTAL_JETPACK",
             "JETPACK_ACTIVE", "JETPACK_INACTIVE"):
    _, _, pixels = png_pixels(ROOT / EXPECTED[name])
    assert pixels[0] == 0, (name, "expected transparent top-left corner")
_, _, slope_pixels = png_pixels(ROOT / EXPECTED["LEVEL_SLOPE"])
assert slope_pixels[0] >> 24 <= 16, "slope artwork should fade at its transparent corner"
wheel_w, wheel_h, wheel_pixels = png_pixels(ROOT / EXPECTED["LEVEL_TRIGGER_COLOR"])
assert wheel_w == wheel_h, "color-wheel art should be square"
wheel_cx, wheel_cy = (wheel_w - 1) / 2, (wheel_h - 1) / 2
for y in range(wheel_h):
    for x in range(wheel_w):
        radius = ((x - wheel_cx) ** 2 + (y - wheel_cy) ** 2) ** .5
        alpha = wheel_pixels[y * wheel_w + x] >> 24
        assert radius >= 18 or alpha == 0, "color-wheel center hole should stay transparent"
        assert radius < 92 or alpha == 0, "color-wheel outside edge should stay transparent"

print(f"OK: {len(EXPECTED)} named sprites, including clean transparent color-wheel art, both portals and both Jetpack states")
