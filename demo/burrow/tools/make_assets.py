#!/usr/bin/env python3
"""Builds Burrow's sprite sheet and sounds into ../assets.

Everything is made here from scratch with the Python standard library: the
sprites are drawn as text grids below, and the sounds are synthesized from
square, triangle and noise waves. Run it again after editing them:

    python3 demo/burrow/tools/make_assets.py
"""

import math
import os
import random
import struct
import wave
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "assets")

# ---------------------------------------------------------------- sprites

PALETTE = {
    ".": None,
    "k": (24, 20, 32),     # outline
    "w": (244, 244, 240),  # white
    "y": (250, 208, 60),   # helmet yellow
    "Y": (196, 150, 30),   # helmet shade
    "c": (170, 240, 255),  # lamp
    "s": (250, 196, 150),  # skin
    "S": (210, 150, 110),  # skin shade
    "b": (64, 120, 228),   # overalls
    "B": (34, 62, 140),    # overalls shade
    "g": (160, 164, 176),  # metal
    "l": (220, 224, 232),  # metal light
    "p": (176, 84, 214),   # blob
    "P": (112, 44, 150),   # blob shade
    "h": (250, 150, 200),  # blob cheeks
    "e": (80, 196, 110),   # drake
    "E": (34, 118, 64),    # drake shade
    "r": (226, 56, 50),    # red
    "R": (140, 24, 30),    # dark red
    "f": (255, 244, 140),  # flame core
    "F": (255, 150, 40),   # flame
    "G": (96, 92, 104),    # rock shade
    "m": (138, 134, 146),  # rock
    "M": (186, 182, 192),  # rock light
    "n": (120, 76, 44),    # brown
    "o": (250, 124, 40),   # orange
}

DIGGER_RIGHT = [
    "................",
    ".....kkkkkk.....",
    "....kyyyyyyk....",
    "...kyyyyyyyykk..",
    "...kYyyyyyyykck.",
    "...kkkkkkkkkkk..",
    "....kssssssk....",
    "....kssskssk....",
    "....kSsssssk....",
    "...kkbbbbbbkk...",
    "..kbbbbbbbbbkkk.",
    "..kbbkbbbbbbkglk",
    "...kbbbbbbbbkkk.",
    "....kbbBBbbk....",
    "...kkk....kkk...",
    "...kkk....kkk...",
]

DIGGER_RIGHT_STEP = DIGGER_RIGHT[:13] + [
    "....kbbbbbbk....",
    ".....kkkkkk.....",
    ".....kkkkkk.....",
]

DIGGER_RIGHT_PUMP = DIGGER_RIGHT[:9] + [
    "...kkbbbbbbkk...",
    "..kbbbbbbbbbbkkk",
    "..kbbkbbbbbbbglk",
    "...kbbbbbbbbbkkk",
    "....kbbBBbbk....",
    "...kkk....kkk...",
    "...kkk....kkk...",
]

BLOB = [
    "................",
    "................",
    ".....kkkkkk.....",
    "...kkppppppkk...",
    "..kppppppppppk..",
    ".kpppwwppwwpppk.",
    ".kppwwkpwwkpppk.",
    ".kppwwkpwwkpppk.",
    ".kpppwwppwwpppk.",
    ".khhpppppppphhk.",
    ".kpppppkkppppPk.",
    "..kppppppppPPk..",
    "...kkPPPPPPkk...",
    "...kpk....kpk...",
    "...kkk....kkk...",
    "................",
]

BLOB_STEP = BLOB[:13] + [
    "....kpk..kpk....",
    "....kkk..kkk....",
    "................",
]

EYES = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "....www..www....",
    "...wwkw.wwkw....",
    "...wwkw.wwkw....",
    "....www..www....",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
]

EYES_LOOK = [row.replace("wwkw", "wkww") for row in EYES]

DRAKE = [
    "................",
    "................",
    "..........kkkk..",
    ".........keeeek.",
    "........keewkeek",
    "...kk...keeeeeek",
    "..keek.keeeekkkk",
    ".keEEekeeeeeerk.",
    "keEeeeeeeeeekkk.",
    "kEeeeeeeeeeeek..",
    ".kEEeeeeeeeeek..",
    "..kEEEEEEEEEk...",
    "...kek...kek....",
    "...kEk...kEk....",
    "...kkk...kkk....",
    "................",
]

