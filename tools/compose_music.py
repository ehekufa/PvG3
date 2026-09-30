#!/usr/bin/env python3
"""Compose PvG3's original, seamless background theme as a stereo PCM WAV.

No samples, MIDI instruments, music libraries or network downloads are used:
every voice is synthesized from oscillators and seeded noise in the Python
standard library. Tempo 112.5 BPM makes one beat exactly 25,600 samples at
48 kHz, so the 16-bar loop lands precisely on a sample boundary.

Run: python3 tools/compose_music.py
     python3 tools/compose_music.py --check  # verify committed audio is in sync
"""

from array import array
from io import BytesIO
from math import cos, exp, pi, sin, sqrt
from pathlib import Path
from random import Random
import hashlib
import sys
import wave

RATE = 48000
BEAT = 25600  # 112.5 BPM; exact sample timing
BARS = 16
FRAMES = BARS * 4 * BEAT
TAU = 2 * pi
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/music/kirill-pond-loop.wav"

# One four-chord journey, a little suspense, and a return to the home chord.
# MIDI note numbers are only a convenient way to describe ORIGINAL music.
# Chord voices stay in the middle register so the bass and mallet have space.
CHORDS = (
    (52, 55, 59, 66), (48, 52, 55, 59), (50, 55, 59, 64), (50, 54, 57, 64),
    (52, 55, 59, 66), (48, 52, 55, 59), (45, 48, 52, 55), (47, 51, 54, 57),
    (52, 55, 59, 66), (48, 52, 55, 59), (50, 55, 59, 64), (50, 54, 57, 64),
    (48, 52, 55, 59), (45, 48, 52, 55), (47, 51, 54, 57), (52, 55, 59, 66),
)

# (beat within bar, pitch, relative velocity): a playful question/answer for
# Kirill and the plants, with a small darker turn before the phrase resolves.
MELODY = (
    ((0, 76, 1), (1.5, 79, .8), (2.25, 71, .75), (3, 76, .85)),
    ((0, 79, .9), (1, 76, .7), (1.75, 74, .75), (2.5, 72, .8), (3.5, 71, .65)),
    ((0, 74, .95), (.75, 71, .7), (1.5, 67, .75), (2.5, 71, .8), (3.5, 76, .75)),
    ((0, 78, .9), (1, 76, .75), (1.75, 74, .75), (2.75, 69, .75)),
    ((0, 76, 1), (.75, 79, .85), (1.5, 83, .85), (2.5, 79, .78), (3.5, 78, .68)),
    ((0, 76, .85), (.5, 74, .65), (1.5, 72, .85), (2.5, 67, .65), (3, 71, .8)),
    ((0, 72, .9), (.75, 76, .75), (1.5, 79, .8), (2.5, 76, .75), (3.5, 72, .7)),
    ((0, 75, .9), (.75, 78, .75), (1.5, 81, .87), (2.5, 78, .72), (3.25, 75, .8)),
    ((0, 76, .98), (.75, 71, .7), (1.5, 79, .82), (2.25, 78, .72), (3, 76, .86)),
    ((0, 72, .86), (1, 76, .79), (1.5, 79, .78), (2.5, 83, .75), (3.25, 79, .75)),
    ((0, 74, .94), (.75, 71, .72), (1.5, 67, .78), (2, 69, .7), (3, 71, .84)),
    ((0, 78, .83), (.75, 74, .75), (1.5, 76, .79), (2.5, 69, .8)),
    ((0, 79, .88), (1, 76, .72), (1.75, 72, .79), (2.5, 71, .76)),
    ((0, 72, .85), (.75, 76, .8), (1.5, 79, .9), (2.5, 76, .74)),
    ((0, 75, .85), (.75, 78, .78), (1.5, 81, .91), (2.5, 78, .75), (3.5, 75, .72)),
    ((0, 76, .98), (1, 79, .73), (2, 83, .85), (3.25, 71, .73)),
)


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def seconds(count):
    return int(RATE * count)


