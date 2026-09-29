/* Native AAudio playback of the original 48 kHz WAV soundtrack and the
 * temporary spoken lines in level 0. API 26+ (the game's minimum is 29).
 * The callback does no allocation, I/O, logging or game-state work. The
 * Android event thread posts voice commands atomically; a new line replaces
 * the previous one while the soundtrack quietly continues underneath. */
#include "android_music.h"
#include "music_wav.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_LOG(...) __android_log_print(ANDROID_LOG_WARN, "PvG3 music", __VA_ARGS__)
#define MUSIC_ASSET "music/kirill-pond-loop.wav"

static const char *const VOICE_ASSETS[3] = {
    "voice/intro-bread.wav", "voice/intro-dima.wav", "voice/intro-kirill.wav"
};

typedef struct {
    AAudioStream *stream;
    uint8_t *file;
    MusicWav wav;
    uint8_t *voice_file[3];
    MusicWav voice[3];
    size_t music_cursor; /* callback-owned after starting the stream */
    size_t voice_cursor; /* callback-owned */
    unsigned heard_command; /* callback-owned */
    int playing; /* Android event thread only */
} MusicPlayer;

static MusicPlayer player;
/* High bits are a monotonically changing sequence, low two bits are 0 for
 * stop or 1..3 for the dialogue line. A replay of the same line restarts it. */
static _Atomic unsigned voice_command;

static aaudio_data_callback_result_t music_callback(AAudioStream *stream,
                                                    void *context, void *audio,
                                                    int32_t frames) {
    (void)stream;
    MusicPlayer *music = (MusicPlayer *)context;
    music_wav_copy_loop(&music->wav, &music->music_cursor, (uint8_t *)audio,
                        (size_t)frames);
    unsigned command = atomic_load_explicit(&voice_command, memory_order_acquire);
    if (command != music->heard_command) {
        music->heard_command = command;
        music->voice_cursor = 0;
    }
    unsigned line = command & 3u;
    if (line && music->voice[line - 1].frames)
        music_wav_overlay_voice(&music->voice[line - 1], &music->voice_cursor,
                                (uint8_t *)audio, (size_t)frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void android_music_shutdown(void) {
    if (player.stream) {
        if (player.playing) AAudioStream_requestStop(player.stream);
        AAudioStream_close(player.stream); /* callback ended before freeing PCM */
    }
    free(player.file);
    for (int i = 0; i < 3; ++i) free(player.voice_file[i]);
    memset(&player, 0, sizeof player);
    atomic_store_explicit(&voice_command, 0, memory_order_release);
}

/* WAV is optional and must have the same PCM format as the soundtrack so
 * AAudio can mix it without resampling or JNI on the real-time thread. */
static int load_wav(AAssetManager *assets, const char *name,
                    uint8_t **bytes, MusicWav *wav) {
    AAsset *asset = AAssetManager_open(assets, name, AASSET_MODE_STREAMING);
    if (!asset) { MUSIC_LOG("missing %s", name); return 0; }
    off64_t length = AAsset_getLength64(asset);
    if (length < 44 || length > 16 * 1024 * 1024) {
        MUSIC_LOG("invalid WAV size: %s (%lld)", name, (long long)length);
        AAsset_close(asset);
        return 0;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data) { AAsset_close(asset); return 0; }
    size_t read = 0;
    while (read < (size_t)length) {
        int chunk = (size_t)length - read > INT_MAX ? INT_MAX : (int)((size_t)length - read);
        int got = AAsset_read(asset, data + read, chunk);
        if (got <= 0) break;
        read += (size_t)got;
    }
    AAsset_close(asset);
    if (read != (size_t)length || !music_wav_parse(data, read, wav)) {
        MUSIC_LOG("invalid or incomplete WAV: %s", name);
        free(data);
        return 0;
    }
    *bytes = data;
    return 1;
}

int android_music_init(AAssetManager *assets) {
    android_music_shutdown();
    if (!assets || !load_wav(assets, MUSIC_ASSET, &player.file, &player.wav)) return 0;
    for (int i = 0; i < 3; ++i)
        load_wav(assets, VOICE_ASSETS[i], &player.voice_file[i], &player.voice[i]);

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

void android_music_intro_line(int line) {
    unsigned prev = atomic_load_explicit(&voice_command, memory_order_relaxed);
    unsigned next = ((prev >> 2) + 1u) * 4u;
    if (line >= 0 && line < 3) next |= (unsigned)(line + 1);
    atomic_store_explicit(&voice_command, next, memory_order_release);
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
