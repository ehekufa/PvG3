# LVGL used by the native Android UI

Upstream: https://github.com/lvgl/lvgl , **v9.3.0** (tag `108e5aff3c90cc4d969331ed61aff2bbd365d430`). `src/`, `lvgl.h`, `lvgl_private.h`, `lv_version.h` and `LICENCE.txt` are copied from the official v9.3.0 release archive (SHA-256 `4933becfd3603b29158a5d04138139582836ef2bc17beb6c39dccda9cb0d32e7`). No upstream source files were edited. The rest of the upstream repository (tests, examples, docs, simulator) is not needed at runtime.

Configuration: `src/lv_conf.h` in the game repository. The core game and its navigation are compiled from C with the native Android NDK for arm64/arm32 and for local framebuffer tests. `src/lvgl_ui.c` supplies an ARGB8888 software flush and pointer input driver, composited into the existing GLES2 texture for online rooms, published-level browsing and custom-level controls. The campaign menu, book, garden and battle HUD use the built-in C renderer. PT Sans is embedded for both renderers. Original user artwork is decoded from `sprites_data.h` by game.c; LVGL makes transient image descriptors from the same pixels.

Licensing: LVGL itself is MIT (`LICENCE.txt`). Tiny TTF bundles stb_truetype with MIT/public-domain choice (`src/libs/tiny_ttf/LICENSE.txt`). Both notices are also packaged into the APK under `assets/licenses/`. PT Sans remains OFL, see `assets/fonts/OFL.txt` in the repository root.