def make_mallet(note):
    f = hz(note)
    result = array("f")
    for i in range(seconds(.82)):
        t = i / RATE
        a = (1 - exp(-t * 410)) * exp(-t * 5.7)
        a *= min(1.0, (seconds(.82) - i) / seconds(.055))
        x = TAU * f * t
        result.append(a * (sin(x + .13 * exp(-t * 22) * sin(4 * x)) * .74
                           + sin(2.01 * x) * .17 * exp(-t * 8)
                           + sin(3.93 * x) * .13 * exp(-t * 19)))
    return result


def make_glass(note):
    f = hz(note)
    result = array("f")
    for i in range(seconds(1.55)):
        t = i / RATE
        a = (1 - exp(-t * 480)) * exp(-t * 2.3)
        a *= min(1.0, (seconds(1.55) - i) / seconds(.09))
        x = TAU * f * t
        result.append(a * (sin(x) * .64 + sin(2.7 * x) * .19 * exp(-t * 3.3)
                           + sin(4.13 * x) * .09 * exp(-t * 8)))
    return result


def make_bass(note):
    f = hz(note)
    result = array("f")
    for i in range(seconds(.78)):
        t = i / RATE
        a = (1 - exp(-t * 185)) * exp(-t * 3.6)
        a *= min(1.0, (seconds(.78) - i) / seconds(.09))
        x = TAU * f * t
        result.append(a * (sin(x) * .90 + sin(2 * x) * .22 * exp(-t * 5)
                           + sin(3 * x) * .065 * exp(-t * 13)))
    return result


def make_pad(note):
    f = hz(note)
    length = 4 * BEAT + seconds(.52)  # releases into next chord (and loop start)
    result = array("f")
    for i in range(length):
        t = i / RATE
        a = min(1.0, t / .22) * min(1.0, (length - i) / seconds(.5))
        x = TAU * f * t
        result.append(a * (sin(x) * .50 + sin(x * 1.0019) * .24
                           + sin(x * .9981) * .24 + sin(2 * x) * .075))
    return result


def make_kick():
    result = array("f")
    phase = 0.0
    for i in range(seconds(.3)):
        t = i / RATE
        f = 54 + 120 * exp(-t * 28)
        phase += TAU * f / RATE
        result.append((sin(phase) * exp(-t * 17) * .94
                       + sin(phase * 2) * exp(-t * 48) * .13)
                      * min(1.0, t * 1000))
    return result


def make_noise_hit(kind, seed):
    rnd = Random(seed)
    snare = kind == "snare"
    duration = .28 if snare else .115
    decay = 18 if snare else 48
    low = 0.0
    result = array("f")
    for i in range(seconds(duration)):
        t = i / RATE
        white = rnd.uniform(-1.0, 1.0)
        low = low * .84 + white * .16
        high = white - low
        a = (1 - exp(-t * 850)) * exp(-t * decay)
        a *= min(1.0, (seconds(duration) - i) / seconds(.012))
        tone = sin(TAU * 174 * t) * exp(-t * 24) * .16 if snare else 0.0
        result.append(a * (high * (.48 if snare else .38) + tone))
    return result


class Mixer:
    def __init__(self):
        self.left = array("f", [0.0]) * FRAMES
        self.right = array("f", [0.0]) * FRAMES

    def add(self, sound, start, volume, pan=0.0):
        """Circular mixing carries note/echo tails through the loop boundary."""
        start %= FRAMES
        lscale = volume * sqrt((1 - pan) / 2)
        rscale = volume * sqrt((1 + pan) / 2)
        left, right = self.left, self.right
        for i, value in enumerate(sound):
            p = (start + i) % FRAMES
            left[p] += value * lscale
            right[p] += value * rscale

    def as_wav(self):
        # Circular cross-channel delays create space without non-free samples.
        left, right = self.left, self.right
        delay_a, delay_b = seconds(.195), seconds(.383)
        peak = max(max(abs(s) for s in left), max(abs(s) for s in right))
        if peak < .01:
            raise ValueError("silent score")
        scale = .80 / (peak * 1.19)  # 1.19 is the upper bound of the two echoes
        pcm = array("h")
        for i in range(FRAMES):
            l = (left[i] + .115 * right[(i - delay_a) % FRAMES]
                 + .075 * left[(i - delay_b) % FRAMES]) * scale
            r = (right[i] + .115 * left[(i - delay_a) % FRAMES]
                 + .075 * right[(i - delay_b) % FRAMES]) * scale
            pcm.append(round(max(-1.0, min(1.0, l)) * 32767))
            pcm.append(round(max(-1.0, min(1.0, r)) * 32767))
        if sys.byteorder != "little":
            pcm.byteswap()
        output = BytesIO()
        with wave.open(output, "wb") as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(pcm.tobytes())
        return output.getvalue()


