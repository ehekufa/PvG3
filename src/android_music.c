/* Native AAudio playback for the game's original 48 kHz WAV loop.
 * API 26+; PvG3's minimum is API 29. The AAudio callback only copies PCM,
 * never allocates or touches rendering/game state. Lifecycle commands pause
 * it when the app loses focus or its window, not only when the game pauses.
 */
#include "android_music.h"
#include "music_wav.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_LOG(...) __android_log_print(ANDROID_LOG_WARN, "PvG3 music", __VA_ARGS__)
#define MUSIC_ASSET "music/kirill-pond-loop.wav"

typedef struct {
    AAudioStream *stream;
    uint8_t *file;
    MusicWav wav;
    size_t cursor; /* written ONLY by the AAudio callback after starting */
    int playing;   /* written ONLY by the Android event thread */
} MusicPlayer;

static MusicPlayer player;

static aaudio_data_callback_result_t music_callback(AAudioStream *stream,
                                                    void *context, void *audio,
                                                    int32_t frames) {
    (void)stream;
    MusicPlayer *music = (MusicPlayer *)context;
    music_wav_copy_loop(&music->wav, &music->cursor, (uint8_t *)audio,
                        (size_t)frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void android_music_shutdown(void) {
    if (player.stream) {
        if (player.playing) AAudioStream_requestStop(player.stream);
        AAudioStream_close(player.stream); /* callback is finished before freeing PCM */
    }
    free(player.file);
    memset(&player, 0, sizeof(player));
}

int android_music_init(AAssetManager *assets) {
    android_music_shutdown();
    if (!assets) return 0;
    AAsset *asset = AAssetManager_open(assets, MUSIC_ASSET, AASSET_MODE_STREAMING);
    if (!asset) { MUSIC_LOG("missing %s", MUSIC_ASSET); return 0; }
    off64_t length = AAsset_getLength64(asset);
    if (length < 44 || length > 16 * 1024 * 1024) {
        MUSIC_LOG("invalid WAV size: %lld", (long long)length);
        AAsset_close(asset);
        return 0;
    }
    player.file = malloc((size_t)length);
    if (!player.file) { AAsset_close(asset); return 0; }
    size_t read = 0;
    while (read < (size_t)length) {
        int chunk = (size_t)length - read > INT_MAX ? INT_MAX : (int)((size_t)length - read);
        int got = AAsset_read(asset, player.file + read, chunk);
        if (got <= 0) break;
        read += (size_t)got;
    }
    AAsset_close(asset);
    if (read != (size_t)length ||
        !music_wav_parse(player.file, (size_t)length, &player.wav)) {
        MUSIC_LOG("invalid or incomplete music WAV");
        android_music_shutdown();
        return 0;
    }

    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        MUSIC_LOG("cannot create AAudio builder: %d", result);
        android_music_shutdown();
        return 0;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)player.wav.sample_rate);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setDataCallback(builder, music_callback, &player);
    result = AAudioStreamBuilder_openStream(builder, &player.stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK || !player.stream) {
        MUSIC_LOG("cannot open AAudio output: %d", result);
        android_music_shutdown();
        return 0;
    }
    if (AAudioStream_getSampleRate(player.stream) != (int32_t)player.wav.sample_rate ||
        AAudioStream_getChannelCount(player.stream) != 2 ||
        AAudioStream_getFormat(player.stream) != AAUDIO_FORMAT_PCM_I16) {
        MUSIC_LOG("device output differs from stereo 48kHz PCM16");
        android_music_shutdown();
        return 0;
    }
    return 1;
}

void android_music_set_playing(int foreground) {
    if (!player.stream || !!foreground == !!player.playing) return;
    aaudio_result_t result = foreground ? AAudioStream_requestStart(player.stream)
                                       : AAudioStream_requestPause(player.stream);
    if (!foreground && result == AAUDIO_ERROR_UNIMPLEMENTED)
        result = AAudioStream_requestStop(player.stream);
    if (result == AAUDIO_OK) player.playing = !!foreground;
    else MUSIC_LOG("cannot %s music: %d", foreground ? "start" : "pause", result);
}
