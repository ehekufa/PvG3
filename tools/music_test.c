/* Validate the committed music as both an independently usable WAV and
 * the exact stereo PCM format streamed by the Android audio callback.
 * gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
 *     src/music_wav.c tools/music_test.c -o music_test -lm && ./music_test
 */
#include "music_wav.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sample(const uint8_t *p) {
    int value = (int)p[0] | ((int)p[1] << 8);
    return value < 32768 ? value : value - 65536;
}

int main(void) {
    FILE *file = fopen("assets/music/kirill-pond-loop.wav", "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size == 44 + 16 * 4 * 25600 * 4);
    assert(fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)size);
    assert(bytes);
    assert(fread(bytes, 1, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);

    MusicWav wav;
    assert(music_wav_parse(bytes, (size_t)size, &wav));
    assert(wav.frames == bytes + 44 && wav.frame_count == 16u * 4u * 25600u);
    assert(wav.sample_rate == 48000);
    int peak = 0;
    double energy = 0;
    for (size_t i = 0; i < wav.frame_count * 4; i += 2) {
        int value = sample(wav.frames + i);
        int absolute = abs(value);
        if (absolute > peak) peak = absolute;
        energy += (double)value * value;
    }
    double rms = sqrt(energy / (wav.frame_count * 2));
    assert(peak > 10000 && peak < 32000 && rms > 1200 && rms < 6500);
    /* Continuity when the playback cursor wraps: no click at the loop edge. */
    assert(abs(sample(wav.frames) - sample(wav.frames + (wav.frame_count - 1) * 4)) < 500);
    assert(abs(sample(wav.frames + 2) - sample(wav.frames + (wav.frame_count - 1) * 4 + 2)) < 500);
    uint8_t buffer[10 * 4];
    size_t cursor = wav.frame_count - 3;
    music_wav_copy_loop(&wav, &cursor, buffer, 10);
    assert(cursor == 7);
    assert(memcmp(buffer, wav.frames + (wav.frame_count - 3) * 4, 3 * 4) == 0);
    assert(memcmp(buffer + 3 * 4, wav.frames, 7 * 4) == 0);
    cursor = 0;
    music_wav_copy_loop(&wav, &cursor, buffer, 0);
    assert(cursor == 0);

    assert(!music_wav_parse(NULL, (size_t)size, &wav));
    assert(!music_wav_parse(bytes, (size_t)size - 1, &wav));
    assert(!music_wav_parse(bytes, (size_t)size, NULL));
    uint8_t original = bytes[0];
    bytes[0] = 'X';
    assert(!music_wav_parse(bytes, (size_t)size, &wav) && !wav.frames);
    bytes[0] = original;
    original = bytes[22]; /* format channels must be stereo */
    bytes[22] = 1;
    assert(!music_wav_parse(bytes, (size_t)size, &wav) && !wav.frames);
    bytes[22] = original;
    assert(music_wav_parse(bytes, (size_t)size, &wav));

    printf("OK: original %.3fs looping WAV, stereo PCM16 48kHz, peak=%d rms=%.0f\n",
           (double)wav.frame_count / wav.sample_rate, peak, rms);
    free(bytes);
    return 0;
}