DRAKE_STEP = DRAKE[:12] + [
    "....kek.kek.....",
    "....kEk.kEk.....",
    "....kkk.kkk.....",
    "................",
]

DRAKE_BREATHE = [
    "................",
    "................",
    "..........kkkk..",
    ".........keeeek.",
    "........keewkeek",
    "...kk...keeeeeek",
    "..keek.keeeeeekk",
    ".keEEekeeeeeekrr",
    "keEeeeeeeeeekkrr",
    "kEeeeeeeeeeeeekk",
    ".kEEeeeeeeeeek..",
    "..kEEEEEEEEEk...",
    "...kek...kek....",
    "...kEk...kEk....",
    "...kkk...kkk....",
    "................",
]

FLAME = [
    "................",
    "................",
    "................",
    "..........FF....",
    "....FF...FFFF...",
    "..FFFFFFFFffFF..",
    ".FFffffffffffFF.",
    "FFfffwwwwfffffFF",
    "FFfffwwwwfffffFF",
    ".FFffffffffffFF.",
    "..FFFFFffFFFFF..",
    "....FF..FFFF....",
    "..........FF....",
    "................",
    "................",
    "................",
]

FLAME_FLICKER = [
    "................",
    "................",
    "................",
    "...FF...........",
    "..FFFF....FF....",
    ".FFffFFFFFFFFF..",
    "FFfffffffffffFF.",
    "FFffffwwwwffffFF",
    "FFffffwwwwffffFF",
    "FFfffffffffffFF.",
    ".FFffFFFFFFFFF..",
    "..FFFF....FF....",
    "...FF...........",
    "................",
    "................",
    "................",
]

ROCK = [
    "................",
    "................",
    ".....kkkkkk.....",
    "...kkMMMMmmkk...",
    "..kMMMMmmmmmmk..",
    ".kMMMmmmmmmmmGk.",
    ".kMMmmmmmmGmmGk.",
    "kMmmmmmmmmmmmmGk",
    "kmmmmGmmmmmmmmGk",
    "kmmmmmmmmmmmGGGk",
    "kmmmmmmmmmmmmGGk",
    ".kmmmmmGmmmmGGk.",
    ".kGmmmmmmmmGGGk.",
    "..kGGGmmmGGGGk..",
    "...kkkkkkkkkk...",
    "................",
]

ROCK_CRACKED = [
    "................",
    "................",
    ".....kkkkkk.....",
    "...kkMMMkmmkk...",
    "..kMMMMmkmmmmk..",
    ".kMMMmmmmkmmmGk.",
    ".kMMmmmmmkGmmGk.",
    "kMmmmmmmkmmmmmGk",
    "kmmmkkkkmmmmmmGk",
    "kmmkmmmmkmmmGGGk",
    "kmmmmmmmmkkmmGGk",
    ".kmmmmmGmmmkGGk.",
    ".kGmmmmmmmmGkGk.",
    "..kGGGmmmGGGGk..",
    "...kkkkkkkkkk...",
    "................",
]

RUBBLE = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..kkk......kk...",
    ".kMmGk..kkkmGk..",
    ".kmmGk.kMmmGk...",
    "..kkkkkmmmGGk.kk",
    "...kMmmGkkkk.kGk",
    "..kkkkkkk....kkk",
    "................",
]

BURST = [
    "................",
    ".......w........",
    "...w...w...w....",
    "....w..w..w.....",
    ".....w.w.w......",
    "......www.......",
    ".wwwwwwfwwwwww..",
    "......www.......",
    ".....w.w.w......",
    "....w..w..w.....",
    "...w...w...w....",
    ".......w........",
    "................",
    "................",
    "................",
    "................",
]

MUSHROOM = [
    "................",
    "................",
    ".....kkkkkk.....",
    "...kkrrwwrrkk...",
    "..krrrrwwrrrrk..",
    ".krwwrrrrrrwwrk.",
    ".krwwrrrrrrwwrk.",
    "krrrrrrwwrrrrrrk",
    "kRRrrrrwwrrrrRRk",
    ".kkRRRRRRRRRRkk.",
    "...kkwwwwwwkk...",
    "....kwwwkwwk....",
    "....kwwwwwwk....",
    "....kwwkwwwk....",
    ".....kkkkkk.....",
    "................",
]

