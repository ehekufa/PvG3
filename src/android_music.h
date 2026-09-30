#ifndef PVG3_ANDROID_MUSIC_H
#define PVG3_ANDROID_MUSIC_H

#include <android/asset_manager.h>

/* Stream the original WAV packaged in APK assets. Music is optional: if the
 * device cannot open an output stream, gameplay still starts normally. */
int android_music_init(AAssetManager *assets);
void android_music_set_playing(int foreground);
/* 0..2: replay the spoken line of level 0; -1: stop speech on exit/skip.
 * Takes effect in the AAudio callback without blocking the UI thread. */
void android_music_intro_line(int line);
void android_music_shutdown(void);

#endif