def compose():
    mix = Mixer()
    counter = {n for chord in CHORDS for n in
               (chord[0] + 24, chord[2] + 12, chord[1] + 24, chord[3] + 12)}
    melody_notes = {n for phrase in MELODY for _, n, _ in phrase}
    mallets = {n: make_mallet(n) for n in melody_notes | counter}
    glass = {n: make_glass(n) for n in (83, 86, 88, 90)}
    basses = {n: make_bass(n) for n in (40, 43, 36, 38, 33, 35)}
    pads = {n: make_pad(n) for chord in CHORDS for n in chord}
    kick = make_kick()
    snare = make_noise_hit("snare", 45)
    hat = make_noise_hit("hat", 2026)
    roots = (40, 36, 43, 38, 40, 36, 33, 35,
             40, 36, 43, 38, 36, 33, 35, 40)

    for bar, (chord, melody, root) in enumerate(zip(CHORDS, MELODY, roots)):
        at = bar * 4 * BEAT
        softer = .82 if bar in (3, 7, 11, 15) else 1.0
        for voice, note in enumerate(chord):
            mix.add(pads[note], at, .064 * softer, (-.48, .43, -.23, .18)[voice])
        for beat, note, velocity in melody:
            mix.add(mallets[note], at + round(beat * BEAT),
                    .31 * velocity * softer, -.23 if int(beat) % 2 == 0 else .27)
        for beat, velocity in ((0, .29), (2, .25), (3.5, .19)):
            mix.add(basses[root], at + round(beat * BEAT), velocity, -.08)
        for beat, velocity in ((0, .53), (2, .46)):
            mix.add(kick, at + round(beat * BEAT), velocity * softer)
        for beat in (1, 3):
            mix.add(snare, at + beat * BEAT, .33 * softer, .12)
        for step in range(8):
            v = .24 if step % 2 else .19
            mix.add(hat, at + step * BEAT // 2, v * softer,
                    -.3 if step % 2 else .3)
        if bar in (2, 4, 6, 9, 10, 12, 13):
            # Quiet, offbeat counter-pattern like drops falling onto a leaf.
            high = (chord[0] + 24, chord[2] + 12,
                    chord[1] + 24, chord[3] + 12)
            for step, beat in enumerate((.5, 1.5, 2.5, 3.5)):
                mix.add(mallets[high[step]], at + round(beat * BEAT),
                        .085, .50 if step % 2 else -.50)
        if bar % 4 == 3:
            ring = (86, 90, 88, 83)[bar // 4]
            mix.add(glass[ring], at + 3 * BEAT, .13, .6 if bar % 8 == 3 else -.6)
        elif bar in (0, 8):
            mix.add(glass[83], at, .085, -.5)

    return mix.as_wav()


def main():
    data = compose()
    digest = hashlib.sha256(data).hexdigest()
    if sys.argv[1:] == ["--check"]:
        if not OUT.is_file() or OUT.read_bytes() != data:
            raise SystemExit(f"{OUT} differs from tools/compose_music.py; regenerate it")
        print(f"OK: original 16-bar stereo WAV, {FRAMES / RATE:.3f}s, sha256={digest}")
    elif len(sys.argv) == 1:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_bytes(data)
        print(f"Wrote {OUT.relative_to(ROOT)} ({len(data)} bytes, {FRAMES / RATE:.3f}s, sha256={digest})")
    else:
        raise SystemExit("Usage: python3 tools/compose_music.py [--check]")


if __name__ == "__main__":
    main()