FLOWER = [
    "................",
    "................",
    "................",
    "................",
    ".....kk.kk......",
    "....krrkrrk.....",
    "....krryrrk.....",
    ".....krrrk......",
    "......kEk.......",
    "...kk.kEk.......",
    "..kEEkkEk.......",
    "...kkEEEk.......",
    "......kEk.......",
    "......kEk.......",
    "................",
    "................",
]


def rotate_left(grid):
    """The picture turned a quarter turn counterclockwise."""
    size = len(grid)
    return ["".join(grid[x][size - 1 - y] for x in range(size)) for y in range(size)]


def rotate_right(grid):
    """The picture turned a quarter turn clockwise."""
    size = len(grid)
    return ["".join(grid[size - 1 - x][y] for x in range(size)) for y in range(size)]


# Sheet cells, 8 to a row, in the order burrow.bas expects.
CELLS = [
    DIGGER_RIGHT, DIGGER_RIGHT_STEP, DIGGER_RIGHT_PUMP,                      # 0-2 right
    rotate_left(DIGGER_RIGHT), rotate_left(DIGGER_RIGHT_STEP), rotate_left(DIGGER_RIGHT_PUMP),     # 3-5 up
    rotate_right(DIGGER_RIGHT), rotate_right(DIGGER_RIGHT_STEP), rotate_right(DIGGER_RIGHT_PUMP),  # 6-8 down
    BLOB, BLOB_STEP, EYES, EYES_LOOK,                                        # 9-12
    DRAKE, DRAKE_STEP, DRAKE_BREATHE,                                        # 13-15
    FLAME, FLAME_FLICKER,                                                    # 16-17
    ROCK, ROCK_CRACKED, RUBBLE,                                              # 18-20
    BURST, MUSHROOM, FLOWER,                                                 # 21-23
]
SCALE = 2
CELL = 16 * SCALE
PER_ROW = 8


def write_png(path, width, height, rgba):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def build_sheet():
    rows = (len(CELLS) + PER_ROW - 1) // PER_ROW
    width, height = PER_ROW * CELL, rows * CELL
    rgba = bytearray(width * height * 4)
    for index, grid in enumerate(CELLS):
        assert len(grid) == 16 and all(len(row) == 16 for row in grid), index
        ox, oy = (index % PER_ROW) * CELL, (index // PER_ROW) * CELL
        for y, row in enumerate(grid):
            for x, key in enumerate(row):
                color = PALETTE[key]
                if color is None:
                    continue
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        at = ((oy + y * SCALE + dy) * width + ox + x * SCALE + dx) * 4
                        rgba[at:at + 4] = bytes(color) + b"\xff"
    write_png(os.path.join(ASSETS, "sprites.png"), width, height, rgba)


# ---------------------------------------------------------------- sounds

RATE = 22050


def note(name):
    """Frequency of a note such as "C4" or "F#5"; "-" is a rest."""
    if name == "-":
        return 0
    names = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
    semitone = names[name[:-1]] + 12 * (int(name[-1]) + 1)
    return 440.0 * 2 ** ((semitone - 69) / 12)


def tone(freq, seconds, volume=0.3, wave_shape="square", duty=0.5, decay=4.0, slide=0.0):
    """One note; slide bends the pitch by that many octaves over the note."""
    samples = []
    phase = 0.0
    count = int(seconds * RATE)
    for i in range(count):
        t = i / RATE
        f = freq * 2 ** (slide * i / max(count, 1))
        phase = (phase + f / RATE) % 1.0
        if freq <= 0:
            value = 0.0
        elif wave_shape == "square":
            value = 1.0 if phase < duty else -1.0
        elif wave_shape == "triangle":
            value = 4 * abs(phase - 0.5) - 1
        else:
            value = math.sin(2 * math.pi * phase)
        envelope = math.exp(-decay * t) * min(1.0, (count - i) / (0.004 * RATE))
        samples.append(value * volume * envelope)
    return samples


def noise(seconds, volume=0.3, decay=6.0, smooth=0.0, seed=1):
    """Noise; smooth (0-0.99) makes it duller."""
    rng = random.Random(seed)
    samples, last = [], 0.0
    count = int(seconds * RATE)
    for i in range(count):
        last = last * smooth + rng.uniform(-1, 1) * (1 - smooth)
        samples.append(last * volume * math.exp(-decay * i / RATE) * min(1.0, (count - i) / (0.004 * RATE)))
    return samples


def mix(*tracks):
    length = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(length)]


