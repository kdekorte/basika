#!/usr/bin/env python3
"""Builds Vortex's background music into ../assets/music.wav.

The tune is synthesized from scratch with the Python standard library: a
16-bar synthwave loop at 150 beats a minute in A minor, with four-on-the-floor
drums, a driving octave bass and a pad that duck under each kick, fast
arpeggios with an echo, and a lead melody over the second eight bars. Every
note is mixed into one loop-length buffer, wrapping around at the end, so the
loop repeats without a seam. Run it again after editing the tune:

    python3 demo/vortex/tools/make_music.py
"""

import math
import os
import random
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "assets")

RATE = 22050
BPM = 150
STEP = 60 / BPM / 4  # one 16th note
BAR = 16 * STEP
BARS = 16
LENGTH = int(round(BARS * BAR * RATE))

# One chord a bar: root and the chord tones as semitones above it.
MINOR, MAJOR = (0, 3, 7), (0, 4, 7)
CHORDS = [("A", MINOR), ("A", MINOR), ("F", MAJOR), ("F", MAJOR),
          ("C", MAJOR), ("C", MAJOR), ("G", MAJOR), ("G", MAJOR),
          ("A", MINOR), ("A", MINOR), ("F", MAJOR), ("F", MAJOR),
          ("D", MINOR), ("D", MINOR), ("E", MAJOR), ("E", MAJOR)]

# The lead over bars 9-16, in 16th notes; "-" is a rest.
LEAD = ("A5:4 C6:2 B5:2 A5:4 E5:4 "
        "G5:2 A5:2 B5:4 C6:4 B5:4 "
        "A5:4 F5:4 C6:6 A5:2 "
        "G5:4 F5:4 E5:4 C5:4 "
        "D6:4 C6:2 A5:2 F5:4 A5:4 "
        "C6:4 D6:4 F6:4 E6:4 "
        "E6:6 D6:2 B5:4 G#5:4 "
        "B5:4 G#5:4 E5:6 - -").split()

NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def midi(name, octave):
    return NAMES[name] + 12 * (octave + 1)


def note_midi(text):
    return NAMES[text[:-1]] + 12 * (int(text[-1]) + 1)


