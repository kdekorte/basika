#!/usr/bin/env python3
"""Builds the Jukebox songs into ../assets/*.wav.

Three public domain tunes, each arranged for its own synthesized instrument
over a soft bass and chord accompaniment, using only the Python standard
library:

    happy_birthday.wav   Happy Birthday to You (Mildred and Patty Hill, 1893),
                         twice through on a music box
    ode_to_joy.wav       Ode to Joy (Ludwig van Beethoven, 1824) on an organ
    greensleeves.wav     Greensleeves (traditional, 16th century) on a
                         plucked lute

Run it again after editing a tune:

    python3 demo/jukebox/tools/make_songs.py
"""

import math
import os
import random
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "assets")

RATE = 22050
LEAD_IN = 0.4  # seconds of silence before the first note
TAIL = 2.0  # seconds after the last beat for the notes to ring out

NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
QUALITIES = {"": (0, 4, 7), "m": (0, 3, 7), "7": (0, 4, 7, 10)}


def note_midi(text):
    return NAMES[text[:-1]] + 12 * (int(text[-1]) + 1)


def freq(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def parse_melody(text):
    """'G4:.75 G4:.25 A4:1 R:1' -> [(midi or None, beats), ...]"""
    notes = []
    for item in text.split():
        name, beats = item.split(":")
        notes.append((None if name == "R" else note_midi(name), float(beats)))
    return notes


def parse_chord(text):
    """'Am' -> (root midi in octave 3, intervals)"""
    root = text[:2] if len(text) > 1 and text[1] == "#" else text[:1]
    return NAMES[root] + 48, QUALITIES[text[len(root):]]


# ---------------------------------------------------------------- voices

def env(i, count, attack, decay, release=0.03):
    t = i / RATE
    e = min(1.0, t / attack) if attack > 0 else 1.0
    e *= math.exp(-decay * t)
    return e * min(1.0, (count - i) / (release * RATE))


def music_box(f, seconds):
    """A bell-like tine: a sine with inharmonic partials that fade fast."""
    n = int((seconds + 1.2) * RATE)
    out = []
    for i in range(n):
        t = i / RATE
        e = env(i, n, 0.002, 2.2)
        s = math.sin(2 * math.pi * f * t)
        s += 0.35 * math.sin(2 * math.pi * f * 3.01 * t) * math.exp(-6 * t)
        s += 0.15 * math.sin(2 * math.pi * f * 5.43 * t) * math.exp(-11 * t)
        out.append(s * e)
    return out


def organ(f, seconds):
    """Drawbar organ: octave-stacked sines with a gentle vibrato, held for the note."""
    n = int((seconds + 0.08) * RATE)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / RATE
        phase += 2 * math.pi * f * (1 + 0.003 * math.sin(2 * math.pi * 5.5 * t)) / RATE
        s = math.sin(phase) + 0.5 * math.sin(2 * phase) + 0.3 * math.sin(3 * phase) + 0.15 * math.sin(4 * phase)
        e = min(1.0, t / 0.02) * min(1.0, (n - i) / (0.07 * RATE))
        out.append(s * e * 0.55)
    return out


def lute(f, seconds):
    """A plucked string (Karplus-Strong): noise in a delay line that averages itself."""
    n = int((seconds + 1.5) * RATE)
    period = max(2, int(round(RATE / f)))
    line = [random.uniform(-1, 1) for _ in range(period)]
    out = []
    for i in range(n):
        j = i % period
        s = line[j]
        line[j] = 0.5 * (s + line[(j + 1) % period]) * 0.996
        out.append(s * min(1.0, (n - i) / (0.05 * RATE)))
    return out


def soft_tone(f, seconds, decay):
    """Accompaniment: a round sine with a little second harmonic."""
    n = int((seconds + 0.5) * RATE)
    return [(math.sin(2 * math.pi * f * i / RATE) + 0.25 * math.sin(4 * math.pi * f * i / RATE)) * env(i, n, 0.005, decay)
            for i in range(n)]


# ---------------------------------------------------------------- arranging

def add(track, start, samples, gain=1.0):
    end = min(len(track), start + len(samples))
    for i in range(start, end):
        track[i] += samples[i - start] * gain


def render(song):
    beat = 60 / song["bpm"]
    per_bar = song["meter"]
    melody = parse_melody(song["melody"])
    chords = song["chords"].split("|")
    beats = max(sum(b for _, b in melody), song["pickup"] + len(chords) * per_bar)
    track = [0.0] * int((LEAD_IN + beats * beat + TAIL) * RATE)

    def at(b):
        return int((LEAD_IN + b * beat) * RATE)

    pos = 0.0
    for m, b in melody:
        if m is not None:
            add(track, at(pos), song["voice"](freq(m), b * beat * 0.95), 0.6)
        pos += b

    # Bass on the downbeat (and beat 3 in 4/4), chord tones on the other beats.
    for bar, text in enumerate(chords):
        names = text.split()
        start = song["pickup"] + bar * per_bar
        for k in range(per_bar):
            root, quality = parse_chord(names[min(len(names) - 1, k * len(names) // per_bar)])
            b = start + k
            if k == 0 or (per_bar == 4 and k == 2):
                bass = root - 12 if k == 0 else root - 5
                add(track, at(b), soft_tone(freq(bass), beat, 2.5), 0.45)
            else:
                for iv in quality[:3]:
                    add(track, at(b), soft_tone(freq(root + 12 + iv), beat * 0.6, 7), 0.11)
    return track


HAPPY_PASS = ("G4:.75 G4:.25 A4:1 G4:1 C5:1 B4:2 "
              "G4:.75 G4:.25 A4:1 G4:1 D5:1 C5:2 "
              "G4:.75 G4:.25 G5:1 E5:1 C5:1 B4:1 A4:1 "
              "F5:.75 F5:.25 E5:1 C5:1 D5:1 ")

SONGS = {
    "happy_birthday.wav": {
        "voice": music_box, "bpm": 100, "meter": 3, "pickup": 1,
        "melody": HAPPY_PASS + "C5:2 " + HAPPY_PASS + "C5:3",
        "chords": "|".join(["C", "G7", "G7", "C", "C", "F", "C G7", "C"] * 2),
    },
    "ode_to_joy.wav": {
        "voice": organ, "bpm": 120, "meter": 4, "pickup": 0,
        "melody": ("E5:1 E5:1 F5:1 G5:1 G5:1 F5:1 E5:1 D5:1 C5:1 C5:1 D5:1 E5:1 E5:1.5 D5:.5 D5:2 "
                   "E5:1 E5:1 F5:1 G5:1 G5:1 F5:1 E5:1 D5:1 C5:1 C5:1 D5:1 E5:1 D5:1.5 C5:.5 C5:2 "
                   "D5:1 D5:1 E5:1 C5:1 D5:1 E5:.5 F5:.5 E5:1 C5:1 "
                   "D5:1 E5:.5 F5:.5 E5:1 D5:1 C5:1 D5:1 G4:2 "
                   "E5:1 E5:1 F5:1 G5:1 G5:1 F5:1 E5:1 D5:1 C5:1 C5:1 D5:1 E5:1 D5:1.5 C5:.5 C5:4"),
        "chords": "C|G|C|G|C|G|C|G C|G|C|G|C G|C|G|C|G C|C",
    },
    "greensleeves.wav": {
        "voice": lute, "bpm": 132, "meter": 3, "pickup": 1,
        "melody": ("A4:1 "
                   "C5:2 D5:1 E5:1.5 F5:.5 E5:1 D5:2 B4:1 G4:1.5 A4:.5 B4:1 "
                   "C5:2 A4:1 A4:1.5 G#4:.5 A4:1 B4:2 G#4:1 E4:2 A4:1 "
                   "C5:2 D5:1 E5:1.5 F5:.5 E5:1 D5:2 B4:1 G4:1.5 A4:.5 B4:1 "
                   "C5:1.5 B4:.5 A4:1 G#4:1.5 F#4:.5 G#4:1 A4:3 A4:2 R:1 "
                   "G5:3 G5:1.5 F#5:.5 E5:1 D5:2 B4:1 G4:1.5 A4:.5 B4:1 "
                   "C5:2 A4:1 A4:1.5 G#4:.5 A4:1 B4:2 G#4:1 E4:2 R:1 "
                   "G5:3 G5:1.5 F#5:.5 E5:1 D5:2 B4:1 G4:1.5 A4:.5 B4:1 "
                   "C5:1.5 B4:.5 A4:1 G#4:1.5 F#4:.5 G#4:1 A4:3 A4:3"),
        "chords": ("Am|C|G|Em|Am|F|E|E|Am|C|G|Em|Am|E|Am|Am|"
                   "C|C|G|Em|Am|F|E|E|C|C|G|Em|Am|E|Am|Am"),
    },
}


def write_wav(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    gain = 0.85 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * gain)) * 32767)) for s in samples))


if __name__ == "__main__":
    random.seed(1893)
    os.makedirs(ASSETS, exist_ok=True)
    for name, song in SONGS.items():
        samples = render(song)
        write_wav(os.path.join(ASSETS, name), samples)
        print(f"{name}: {len(samples) / RATE:.1f} s")