def sequence(notes, step, **options):
    """Notes of `step` seconds each; a note such as "C5:2" lasts two steps."""
    out = []
    for item in notes:
        name, _, length = item.partition(":")
        out += tone(note(name), step * int(length or 1), **options)
    return out


def write_wav(name, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    gain = min(1.0, 0.9 / peak)
    with wave.open(os.path.join(ASSETS, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * gain)) * 32767)) for s in samples))


def build_sounds():
    step = 0.15
    # The digging tune: it loops while the digger moves, so it ends where it starts.
    melody = ("E5 G5 A5 G5 E5 D5 C5 D5 "
              "F5 A5 C6 A5 F5 E5 D5 E5 "
              "G5 B5 D6 B5 G5 F5 E5 D5 "
              "C5 E5 G5 E5 C5:2 - -").split()
    bass = ("C3 - G2 - C3 - G2 - "
            "F2 - C3 - F2 - C3 - "
            "G2 - D3 - G2 - D3 - "
            "C3 - G2 - C3 - - -").split()
    write_wav("walk.wav", mix(sequence(melody, step, volume=0.22, duty=0.25, decay=6),
                              sequence(bass, step, volume=0.3, wave_shape="triangle", decay=3)))

    write_wav("start.wav", mix(
        sequence("C5 E5 G5 C6 - G5 C6:2 E6:3 - -".split(), 0.11, volume=0.25, duty=0.25, decay=3),
        sequence("C3 - G3 - C4 - G3 C4:2 C3:3 - -".split(), 0.11, volume=0.3, wave_shape="triangle", decay=2)))
    write_wav("clear.wav", mix(
        sequence("G5 C6 E6 G6:2 E6 G6:4 - -".split(), 0.1, volume=0.25, duty=0.25, decay=2),
        sequence("C4 E4 G4 C5:2 G4 C5:4 - -".split(), 0.1, volume=0.25, wave_shape="triangle", decay=2)))
    write_wav("die.wav", sequence("B5 A#5 A5 G#5 G5 F#5 F5 E5 D#5 D5 C#5 C5:4".split(), 0.09,
                                  volume=0.25, duty=0.5, decay=2, slide=-0.05))
    write_wav("over.wav", mix(
        sequence("E5:2 D5:2 C5:2 B4:2 A4:2 G4:4 C4:6".split(), 0.12, volume=0.22, duty=0.25, decay=1.5),
        sequence("C3:4 G2:4 A2:4 E2:4 C2:6".split(), 0.12, volume=0.3, wave_shape="triangle", decay=1)))
    write_wav("extra.wav", sequence("C6 E6 G6 C7 G6 C7".split(), 0.06, volume=0.22, duty=0.25, decay=5))
    write_wav("bonus.wav", sequence("G6 B6 D7 G7".split(), 0.05, volume=0.2, duty=0.25, decay=6))
    write_wav("harpoon.wav", tone(500, 0.1, volume=0.25, duty=0.25, decay=10, slide=1.5))
    write_wav("pump.wav", mix(noise(0.09, volume=0.35, decay=25, smooth=0.6, seed=2),
                              tone(180, 0.09, volume=0.15, decay=20, slide=0.8)))
    write_wav("pop.wav", mix(noise(0.25, volume=0.5, decay=18, smooth=0.3, seed=3),
                             tone(900, 0.2, volume=0.25, duty=0.25, decay=12, slide=-2)))
    write_wav("fire.wav", noise(0.5, volume=0.5, decay=3, smooth=0.85, seed=4))
    write_wav("fall.wav", tone(1200, 0.6, volume=0.18, duty=0.25, decay=1, slide=-1.6))
    write_wav("crush.wav", mix(noise(0.35, volume=0.6, decay=10, smooth=0.9, seed=5),
                               tone(70, 0.35, volume=0.4, wave_shape="triangle", decay=8, slide=-0.5)))


if __name__ == "__main__":
    os.makedirs(ASSETS, exist_ok=True)
    build_sheet()
    build_sounds()
    print("wrote", ", ".join(sorted(os.listdir(ASSETS))))
