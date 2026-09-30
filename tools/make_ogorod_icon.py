#!/usr/bin/env python3
"""Рисует иконку и название APK движка ОГОРОД.

Никаких зависимостей: PNG пишется вручную (zlib из стандартной
библиотеки), поэтому иконка одинакова на любой машине и в CI.

    python3 tools/make_ogorod_icon.py            # собрать res/ для движка
    python3 tools/make_ogorod_icon.py --check    # только проверить

Результат:
    engine/android/res/values/strings.xml        — имя приложения
    engine/android/res/mipmap-*/ic_launcher.png   — иконка запуска
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "engine" / "android" / "res"

APP_NAME = "Огород"

# Плотности Android: своя иконка на каждый экран.
DENSITIES = {
    "mdpi": 48,
    "hdpi": 72,
    "xhdpi": 96,
    "xxhdpi": 144,
    "xxxhdpi": 192,
}

# Палитра иконки: небо-газон, земля, росток.
GRASS = (0x6F, 0xC2, 0x6B)
GRASS_DARK = (0x4E, 0xA8, 0x51)
SOIL = (0x7A, 0x4F, 0x2B)
SOIL_DARK = (0x5D, 0x3A, 0x1E)
LEAF = (0x2F, 0x8F, 0x3C)
LEAF_LIGHT = (0x6A, 0xC7, 0x4E)
STEM = (0x3C, 0x7A, 0x2E)


class Canvas:
    """Простейший буфер RGB с примитивами по долям размера."""

    def __init__(self, size: int) -> None:
        self.size = size
        self.px = bytearray(GRASS * (size * size))

    def _blend(self, x: int, y: int, rgb: tuple[int, int, int], a: float) -> None:
        if a <= 0.0 or not (0 <= x < self.size and 0 <= y < self.size):
            return
        a = min(1.0, a)
        i = (y * self.size + x) * 3
        for k in range(3):
            self.px[i + k] = int(self.px[i + k] * (1.0 - a) + rgb[k] * a)

    def fill(self, rgb: tuple[int, int, int]) -> None:
        self.px = bytearray(rgb * (self.size * self.size))

    def rect(self, x0: float, y0: float, x1: float, y1: float,
             rgb: tuple[int, int, int]) -> None:
        s = self.size
        for y in range(max(0, int(y0 * s)), min(s, int(y1 * s) + 1)):
            for x in range(max(0, int(x0 * s)), min(s, int(x1 * s) + 1)):
                self._blend(x, y, rgb, 1.0)

    def ellipse(self, cx: float, cy: float, rx: float, ry: float,
                rgb: tuple[int, int, int], soft: float = 0.012) -> None:
        s = self.size
        for y in range(max(0, int((cy - ry) * s) - 1), min(s, int((cy + ry) * s) + 2)):
            for x in range(max(0, int((cx - rx) * s) - 1), min(s, int((cx + rx) * s) + 2)):
                d = ((x / s - cx) / rx) ** 2 + ((y / s - cy) / ry) ** 2
                a = 1.0 if d <= 1.0 else (1.0 if d <= (1.0 + soft) ** 2 else 0.0)
                if a:
                    self._blend(x, y, rgb, a)

    def png(self) -> bytes:
        s = self.size
        raw = bytearray()
        for y in range(s):
            raw.append(0)                      # фильтр строки: None
            raw += self.px[y * s * 3:(y + 1) * s * 3]

        def chunk(tag: bytes, data: bytes) -> bytes:
            return (struct.pack(">I", len(data)) + tag + data +
                    struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        ihdr = struct.pack(">IIBBBBB", s, s, 8, 2, 0, 0, 0)   # 8 бит, truecolor
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def draw(size: int) -> bytes:
    c = Canvas(size)
    # трава сверху, земля снизу
    c.rect(0.0, 0.62, 1.0, 1.0, SOIL)
    c.rect(0.0, 0.62, 1.0, 0.655, GRASS_DARK)
    # комочки земли
    for cx, cy, r in ((0.18, 0.74, 0.045), (0.52, 0.80, 0.055),
                      (0.82, 0.72, 0.040), (0.34, 0.90, 0.048)):
        c.ellipse(cx, cy, r, r * 0.8, SOIL_DARK)
    # стебель
    c.rect(0.485, 0.30, 0.515, 0.66, STEM)
    # два листа: тёмный снизу, светлый серп сверху
    c.ellipse(0.34, 0.37, 0.21, 0.13, LEAF, soft=0.02)
    c.ellipse(0.66, 0.45, 0.21, 0.13, LEAF, soft=0.02)
    c.ellipse(0.33, 0.352, 0.170, 0.108, LEAF_LIGHT, soft=0.03)
    c.ellipse(0.65, 0.432, 0.170, 0.108, LEAF_LIGHT, soft=0.03)
    # жилки листьев
    c.rect(0.335, 0.36, 0.345, 0.40, STEM)
    c.rect(0.655, 0.44, 0.665, 0.48, STEM)
    return c.png()


def strings_xml() -> str:
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<!-- Сгенерировано tools/make_ogorod_icon.py, не править руками. -->\n'
            '<resources>\n'
            f'    <string name="app_name">{APP_NAME}</string>\n'
            '</resources>\n')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="проверить, что файлы на диске актуальны")
    args = ap.parse_args()

    want = {"values/strings.xml": strings_xml().encode("utf-8")}
    for dens, size in DENSITIES.items():
        want[f"mipmap-{dens}/ic_launcher.png"] = draw(size)

    stale = []
    for rel, data in want.items():
        path = RES / rel
        if args.check:
            current = path.read_bytes() if path.exists() else b""
            if current != data:
                stale.append(rel)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f"{path.relative_to(ROOT)}: {len(data)} байт")

    if args.check:
        if stale:
            print("make_ogorod_icon: устарели: " + ", ".join(stale), file=sys.stderr)
            return 1
        print(f"make_ogorod_icon: {len(want)} файлов, всё актуально")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
