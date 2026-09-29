#ifndef PVG3_MUSIC_WAV_H
#define PVG3_MUSIC_WAV_H

#include <stddef.h>
#include <stdint.h>

/* A non-owning view of an entire RIFF/WAVE file. The only supported format is
 * interleaved stereo, 16-bit little-endian PCM, 48000 frames per second.
 * Keep the underlying file bytes alive while using this view. */
typedef struct {
    const uint8_t *frames; /* four bytes per stereo frame: L16, R16 */
    size_t frame_count;
    uint32_t sample_rate;
} MusicWav;

/* Validate the RIFF structure and audio format. On error, zero *out; never
 * pass arbitrary bytes through to the Android audio callback. */
int music_wav_parse(const uint8_t *bytes, size_t size, MusicWav *out);

/* Copy a chunk of interleaved frames, wrapping seamlessly at the end.
 * The caller owns *cursor (0 <= cursor < frame_count); no allocations. */
void music_wav_copy_loop(const MusicWav *wav, size_t *cursor,
                         uint8_t *destination, size_t frame_count);

/* Mix one non-looping spoken clip into a frame of background music, ducking
 * music while there is speech. Both WAVs are already validated as stereo
 * 48kHz PCM16. When the voice ends, the unchanged music remains audible. */
void music_wav_overlay_voice(const MusicWav *voice, size_t *voice_cursor,
                             uint8_t *music, size_t frame_count);

#endif
