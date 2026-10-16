#!/usr/bin/env python3
"""Synthesizes the video's background music: an original 80s synth-pop
instrumental in A minor at 112 BPM, written for this video.

  Drums   a LinnDrum-style kick, a gated-reverb snare on 2 and 4 and
          eighth-note hats
  Bass    a pulse-wave octave bass on eighths
  Pads    detuned saw chords, Am - F - C - G
  Arp     sixteenth-note arpeggios over the chords
  Lead    a square-wave melody with vibrato and an echo, in the chorus

Usage: make_music.py out.wav seconds
The song is sections of 8 bars (intro, verse, chorus, ...) repeated to fill
the length, then fades over the last 4 seconds. Python standard library
only, like demo/vortex/tools/make_music.py.
"""
import math
import random
import struct
import sys
import wave
from array import array

RATE = 44100
BPM = 112
BEAT = 60.0 / BPM
BAR = 4 * BEAT
random.seed(1987)


def midi_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12.0)


# The chords, one per bar (MIDI notes of the triad, root position).
CHORDS = {
    'Am': [57, 60, 64], 'F': [53, 57, 60], 'C': [48, 52, 55], 'G': [55, 59, 62],
    'Dm': [50, 53, 57], 'E': [52, 56, 59],
}
VERSE = ['Am', 'F', 'C', 'G', 'Am', 'F', 'C', 'G']
CHORUS = ['F', 'G', 'Am', 'Am', 'F', 'G', 'C', 'E']
BRIDGE = ['Dm', 'Am', 'F', 'C', 'Dm', 'Am', 'F', 'E']

# The chorus melody: (bar, beat, length in beats, MIDI note).
HOOK = [
    (0, 0, 1.5, 72), (0, 1.5, .5, 74), (0, 2, 1, 76), (0, 3, 1, 72),
    (1, 0, 1.5, 74), (1, 1.5, .5, 76), (1, 2, 2, 79),
    (2, 0, 1, 76), (2, 1, 1, 74), (2, 2, 1, 72), (2, 3, 1, 69),
    (3, 0, 3, 72), (3, 3, 1, 71),
    (4, 0, 1.5, 72), (4, 1.5, .5, 74), (4, 2, 1, 76), (4, 3, 1, 81),
    (5, 0, 1.5, 79), (5, 1.5, .5, 76), (5, 2, 2, 74),
    (6, 0, 1, 72), (6, 1, 1, 74), (6, 2, 1, 76), (6, 3, 1, 79),
    (7, 0, 2, 76), (7, 2, 2, 80),
]


class Track:
    """A float sample buffer that notes are mixed into."""

    def __init__(self, seconds):
        self.n = int(seconds * RATE) + RATE
        self.buf = array('f', bytes(4 * self.n))

    def add(self, start, samples, gain=1.0):
        i0 = int(start * RATE)
        buf = self.buf
        for k, v in enumerate(samples):
            i = i0 + k
            if i >= self.n:
                break
            buf[i] += v * gain


def env(n, attack, release, total):
    """Linear attack, sustain, linear release envelope value at sample n."""
    a = int(attack * RATE)
    r = int(release * RATE)
    if n < a:
        return n / a
    if n > total - r:
        return max(0.0, (total - n) / r)
    return 1.0


def saw(phase):
    return 2.0 * (phase - math.floor(phase + .5))


def pulse(phase, width):
    return 1.0 if (phase % 1.0) < width else -1.0


def kick():
    out = []
    n = int(.32 * RATE)
    ph = 0.0
    for i in range(n):
        t = i / RATE
        f = 48 + 110 * math.exp(-t * 32)
        ph += f / RATE
        out.append(math.sin(2 * math.pi * ph) * math.exp(-t * 9) * (1.0 if i > 20 else i / 20))
    return out


def snare():
    """Noise plus a tone, held at full level then cut: the gated 80s snare."""
    out = []
    n = int(.26 * RATE)
    lp = 0.0
    for i in range(n):
        t = i / RATE
        noise = random.uniform(-1, 1)
        lp += .45 * (noise - lp)
        tone = math.sin(2 * math.pi * 185 * t) * math.exp(-t * 30)
        gate = 1.0 if t < .17 else max(0.0, 1 - (t - .17) / .03)
        body = math.exp(-t * 4.5) * gate
        out.append((noise - .6 * lp) * .7 * body + tone * .6)
    return out


def hat(open_=False):
    out = []
    n = int((.12 if open_ else .035) * RATE)
    prev = 0.0
    for i in range(n):
        t = i / RATE
        noise = random.uniform(-1, 1)
        hp = noise - prev
        prev = noise
        out.append(hp * math.exp(-t * (18 if open_ else 90)))
    return out


def bass_note(note, length):
    n = int(length * RATE)
    f = midi_hz(note)
    out = []
    lp = 0.0
    for i in range(n):
        ph = f * i / RATE
        v = pulse(ph, .3) * .6 + saw(ph * .5) * .4
        lp += .18 * (v - lp)
        out.append(lp * env(i, .004, .03, n))
    return out


def pad_chord(notes, length):
    n = int(length * RATE)
    freqs = []
    for note in notes:
        f = midi_hz(note)
        freqs += [f * .997, f * 1.004]
    out = []
    lp = 0.0
    for i in range(n):
        t = i / RATE
        v = sum(saw(f * t) for f in freqs) / len(freqs)
        cut = .05 + .03 * math.sin(2 * math.pi * .25 * t)
        lp += cut * (v - lp)
        out.append(lp * env(i, .25, .4, n))
    return out


