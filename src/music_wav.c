#include "music_wav.h"

#include <string.h>

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int music_wav_parse(const uint8_t *bytes, size_t size, MusicWav *out) {
    if (!out) return 0;
    *out = (MusicWav){0};
    /* The cap is also applied before loading the file from the APK. */
    if (!bytes || size < 44 || size > 16u * 1024u * 1024u ||
        memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0 ||
        (uint64_t)le32(bytes + 4) + 8u != size) return 0;

    int have_format = 0, have_audio = 0;
    MusicWav candidate = {0};
    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t *chunk = bytes + pos;
        size_t length = le32(chunk + 4);
        pos += 8;
        if (length > size - pos) return 0;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (have_format || length < 16 || le16(bytes + pos) != 1 ||
                le16(bytes + pos + 2) != 2 ||
                le32(bytes + pos + 4) != 48000 ||
                le32(bytes + pos + 8) != 48000u * 4u ||
                le16(bytes + pos + 12) != 4 ||
                le16(bytes + pos + 14) != 16) return 0;
            candidate.sample_rate = 48000;
            have_format = 1;
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (have_audio || length == 0 || length % 4 != 0) return 0;
            candidate.frames = bytes + pos;
            candidate.frame_count = length / 4;
            have_audio = 1;
        }
        pos += length;
        if (length & 1) {
            if (pos == size) return 0;
            pos++; /* RIFF chunks are padded to an even byte count. */
        }
    }
    if (pos != size || !have_format || !have_audio) return 0;
    *out = candidate;
    return 1;
}

void music_wav_copy_loop(const MusicWav *wav, size_t *cursor,
                         uint8_t *destination, size_t frame_count) {
    size_t remaining = frame_count;
    while (remaining) {
        size_t count = wav->frame_count - *cursor;
        if (count > remaining) count = remaining;
        memcpy(destination, wav->frames + *cursor * 4, count * 4);
        destination += count * 4;
        remaining -= count;
        *cursor += count;
        if (*cursor == wav->frame_count) *cursor = 0;
    }
}
