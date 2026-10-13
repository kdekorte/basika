#!/usr/bin/env python3
"""Builds Burrow's sprite sheet and sounds into ../assets.

Everything is made here from scratch with the Python standard library: the
sprites are drawn in code from simple shapes, and the sounds are synthesized
from square, triangle and noise waves. Run it again after editing them:

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
#
# Sprites are drawn in code from simple shapes on a canvas sampled 4 times
# finer than the final pixels, then averaged down, so edges are smooth. Each
# animation frame is the same drawing with different parameters (leg phase,
# bob, squash), which keeps the motion consistent.

SS = 4  # samples per pixel along each axis
# Sprites are designed on a 32-unit grid (64 for the pumped-up ones) and drawn
# SCALE pixels to the unit: the game's tiles are 48 pixels.
SCALE = 1.5
OUTLINE = (28, 22, 34)


class Ellipse:
    def __init__(self, cx, cy, rx, ry, angle=0.0):
        self.cx, self.cy, self.rx, self.ry = cx, cy, rx, ry
        self.c, self.s = math.cos(angle), math.sin(angle)

    def bbox(self, g):
        r = max(self.rx, self.ry) + g
        return self.cx - r, self.cy - r, self.cx + r, self.cy + r

    def contains(self, x, y, g=0.0):
        dx, dy = x - self.cx, y - self.cy
        u, v = dx * self.c + dy * self.s, -dx * self.s + dy * self.c
        return (u / (self.rx + g)) ** 2 + (v / (self.ry + g)) ** 2 <= 1.0


class Capsule:
    """A thick line with round ends; r1 is the radius at the far end."""

    def __init__(self, x0, y0, x1, y1, r0, r1=None):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1
        self.r0, self.r1 = r0, r0 if r1 is None else r1

    def bbox(self, g):
        r = max(self.r0, self.r1) + g
        return min(self.x0, self.x1) - r, min(self.y0, self.y1) - r, max(self.x0, self.x1) + r, max(self.y0, self.y1) + r

    def contains(self, x, y, g=0.0):
        dx, dy = self.x1 - self.x0, self.y1 - self.y0
        length2 = dx * dx + dy * dy or 1e-9
        t = max(0.0, min(1.0, ((x - self.x0) * dx + (y - self.y0) * dy) / length2))
        px, py = self.x0 + t * dx, self.y0 + t * dy
        r = self.r0 + (self.r1 - self.r0) * t + g
        return (x - px) ** 2 + (y - py) ** 2 <= r * r


class Poly:
    def __init__(self, points):
        self.points = points

    def bbox(self, g):
        xs, ys = [p[0] for p in self.points], [p[1] for p in self.points]
        return min(xs) - g, min(ys) - g, max(xs) + g, max(ys) + g

    def contains(self, x, y, g=0.0):
        inside = False
        pts = self.points
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
                inside = not inside
        if inside or g <= 0:
            return inside
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            if Capsule(x0, y0, x1, y1, 0).contains(x, y, g):
                return True
        return False


class Canvas:
    """RGBA samples with "over" blending, size units square; transform maps
    drawing coordinates (scale about a point, then rotation, then offset) for
    squash and tilt."""

    def __init__(self, size):
        self.units = size
        self.size = int(size * SCALE)  # pixels
        self.n = self.size * SS
        self.step = 1.0 / (SS * SCALE)  # units between samples
        self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(self.n * self.n)]
        self.transform = None

    def set_transform(self, sx=1.0, sy=1.0, ox=0.0, oy=0.0, angle=0.0, about=(16.0, 16.0)):
        self.transform = (sx, sy, ox, oy, math.cos(angle), math.sin(angle), about)

    def to_shape(self, x, y):
        if not self.transform:
            return x, y
        sx, sy, ox, oy, c, s, (ax, ay) = self.transform
        x, y = x - ox - ax, y - oy - ay
        x, y = x * c + y * s, -x * s + y * c
        return x / sx + ax, y / sy + ay

    def fill(self, shape, color, alpha=1.0, grow=0.0, clip=None):
        x0, y0, x1, y1 = shape.bbox(grow + 0.5)
        if self.transform:
            # Transformed shapes may land anywhere; test the whole canvas.
            x0, y0, x1, y1 = 0, 0, self.units, self.units
        i0, j0 = max(0, int(x0 / self.step)), max(0, int(y0 / self.step))
        i1, j1 = min(self.n, int(x1 / self.step) + 2), min(self.n, int(y1 / self.step) + 2)
        r, g, b = color
        for j in range(j0, j1):
            for i in range(i0, i1):
                x, y = self.to_shape((i + 0.5) * self.step, (j + 0.5) * self.step)
                if not shape.contains(x, y, grow):
                    continue
                if clip is not None and not clip.contains(x, y):
                    continue
                p = self.px[j * self.n + i]
                a = alpha + p[3] * (1 - alpha)
                if a > 0:
                    p[0] = (r * alpha + p[0] * p[3] * (1 - alpha)) / a
                    p[1] = (g * alpha + p[1] * p[3] * (1 - alpha)) / a
                    p[2] = (b * alpha + p[2] * p[3] * (1 - alpha)) / a
                p[3] = a

    def body(self, parts):
        """Parts (shape, color) share one outline: outlines first, then fills."""
        for shape, _ in parts:
            self.fill(shape, OUTLINE, grow=1.0)
        for shape, color in parts:
            self.fill(shape, color)

    def pixels(self):
        """Averaged down to final pixels, as straight RGBA bytes."""
        out = bytearray(self.size * self.size * 4)
        for y in range(self.size):
            for x in range(self.size):
                r = g = b = a = 0.0
                for j in range(SS):
                    for i in range(SS):
                        p = self.px[(y * SS + j) * self.n + x * SS + i]
                        r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3]
                k = (y * self.size + x) * 4
                if a > 0:
                    out[k:k + 4] = bytes((int(r / a), int(g / a), int(b / a), int(255 * a / (SS * SS))))
        return out


def shade(color, amount):
    """Lighter (amount > 0) or darker (amount < 0)."""
    if amount >= 0:
        return tuple(int(c + (255 - c) * amount) for c in color)
    return tuple(int(c * (1 + amount)) for c in color)


YELLOW = (250, 204, 56)
SKIN = (250, 198, 154)
BLUE = (62, 118, 226)
METAL = (176, 182, 196)
BOOT = (96, 58, 40)
PURPLE = (178, 86, 220)
GREEN = (84, 196, 104)
SPIKE = (250, 150, 50)
ROCK = (142, 136, 150)
WHITE = (246, 246, 242)
INK = (30, 24, 36)


def draw_digger(cv, step=0.0, pump=None, squash=1.0, dizzy=False):
    """The digger facing right. step is the walk phase (0-1); pump is the
    plunger position (0-1) while pumping."""
    swing = math.sin(step * 2 * math.pi)
    bob = abs(math.sin(step * 2 * math.pi)) * -1.0
    if squash != 1.0:
        cv.set_transform(sx=1 + (1 - squash) * 0.6, sy=squash, about=(16, 31))
    # Legs and boots, back leg first.
    for side, phase in ((-1, 1), (1, -1)):
        hip_x = 15 + side * 2
        foot_x = hip_x + phase * swing * 3.5
        lift = max(0.0, phase * swing) * 1.5
        cv.body([(Capsule(hip_x, 24 + bob, foot_x, 28.5 - lift, 1.9), shade(BLUE, -0.25)),
                 (Ellipse(foot_x + 1.2, 29.6 - lift, 2.6, 1.6), BOOT)])
    arm_x = 21 + (pump if pump is not None else -swing * 0.8) * 3
    parts = [
        (Ellipse(15.5, 20.5 + bob, 6.2, 5.4), BLUE),                    # overalls
        (Ellipse(16, 12.5 + bob, 6.0, 5.2), SKIN),                      # face
        (Ellipse(15.5, 8.5 + bob, 7.6, 5.4), YELLOW),                   # helmet
        (Capsule(8.5, 10.4 + bob, 24.5, 10.4 + bob, 1.3), shade(YELLOW, -0.2)),  # brim
        (Capsule(17, 19.5 + bob, arm_x, 21.5 + bob, 1.8), BLUE),        # arm
    ]
    if pump is not None or True:
        nozzle = arm_x + 6 + (pump or 0) * 1.5
        parts.append((Capsule(arm_x, 21.3 + bob, nozzle, 20.8 + bob, 1.2), METAL))
    cv.body(parts)
    # Shading and details.
    cv.fill(Ellipse(13, 6.5 + bob, 3.2, 1.8), shade(YELLOW, 0.55), clip=parts[2][0])
    cv.fill(Ellipse(17, 25 + bob, 6, 3), shade(BLUE, -0.25), clip=parts[0][0])
    cv.fill(Capsule(12.5, 16.5 + bob, 13.5, 24 + bob, 0.9), shade(BLUE, 0.3), clip=parts[0][0])  # strap
    cv.fill(Ellipse(23.4, 8.2 + bob, 2.0, 2.0), (170, 240, 255))        # lamp
    cv.fill(Ellipse(23.0, 7.6 + bob, 0.8, 0.8), WHITE)
    cv.fill(Ellipse(arm_x + 0.3, 21.4 + bob, 1.9, 1.9), SKIN)          # hand
    cv.fill(Ellipse(arm_x + 6.2 + (pump or 0) * 1.5, 20.8 + bob, 1.6, 1.6), shade(METAL, -0.4))
    if dizzy:
        for k in (-1, 1):
            cv.fill(Capsule(18 + k * 1.2, 11.2 + bob - 1.2, 18 - k * 1.2, 11.2 + bob + 1.2, 0.45), INK)
    else:
        cv.fill(Ellipse(19.3, 12.0 + bob, 1.1, 1.6), INK)                # eye
        cv.fill(Ellipse(19.6, 11.4 + bob, 0.4, 0.4), WHITE)
    cv.fill(Ellipse(21.6, 13.6 + bob, 1.4, 1.2), shade(SKIN, -0.15))    # nose
    cv.fill(Ellipse(16.5, 14.6 + bob, 1.6, 1.0), (246, 150, 150), alpha=0.6)  # cheek
    cv.transform = None


def draw_digger_stars(cv, t):
    for k in range(3):
        a = t * 2 * math.pi + k * 2.1
        x, y = 16 + math.cos(a) * 8, 6 + math.sin(a) * 2.5
        cv.fill(Poly([(x, y - 2), (x + 0.6, y - 0.6), (x + 2, y), (x + 0.6, y + 0.6),
                      (x, y + 2), (x - 0.6, y + 0.6), (x - 2, y), (x - 0.6, y - 0.6)]), (255, 236, 120))


def draw_blob(cv, step=0.0, blink=False, puff=0):
    """The blob; puff 1-3 is pumped up, centered in a 64-pixel cell."""
    bounce = math.sin(step * 2 * math.pi)
    if puff:
        grow = (1.5, 2.0, 2.6)[puff - 1]
        cv.set_transform(ox=16, oy=16)
        cx, cy, rx, ry = 16, 16, 11 * grow, 10.4 * grow
        eye_r, eye_dx = 3.2 + puff * 1.1, 4.0 * (1 + puff * 0.45)
    else:
        grow = 1.0
        sq = 1 + 0.06 * bounce
        cx, cy, rx, ry = 16, 17 - abs(bounce) * 1.2, 11 / sq, 10 * sq
        eye_r, eye_dx = 3.2, 3.8
    bodyshape = Ellipse(cx, cy, rx, ry)
    feet = []
    if not puff:
        for side in (-1, 1):
            fx = cx + side * 5 + side * bounce * 1.2
            feet.append((Ellipse(fx, cy + ry - 0.5, 3, 2), shade(PURPLE, -0.3)))
    tufts = [(Poly([(cx - 3, cy - ry + 1.5), (cx - 1, cy - ry - 3), (cx + 0.5, cy - ry + 1.5)]), PURPLE),
             (Poly([(cx, cy - ry + 1.5), (cx + 3, cy - ry - 2.2), (cx + 3.5, cy - ry + 2)]), PURPLE)]
    cv.body(feet + tufts + [(bodyshape, PURPLE)])
    cv.fill(Ellipse(cx + rx * 0.25, cy + ry * 0.35, rx * 0.95, ry * 0.7), shade(PURPLE, -0.2), clip=bodyshape)
    cv.fill(Ellipse(cx - rx * 0.4, cy - ry * 0.45, rx * 0.35, ry * 0.25), shade(PURPLE, 0.5), clip=bodyshape)
    # Eyes look the way it walks; pumped up, they bulge.
    for side in (-1, 1):
        x, y = cx + side * eye_dx + 0.2, cy - 2.2 * grow ** 0.5
        if blink:
            cv.fill(Capsule(x - eye_r, y, x + eye_r, y, 0.6), INK)
            continue
        cv.fill(Ellipse(x, y, eye_r + 0.7, eye_r + 1.2), INK)
        cv.fill(Ellipse(x, y, eye_r, eye_r + 0.5), WHITE)
        cv.fill(Ellipse(x + 1.2, y + 0.4, eye_r * 0.45, eye_r * 0.55), INK)
        cv.fill(Ellipse(x + 0.6, y - 0.7, eye_r * 0.18, eye_r * 0.18), WHITE)
    for side in (-1, 1):
        cv.fill(Ellipse(cx + side * (rx * 0.62), cy + 2.5 * grow, 2 * grow ** 0.5, 1.2 * grow ** 0.5), (250, 150, 200), alpha=0.7)
    if puff:
        cv.fill(Ellipse(cx + 1, cy + 4 * grow, 1.6 + puff, 1.2 + puff * 0.7), INK)     # gasping
    else:
        cv.fill(Capsule(cx - 0.5, cy + 3.5, cx + 2.5, cy + 3.5, 0.6), INK)
    cv.transform = None


def draw_eyes(cv, look=0.0):
    cv.fill(Ellipse(16, 17, 10, 9), PURPLE, alpha=0.18)
    for ex in (12, 20):
        cv.fill(Ellipse(ex, 15, 3.8, 4.4), INK)
        cv.fill(Ellipse(ex, 15, 3.0, 3.6), WHITE)
        cv.fill(Ellipse(ex + look * 1.4, 15.5, 1.4, 1.8), INK)


def draw_drake(cv, step=0.0, inhale=0.0, mouth=0.0, puff=0):
    """The drake facing right; inhale (0-1) puffs it up before it breathes,
    mouth (0-1) opens its jaw. puff 1-3 is pumped up, centered in a 64-pixel
    cell."""
    swing = math.sin(step * 2 * math.pi)
    if puff:
        grow = (1.35, 1.65, 1.95)[puff - 1]
        cv.set_transform(sx=grow, sy=grow * 1.05, ox=16, oy=14, about=(16, 18))
    glow = shade(GREEN, 0.25 * inhale)
    # Tail, legs, wing and spikes behind the body.
    tail_y = 18 + swing * 1.5
    parts = [
        (Capsule(9, 19, 3.5, tail_y, 3.2, 1.2), GREEN),
    ]
    for side, phase in ((-1, 1), (1, -1)):
        x = (13 + side * 3.5)
        parts.append((Capsule(x, 22, x + phase * swing * 2.2, 27.5, 1.9), shade(GREEN, -0.25)))
        parts.append((Ellipse(x + phase * swing * 2.2 + 1.2, 28.4, 2.4, 1.4), shade(GREEN, -0.4)))
    flap = (0.5 + 0.5 * math.sin(step * 2 * math.pi + 1)) if not puff else 0.3
    parts.append((Poly([(11, 15), (8, (6 + 4 * flap)), (15, (9 + 3 * flap)), (16, 15)]), (240, 120, 70)))
    for sx in (8, 11.5, 15):
        parts.append((Poly([((sx - 1.6), 14.5), (sx, 11.2), ((sx + 1.6), 14.5)]), SPIKE))
    puffing = 1 + inhale * 0.15
    body = Ellipse(13.5, 19, 8.6 * puffing, 6.6 * puffing)
    head = Ellipse(23, 13.5, 5.6 * puffing, 5.0 * puffing)
    snout = Ellipse(27.4, (15 - mouth * 0.6), 3.6, 2.4)
    jaw = Ellipse(26.5, (17 + mouth * 2.2), 3.2, 1.6)
    parts += [(body, glow), (jaw, shade(glow, -0.15)), (head, glow), (snout, glow)]
    cv.body(parts)
    cv.fill(Ellipse(14, 22, 6.5, 3.6), (232, 228, 150), clip=body)  # belly
    cv.fill(Ellipse(11, 16, 3.5, 1.8), shade(GREEN, 0.45), clip=body)
    cv.fill(Ellipse(21.5, 11, 2.2, 1.3), shade(GREEN, 0.45), clip=head)
    if mouth > 0:
        cv.fill(Ellipse(28, 16.4, 3, (0.6 + mouth)), (150, 30, 30))
    eye = (24, 11.6)
    cv.fill(Ellipse(eye[0], eye[1], 2.4, 2.6), INK)
    cv.fill(Ellipse(eye[0], eye[1], 1.8, 2.0), WHITE if not puff else (255, 230, 230))
    cv.fill(Ellipse(eye[0] + 0.6, eye[1] + 0.3, 0.8, 1.1), INK)
    cv.fill(Ellipse(29.6, 14, 0.5, 0.5), INK)  # nostril
    if inhale > 0.5:
        for n in range(2):
            cv.fill(Ellipse((30 + n * 1.5), (11 - n * 2.5), (1.1 + n * 0.4), (1.1 + n * 0.4)), (200, 196, 200), alpha=0.7)
    cv.transform = None


def draw_flame(cv, seed, tip=False):
    rng = random.Random(seed)
    length = 26 if tip else 32
    for layer, color in enumerate(((226, 60, 30), (250, 140, 40), (255, 222, 90), (255, 250, 210))):
        w = 9 - layer * 2.1
        pts = []
        steps = 9
        for n in range(steps + 1):
            x = n * length / steps
            taper = (1 - n / steps) ** 0.6 if tip else 1.0
            pts.append((x, 16 - (w * taper + rng.uniform(-1.2, 1.2)) * (0.85 + 0.3 * math.sin(n + seed))))
        for n in range(steps, -1, -1):
            x = n * length / steps
            taper = (1 - n / steps) ** 0.6 if tip else 1.0
            pts.append((x, 16 + (w * taper + rng.uniform(-1.2, 1.2)) * (0.85 + 0.3 * math.cos(n + seed))))
        cv.fill(Poly(pts), color)


def rock_shape(cx=16, cy=17, scale=1.0, angle=0.0, seed=3):
    rng = random.Random(seed)
    pts = []
    for n in range(11):
        a = angle + n * 2 * math.pi / 11
        r = (10.5 + rng.uniform(-1.4, 1.2)) * scale
        pts.append((cx + math.cos(a) * r * 1.05, cy + math.sin(a) * r * 0.92))
    return Poly(pts)


def draw_rock(cv, cracked=False):
    shape = rock_shape()
    cv.body([(shape, ROCK)])
    cv.fill(Ellipse(19, 21, 10, 7), shade(ROCK, -0.3), clip=shape)
    cv.fill(Ellipse(12, 12, 5, 3.2), shade(ROCK, 0.4), clip=shape)
    cv.fill(Ellipse(21, 13, 1.4, 1.0), shade(ROCK, -0.35))
    cv.fill(Ellipse(11, 21, 1.2, 0.9), shade(ROCK, -0.35))
    if cracked:
        for a, b in (((15, 7), (13, 13)), ((13, 13), (17, 18)), ((17, 18), (14, 26)), ((17, 18), (23, 21))):
            cv.fill(Capsule(a[0], a[1], b[0], b[1], 0.55), INK)


def draw_crumble(cv, t):
    """The rock falling apart: t from 0 to 1."""
    rng = random.Random(9)
    for n in range(5):
        a = n * 2 * math.pi / 5 + 0.4
        cx, cy = 16 + math.cos(a) * (3 + 9 * t), 18 + math.sin(a) * (2 + 4 * t) + 8 * t * t
        piece = rock_shape(cx, cy, scale=0.38 - 0.12 * t, angle=a + t * 2, seed=n + 20)
        cv.body([(piece, ROCK)])
        cv.fill(Ellipse(cx - 1, cy - 1, 1.6, 1.0), shade(ROCK, 0.4), clip=piece)
    for n in range(8):
        x, y = rng.uniform(4, 28), rng.uniform(18, 30) + 4 * t
        cv.fill(Ellipse(x, y, 0.8, 0.8), (200, 180, 150), alpha=max(0.0, 0.8 - t))


def draw_pop(cv, t):
    """The burst when a creature pops: t from 0 to 1."""
    r = 4 + 12 * t
    if t < 0.4:
        cv.fill(Ellipse(16, 16, r, r), WHITE, alpha=0.9 - t)
    cv.fill(Ellipse(16, 16, r, r), (255, 236, 140), alpha=0.5 * (1 - t), grow=0)
    for n in range(10):
        a = n * 2 * math.pi / 10 + t
        x0, y0 = 16 + math.cos(a) * r * 0.5, 16 + math.sin(a) * r * 0.5
        x1, y1 = 16 + math.cos(a) * (r + 3), 16 + math.sin(a) * (r + 3)
        cv.fill(Capsule(x0, y0, x1, y1, 1.0 * (1 - t) + 0.3), (255, 250, 220) if n % 2 else (255, 170, 80))


def draw_mushroom(cv):
    cap = Ellipse(16, 15, 12, 9)
    cv.body([(Capsule(16, 18, 16, 27, 4.4, 4.0), WHITE), (cap, (226, 52, 50))])
    cv.fill(Ellipse(18, 19, 12, 5), shade((226, 52, 50), -0.35), clip=cap)
    for x, y, r in ((10, 11, 2.4), (18, 9, 2.0), (23, 14, 1.8), (13, 16, 1.4)):
        cv.fill(Ellipse(x, y, r, r * 0.9), WHITE, clip=cap)
    cv.fill(Ellipse(14.5, 23, 0.9, 1.3), INK)
    cv.fill(Ellipse(17.5, 23, 0.9, 1.3), INK)


def draw_flower(cv, sway):
    top = (16 + sway * 2.5, 12)
    cv.body([(Capsule(16, 30, top[0], top[1], 1.0), (60, 150, 70)),
             (Ellipse(12 + sway, 24, 3.5, 1.6, angle=-0.5), (70, 170, 80))])
    for n in range(5):
        a = n * 2 * math.pi / 5 + sway * 0.3
        cv.body([(Ellipse(top[0] + math.cos(a) * 3.6, top[1] + math.sin(a) * 3.6, 2.6, 2.6), (240, 80, 90))])
    cv.fill(Ellipse(top[0], top[1], 2.4, 2.4), (255, 214, 70))


def draw_life(cv):
    cv.body([(Ellipse(16, 18, 9, 8), SKIN), (Ellipse(15.5, 12, 11, 7.6), YELLOW),
             (Capsule(5, 14.5, 27, 14.5, 1.8), shade(YELLOW, -0.2))])
    cv.fill(Ellipse(12.5, 9, 4, 2.2), shade(YELLOW, 0.55))
    cv.fill(Ellipse(25, 11.5, 2.6, 2.6), (170, 240, 255))
    cv.fill(Ellipse(20, 19, 1.4, 2.0), INK)
    cv.fill(Ellipse(16, 22, 2.2, 1.3), (246, 150, 150), alpha=0.6)


def sprite(draw, size=32, **options):
    """A drawing on a canvas of size units."""
    cv = Canvas(size)
    draw(cv, **options)
    return cv


CELL = int(32 * SCALE)
BIG_CELL = int(64 * SCALE)


def rotate(pixels, turns, size=CELL):
    """Quarter turns clockwise of straight RGBA pixels."""
    for _ in range(turns % 4):
        out = bytearray(len(pixels))
        for y in range(size):
            for x in range(size):
                src = (y * size + x) * 4
                dst = (x * size + (size - 1 - y)) * 4
                out[dst:dst + 4] = pixels[src:src + 4]
        pixels = out
    return pixels


def digger_frames():
    frames = [sprite(draw_digger, step=n / 4).pixels() for n in range(4)]
    frames += [sprite(draw_digger, pump=p).pixels() for p in (0.0, 1.0)]
    return frames


def death_frame(n):
    cv = Canvas(32)
    if n == 0:
        draw_digger(cv, dizzy=True)
    else:
        draw_digger(cv, squash=(0.72, 0.45, 0.22)[n - 1], dizzy=True)
    if n < 3:
        draw_digger_stars(cv, n / 3)
    return cv.pixels()


def build_cells():
    """The 32-pixel cells in sheet order, and the 64-pixel ones."""
    right = digger_frames()
    cells = []
    cells += right                                     # 0-5 digger right: walk 0-3, pump 0-1
    cells += [rotate(f, 3) for f in right]             # 6-11 up
    cells += [rotate(f, 1) for f in right]             # 12-17 down
    cells += [death_frame(n) for n in range(4)]        # 18-21 digger dying
    cells.append(sprite(draw_life).pixels())           # 22 spare digger
    cells += [sprite(draw_blob, step=n / 4).pixels() for n in range(4)]   # 23-26 blob walk
    cells.append(sprite(draw_blob, blink=True).pixels())                  # 27 blob blink
    cells += [sprite(draw_eyes, look=v).pixels() for v in (1.0, -0.4)]    # 28-29 floating eyes
    cells += [sprite(draw_drake, step=n / 4).pixels() for n in range(4)]  # 30-33 drake walk
    cells += [sprite(draw_drake, inhale=v).pixels() for v in (0.5, 1.0)]  # 34-35 drake winding up
    cells.append(sprite(draw_drake, mouth=1.0).pixels())                  # 36 drake breathing
    cells += [sprite(draw_flame, seed=n).pixels() for n in range(3)]      # 37-39 flame
    cells += [sprite(draw_flame, seed=n + 5, tip=True).pixels() for n in range(3)]  # 40-42 flame tip
    cells.append(sprite(draw_rock).pixels())                              # 43 rock
    cells.append(sprite(draw_rock, cracked=True).pixels())                # 44 cracked
    cells += [sprite(draw_crumble, t=t).pixels() for t in (0.2, 0.55, 0.9)]  # 45-47 crumbling
    cells += [sprite(draw_pop, t=t).pixels() for t in (0.1, 0.45, 0.8)]   # 48-50 pop
    cells.append(sprite(draw_mushroom).pixels())                          # 51 mushroom
    cells += [sprite(draw_flower, sway=v).pixels() for v in (-0.5, 0.5)]  # 52-53 flower
    big = [sprite(draw_blob, size=64, puff=n).pixels() for n in (1, 2, 3)]   # 0-2 blob pumped
    big += [sprite(draw_drake, size=64, puff=n).pixels() for n in (1, 2, 3)]  # 3-5 drake pumped
    return cells, big


PER_ROW = 16


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


def write_sheet(path, cells, cell):
    rows = (len(cells) + PER_ROW - 1) // PER_ROW
    width, height = min(len(cells), PER_ROW) * cell, rows * cell
    rgba = bytearray(width * height * 4)
    for index, pixels in enumerate(cells):
        ox, oy = (index % PER_ROW) * cell, (index // PER_ROW) * cell
        for y in range(cell):
            at = ((oy + y) * width + ox) * 4
            rgba[at:at + cell * 4] = pixels[y * cell * 4:(y + 1) * cell * 4]
    write_png(path, width, height, rgba)


def build_sheet():
    cells, big = build_cells()
    write_sheet(os.path.join(ASSETS, "sprites.png"), cells, CELL)
    write_sheet(os.path.join(ASSETS, "sprites_big.png"), big, BIG_CELL)


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