def pluck(note, length):
    n = int(length * RATE)
    f = midi_hz(note)
    out = []
    for i in range(n):
        t = i / RATE
        v = pulse(f * t, .25) * .5 + saw(f * t * 2.001) * .3
        out.append(v * math.exp(-t * 14) * env(i, .002, .02, n))
    return out


def lead_note(note, length):
    n = int(length * RATE)
    f = midi_hz(note)
    out = []
    ph = 0.0
    lp = 0.0
    for i in range(n):
        t = i / RATE
        vib = 1 + .006 * math.sin(2 * math.pi * 5.5 * t) * min(1.0, t * 3)
        ph += f * vib / RATE
        v = pulse(ph, .5) * .5 + saw(ph * 1.003) * .5
        lp += .3 * (v - lp)
        out.append(lp * env(i, .01, .08, n))
    return out


def song_sections(seconds):
    order = [('intro', VERSE), ('verse', VERSE), ('chorus', CHORUS), ('verse', VERSE),
             ('chorus', CHORUS), ('bridge', BRIDGE), ('chorus', CHORUS), ('chorus', CHORUS)]
    # After the last section the song goes round again from the first verse.
    sections = []
    t = 0.0
    i = 0
    while t < seconds:
        name, chords = order[i] if i < len(order) else order[1 + (i - len(order)) % (len(order) - 1)]
        sections.append((t, name, chords))
        t += 8 * BAR
        i += 1
    return sections


def render(seconds):
    drums = Track(seconds)
    bass = Track(seconds)
    pads = Track(seconds)
    arp = Track(seconds)
    lead = Track(seconds)
    k, s, h, ho = kick(), snare(), hat(), hat(True)
    pad_cache, bass_cache, pluck_cache, lead_cache = {}, {}, {}, {}

    for start, name, chords in song_sections(seconds):
        print(f'  {start:6.1f}s  {name}', file=sys.stderr)
        for bar, chord in enumerate(chords):
            b0 = start + bar * BAR
            if b0 > seconds:
                break
            notes = CHORDS[chord]
            # Pads on every section.
            key = chord
            if key not in pad_cache:
                pad_cache[key] = pad_chord(notes, BAR + .3)
            pads.add(b0, pad_cache[key], .55 if name != 'intro' else .7)
            # Drums: the intro has only the kick, and later only hats.
            for beat in range(4):
                t = b0 + beat * BEAT
                if name == 'intro' and bar < 4:
                    if beat == 0:
                        drums.add(t, k, .8)
                    continue
                drums.add(t, k, .9)
                if beat in (1, 3):
                    drums.add(t, s, .55)
                for half in (0, .5):
                    if beat == 3 and half == .5 and bar % 2 == 1:
                        drums.add(t + half * BEAT, ho, .25)
                    else:
                        drums.add(t + half * BEAT, h, .22 if half else .15)
            if name == 'intro' and bar < 4:
                continue
            # Octave bass on eighths.
            root = notes[0] - 24 if notes[0] >= 55 else notes[0] - 12
            for e in range(8):
                note = root + (12 if e % 2 else 0)
                if note not in bass_cache:
                    bass_cache[note] = bass_note(note, BEAT / 2 * .9)
                bass.add(b0 + e * BEAT / 2, bass_cache[note], .5)
            # Sixteenth arpeggio, up and down the chord.
            if name in ('verse', 'chorus', 'bridge'):
                pattern = [0, 1, 2, 3, 2, 1, 0, 1] * 2
                for e, p in enumerate(pattern):
                    note = notes[p % 3] + 12 * (1 + p // 3)
                    if note not in pluck_cache:
                        pluck_cache[note] = pluck(note, BEAT / 4 * .95)
                    arp.add(b0 + e * BEAT / 4, pluck_cache[note], .18)
        # The hook in the chorus.
        if name == 'chorus':
            for bar, beat, length, note in HOOK:
                key = (note, length)
                if key not in lead_cache:
                    lead_cache[key] = lead_note(note, length * BEAT)
                lead.add(start + bar * BAR + beat * BEAT, lead_cache[key], .33)

    # A dotted-eighth echo on the lead and the arp.
    for track, delay, fb in ((lead, BEAT * .75, .35), (arp, BEAT * .75, .3)):
        d = int(delay * RATE)
        buf = track.buf
        for i in range(d, track.n):
            buf[i] += buf[i - d] * fb

    n = int(seconds * RATE)
    mix = array('f', bytes(4 * n))
    for i in range(n):
        mix[i] = (drums.buf[i] * .9 + bass.buf[i] * .8 + pads.buf[i] * .55
                  + arp.buf[i] * .8 + lead.buf[i] * .9)
    return mix


def main():
    out, seconds = sys.argv[1], float(sys.argv[2])
    mix = render(seconds)
    n = len(mix)
    fade = int(4 * RATE)
    peak = max(abs(v) for v in mix) or 1.0
    scale = .89 / peak
    frames = bytearray()
    for i in range(n):
        v = mix[i] * scale
        if i > n - fade:
            v *= (n - i) / fade
        if i < RATE // 2:
            v *= i / (RATE // 2)
        v = math.tanh(v * 1.2) / math.tanh(1.2)  # gentle saturation, like tape
        s = int(max(-1.0, min(1.0, v)) * 32767)
        frames += struct.pack('<hh', s, s)
    with wave.open(out, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print(f'wrote {out}: {seconds:.1f} s', file=sys.stderr)


if __name__ == '__main__':
    main()
