#ifndef PVG3_ANDROID_MUSIC_H
#define PVG3_ANDROID_MUSIC_H

#include <android/asset_manager.h>

/* Stream the original WAV packaged in APK assets. Music is optional: if the
 * device cannot open an output stream, gameplay still starts normally. */
int android_music_init(AAssetManager *assets);
void android_music_set_playing(int foreground);
void android_music_shutdown(void);

#endif
