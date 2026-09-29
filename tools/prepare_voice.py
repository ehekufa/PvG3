#!/usr/bin/env python3
"""Convert temporary spoken lines to the stereo PCM16 48 kHz WAVs used by AAudio.

Speech recordings are stored only in assets/voice; no online TTS service or
Android speech engine is required when playing the APK. Re-run this tool after
replacing a line with a different mono PCM16 WAV (e.g. a real actor later).

    python3 tools/prepare_voice.py assets/voice/intro-*.wav
"""

from array import array
from pathlib import Path
import sys
import wave

OUTPUT_RATE = 48000


def prepare(path: Path) -> None:
    with wave.open(str(path), "rb") as wav:
        channels, width, rate, frames, codec, _ = wav.getparams()
        if width != 2 or codec != "NONE" or not (8000 <= rate <= 96000):
            raise ValueError(f"{path}: expected uncompressed 16-bit PCM")
        if channels == 2 and rate == OUTPUT_RATE:
            print(f"{path}: already stereo 48 kHz")
            return
        if channels != 1:
            raise ValueError(f"{path}: expected mono (or stereo 48 kHz)")
        samples = array("h")
        samples.frombytes(wav.readframes(frames))
        if sys.byteorder != "little":
            samples.byteswap()
        if not samples:
            raise ValueError(f"{path}: no audio")

    stereo = array("h")
    out_frames = (len(samples) * OUTPUT_RATE + rate // 2) // rate
    for i in range(out_frames):
        position = i * rate
        src, fraction = divmod(position, OUTPUT_RATE)
        first = samples[min(src, len(samples) - 1)]
        second = samples[min(src + 1, len(samples) - 1)]
        value = (first * (OUTPUT_RATE - fraction) + second * fraction +
                 OUTPUT_RATE // 2) // OUTPUT_RATE
        stereo.extend((value, value))
    if sys.byteorder != "little":
        stereo.byteswap()
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(OUTPUT_RATE)
        wav.writeframes(stereo.tobytes())
    print(f"{path}: {rate} Hz mono -> {OUTPUT_RATE} Hz stereo, "
          f"{out_frames / OUTPUT_RATE:.2f}s")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: prepare_voice.py assets/voice/intro-*.wav")
    for name in sys.argv[1:]:
        prepare(Path(name))
