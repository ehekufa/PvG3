# LVGL used by the native Android UI

Upstream: https://github.com/lvgl/lvgl , **v9.3.0** (tag `108e5aff3c90cc4d969331ed61aff2bbd365d430`). `src/`, `lvgl.h`, `lvgl_private.h`, `lv_version.h` and `LICENCE.txt` are copied from the official v9.3.0 release archive (SHA-256 `4933becfd3603b29158a5d04138139582836ef2bc17beb6c39dccda9cb0d32e7`). No upstream source files were edited. The rest of the upstream repository (tests, examples, docs, simulator) is not needed at runtime.

Configuration: `src/lv_conf.h` in the game repository. We compile C sources with the native Android NDK for arm64/arm32 and for local framebuffer tests; no Java UI, WebView, network dependency, or Gradle plugin is necessary. `src/lvgl_ui.c` supplies an ARGB8888 software flush and the pointer input driver, composited into the existing GLES2 texture. PT Sans is embedded once, then rendered by LVGL Tiny TTF for Cyrillic labels. Original user artwork is decoded from `sprites_data.h` by game.c; LVGL makes transient image descriptors from the same pixels.

Licensing: LVGL itself is MIT (`LICENCE.txt`). Tiny TTF bundles stb_truetype with MIT/public-domain choice (`src/libs/tiny_ttf/LICENSE.txt`). Both notices are also packaged into the APK under `assets/licenses/`. PT Sans remains OFL, see `assets/fonts/OFL.txt` in the repository root.
