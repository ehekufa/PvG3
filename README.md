# PvG3

A small **Plants vs. Zombies**-style lane-defense game written in **pure C** —
no engine, no external assets. The whole game (logic + renderer) draws into one
RGBA framebuffer; the Android layer just blits that framebuffer to the screen
through OpenGL ES.

Everything you see — lawn, plants, zombies, peas, suns, UI and text — is drawn
procedurally from filled circles/ellipses/rectangles and a tiny built-in 5×7
bitmap font.

## How to play

- Tap **sun** drops (they fall from the sky and are produced by sunflowers) to
  collect sun.
- Tap a **seed packet** at the top to select a plant, then tap a lawn cell to
  plant it.
- Survive the zombie waves. If a zombie gets past your lawn in a lane with no
  lawnmower left, it's game over.

| Plant       | Cost | What it does                                       |
|-------------|-----:|----------------------------------------------------|
| Sunflower   |   50 | Produces sun over time                             |
| Peashooter  |  100 | Shoots peas down its lane                          |
| Wall-nut    |   50 | Cheap high-HP wall that tanks zombies              |
| Snow Pea    |  175 | Peas that slow zombies                             |
| Cherry Bomb |  150 | Short fuse, then blasts everything in a 3x3 area   |
| Potato Mine |   25 | Arms itself, then blows up the first zombie to step on it |

## Layout

```
src/game.h          platform-independent API
src/game.c          game logic + software renderer (the whole game)
src/android_main.c  Android layer: native_app_glue + EGL/GLES2
tools/host_test.c   desktop harness that renders BMP screenshots
AndroidManifest.xml app.pvg3, NativeActivity, Android 10+ (API 29+)
.github/workflows/build-android.yml   CI build
```

## Build (Android APK)

The GitHub Actions workflow builds **both** ARM ABIs from the same sources and
packs them into a **single** APK:

- `arm64-v8a`   (64-bit)
- `armeabi-v7a` (32-bit)

min API 29 (Android 10), target API 34 (Android 14). The workflow:

1. sets up the Android SDK + NDK r25c,
2. compiles `libpvg3.so` for both ABIs,
3. packages, zip-aligns and signs one multi-ABI `PvG3.apk`,
4. uploads it as the `PvG3-Android-arm64-arm32` artifact.

## Verify locally (no device needed)

```sh
gcc -O2 -Wall -Isrc src/game.c tools/host_test.c -o host_test -lm
./host_test   # writes shots/menu.bmp and shots/play.bmp
```

## License

MIT — see [LICENSE](LICENSE). This is an original game inspired by the tower
defense genre; it is not affiliated with Plants vs. Zombies or its trademark
holders.