def freq(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def at(step):
    """The sample a 16th-note step starts on."""
    return int(round(step * STEP * RATE))


def add(track, start, samples, gain=1.0):
    """Mixes samples into a loop-length track, wrapping past the end."""
    n = len(track)
    for i, s in enumerate(samples):
        track[(start + i) % n] += s * gain


# ---------------------------------------------------------------- voices

def envelope(i, count, attack, decay, release=0.01):
    t = i / RATE
    e = min(1.0, t / attack) if attack > 0 else 1.0
    e *= math.exp(-decay * t)
    return e * min(1.0, (count - i) / (release * RATE))


def oscillator(f, seconds, shape, attack=0.003, decay=0.0, duty=0.5, detune=(0.0,), vibrato=0.0):
    count = int(seconds * RATE)
    phases = [random.random() for _ in detune]
    out = []
    for i in range(count):
        t = i / RATE
        wobble = 1 + vibrato * math.sin(2 * math.pi * 5.5 * t) * min(1.0, t / 0.15)
        value = 0.0
        for k, cents in enumerate(detune):
            phases[k] = (phases[k] + f * wobble * 2 ** (cents / 1200) / RATE) % 1.0
            p = phases[k]
            if shape == "saw":
                value += 2 * p - 1
            elif shape == "square":
                value += 1.0 if p < duty else -1.0
            elif shape == "triangle":
                value += 4 * abs(p - 0.5) - 1
            else:
                value += math.sin(2 * math.pi * p)
        out.append(value / len(detune) * envelope(i, count, attack, decay))
    return out


def lowpass(samples, cutoff):
    a = 1 - math.exp(-2 * math.pi * cutoff / RATE)
    y, out = 0.0, []
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out


def kick():
    count = int(0.28 * RATE)
    phase, out = 0.0, []
    for i in range(count):
        t = i / RATE
        f = 45 + 110 * math.exp(-t * 30)
        phase += f / RATE
        out.append(math.sin(2 * math.pi * phase) * math.exp(-t * 11) * min(1.0, (count - i) / 80))
    click = [random.uniform(-1, 1) * math.exp(-i / RATE * 400) * 0.3 for i in range(200)]
    for i, c in enumerate(click):
        out[i] += c
    return out


def snare():
    count = int(0.22 * RATE)
    body = oscillator(185, 0.22, "triangle", attack=0, decay=28)
    hiss = lowpass([random.uniform(-1, 1) for _ in range(count)], 6000)
    return [0.45 * b + 0.8 * h * math.exp(-i / RATE * 16) for i, (b, h) in enumerate(zip(body, hiss))]


def hat(length):
    count = int(length * RATE)
    raw = [random.uniform(-1, 1) for _ in range(count)]
    low = lowpass(raw, 5000)
    return [(r - l) * math.exp(-i / RATE * (60 if length < 0.1 else 18)) for i, (r, l) in enumerate(zip(raw, low))]


# ---------------------------------------------------------------- parts

def drums():
    track = [0.0] * LENGTH
    k, s = kick(), snare()
    closed, open_hat = hat(0.05), hat(0.2)
    for bar in range(BARS):
        b = bar * 16
        for beat in range(4):
            add(track, at(b + beat * 4), k, 0.9)
        last_bar = bar % 8 == 7
        add(track, at(b + 4), s, 0.6)
        if last_bar:
            # A fill into the next eight bars.
            add(track, at(b + 12), s, 0.6)
            for n in (13, 14, 15):
                add(track, at(b + n), s, 0.25 + 0.12 * (n - 12))
        else:
            add(track, at(b + 12), s, 0.6)
        for n in range(16):
            if last_bar and n >= 12:
                continue
            if n % 4 == 2:
                add(track, at(b + n), open_hat if n == 14 else closed, 0.35)
            else:
                add(track, at(b + n), closed, 0.12 if n % 2 else 0.18)
    return track


def sidechain(track, depth):
    """Ducks a track after every beat, as if pushed aside by the kick."""
    beat = 4 * STEP * RATE
    for i in range(len(track)):
        since = (i % beat) / RATE
        track[i] *= 1 - depth * math.exp(-since / 0.09)
    return track


def bass():
    track = [0.0] * LENGTH
    octave_pattern = (0, 0, 12, 0, 0, 12, 0, 12, 0, 0, 12, 0, 0, 12, 0, 12)
    for bar, (root, _) in enumerate(CHORDS):
        base = midi(root, 1) if NAMES[root] >= NAMES["F"] else midi(root, 2)
        for n, jump in enumerate(octave_pattern):
            voice = oscillator(freq(base + jump), STEP * 0.9, "saw", decay=9, detune=(-6, 6))
            add(track, at(bar * 16 + n), lowpass(voice, 900), 0.55)
    return sidechain(track, 0.6)


def pad():
    track = [0.0] * LENGTH
    for bar, (root, tones) in enumerate(CHORDS):
        base = midi(root, 3)
        for tone in tones + (12,):
            voice = oscillator(freq(base + tone), BAR + 0.05, "saw", attack=0.12, detune=(-9, 0, 9))
            add(track, at(bar * 16), lowpass(voice, 1400), 0.09)
    return sidechain(track, 0.5)


def echo(track, delay_steps, feedback):
    """A feedback delay that wraps around the loop, so the echoes do too."""
    d = at(delay_steps)
    out = list(track)
    for _ in range(2):  # twice around, so echoes cross the loop point
        for i in range(len(out)):
            out[i] = track[i] + feedback * out[i - d]
    return out


def arpeggio():
    track = [0.0] * LENGTH
    order = (0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1, 2, 3)
    for bar, (root, tones) in enumerate(CHORDS):
        base = midi(root, 4)
        notes = [base + t for t in tones] + [base + 12, base + tones[1] + 12]
        loud = 0.16 if bar < 8 else 0.11  # it steps back under the lead
        for n, k in enumerate(order):
            voice = oscillator(freq(notes[k]), STEP * 0.8, "square", decay=14, duty=0.25)
            add(track, at(bar * 16 + n), lowpass(voice, 4500), loud)
    return echo(track, 3, 0.35)


def lead():
    track = [0.0] * LENGTH
    step = 8 * 16
    for item in LEAD:
        name, _, length = item.partition(":")
        steps = int(length or 1)
        if name != "-":
            seconds = steps * STEP * 0.95
            voice = mix(oscillator(freq(note_midi(name)), seconds, "square", attack=0.01, decay=1.2,
                                   duty=0.5, vibrato=0.004),
                        oscillator(freq(note_midi(name)), seconds, "saw", attack=0.01, decay=1.2,
                                   detune=(-8, 8), vibrato=0.004))
            add(track, at(step), lowpass(voice, 3200), 0.17)
        step += steps
    return echo(track, 6, 0.3)


def riser():
    """White noise sweeping up over the last two beats of each half."""
    track = [0.0] * LENGTH
    count = at(8)
    for half in (0, 8):
        start = at((half + 8) * 16) - count
        raw = [random.uniform(-1, 1) for _ in range(count)]
        y, out = 0.0, []
        for i, r in enumerate(raw):
            progress = i / count
            a = 1 - math.exp(-2 * math.pi * (400 + 7000 * progress) / RATE)
            y += a * (r - y)
            out.append(y * progress ** 2)
        add(track, start, out, 0.25)
    return track


def mix(*tracks):
    return [sum(values) for values in zip(*tracks)]


def write_wav(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    gain = 0.9 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * gain)) * 32767)) for s in samples))


if __name__ == "__main__":
    random.seed(1981)
    os.makedirs(ASSETS, exist_ok=True)
    song = mix(drums(), bass(), pad(), arpeggio(), lead(), riser())
    path = os.path.join(ASSETS, "music.wav")
    write_wav(path, song)
    print("wrote", os.path.relpath(path), f"({LENGTH / RATE:.1f} s loop)")
