#!/usr/bin/env python3
"""Draws Burrow's application icon into ../assets/icon.png (1024 x 1024).

The digger and a blob meet in a tunnel cut through Burrow's four layers of
earth, under a strip of sky, on the rounded square that macOS icons use. The
creatures are the same drawings as the game's sprites (make_assets.py), drawn
large. tools/make_mac_app.py turns the PNG into the app's icon set. Run it
again after editing the icon:

    python3 demo/burrow/tools/make_icon.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_assets as art  # noqa: E402

SIZE = 1024
# Apple's icon grid: an 824-pixel rounded square centered on the canvas.
BOX0, BOX1, RADIUS = 100, 924, 185
SKY, GRASS = 300, 328  # the grass strip runs from SKY to GRASS
LAYERS = [(226, 176, 72), (208, 124, 46), (176, 80, 40), (122, 48, 46)]  # Burrow's earth
FLECKS = [(190, 136, 48), (168, 90, 30), (130, 52, 28), (86, 28, 32)]
TUNNEL = (16, 10, 12)
TUNNEL_Y0, TUNNEL_Y1, TUNNEL_X1 = 470, 862, 860  # the tunnel's top, floor and round right end


def box_coverage(x, y):
    """How much of pixel (x, y) is inside the rounded square, 0-1."""
    cx = min(max(x + 0.5, BOX0 + RADIUS), BOX1 - RADIUS)
    cy = min(max(y + 0.5, BOX0 + RADIUS), BOX1 - RADIUS)
    d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
    return max(0.0, min(1.0, RADIUS - d + 0.5))


def in_tunnel(x, y):
    r = (TUNNEL_Y1 - TUNNEL_Y0) / 2
    if not TUNNEL_Y0 <= y <= TUNNEL_Y1:
        return False
    return x <= TUNNEL_X1 - r or math.hypot(x - (TUNNEL_X1 - r), y - (TUNNEL_Y0 + r)) <= r


def background_color(x, y):
    if y < SKY:
        t = (y - BOX0) / (SKY - BOX0)
        return (int(70 + 80 * t), int(130 + 70 * t), int(230 - 20 * t))
    if y < GRASS:
        return (84, 180, 84) if y < GRASS - 8 else (54, 130, 60)
    if in_tunnel(x, y):
        # A little light falls on the tunnel floor.
        return TUNNEL if y < TUNNEL_Y1 - 30 else (40, 26, 26)
    band = min(3, (y - GRASS) * 4 // (BOX1 - GRASS))
    # Flecks of darker earth on a fixed scatter, like the game's dirt.
    h = (x // 22 * 73856093) ^ (y // 22 * 19349663)
    if h % 7 == 0 and (x % 22) < 9 and (y % 22) < 6:
        return FLECKS[band]
    return LAYERS[band]


def sprite_layer(draw, units, pixels, ss=2, **options):
    """A Burrow sprite drawn at pixels x pixels, as straight RGBA bytes."""
    saved = art.SS, art.SCALE
    art.SS, art.SCALE = ss, pixels / units
    try:
        cv = art.Canvas(units)
        draw(cv, **options)
        return cv.pixels(), cv.size
    finally:
        art.SS, art.SCALE = saved


def over(dst, x0, y0, src, size):
    """Composites straight-alpha src onto dst (float RGBA rows) at (x0, y0)."""
    for y in range(size):
        for x in range(size):
            k = (y * size + x) * 4
            a = src[k + 3] / 255
            if a <= 0:
                continue
            p = dst[(y0 + y) * SIZE + x0 + x]
            for c in range(3):
                p[c] = src[k + c] * a + p[c] * (1 - a)


def main():
    out_path = os.path.join(art.ASSETS, "icon.png")
    img = [[0.0, 0.0, 0.0] for _ in range(SIZE * SIZE)]
    for y in range(BOX0, BOX1):
        for x in range(BOX0, BOX1):
            img[y * SIZE + x] = list(background_color(x, y))

    # A sun, then a rock in the earth above the tunnel, the blob at the far
    # end and the digger in front.
    for y in range(140, 260):
        for x in range(700, 820):
            d = math.hypot(x - 760, y - 200)
            if d < 46:
                img[y * SIZE + x] = [255, 230, 120]
            elif d < 58:
                p = img[y * SIZE + x]
                img[y * SIZE + x] = [p[0] * 0.45 + 255 * 0.55, p[1] * 0.45 + 248 * 0.55, p[2] * 0.45 + 210 * 0.55]
    rock, rsize = sprite_layer(art.draw_rock, 32, 200)
    over(img, 640, 286, rock, rsize)
    blob, bsize = sprite_layer(art.draw_blob, 32, 270, step=0.25)
    over(img, 600, TUNNEL_Y1 - 250, blob, bsize)
    digger, dsize = sprite_layer(art.draw_digger, 32, 450, pump=0.6)
    over(img, 130, TUNNEL_Y1 - 432, digger, dsize)

    # Cut out the rounded square, with a soft shadow below it.
    rgba = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        for x in range(SIZE):
            k = (y * SIZE + x) * 4
            a = box_coverage(x, y)
            if a > 0:
                p = img[y * SIZE + x]
                rgba[k:k + 4] = bytes((int(p[0]), int(p[1]), int(p[2]), int(255 * a)))
            else:
                # Distance outside the square, offset down by 10 pixels.
                cx = min(max(x + 0.5, BOX0 + RADIUS), BOX1 - RADIUS)
                cy = min(max(y - 9.5, BOX0 + RADIUS), BOX1 - RADIUS)
                d = math.hypot(x + 0.5 - cx, y - 9.5 - cy) - RADIUS
                shadow = max(0.0, 1 - d / 28) ** 2 * 0.45
                rgba[k:k + 4] = bytes((0, 0, 0, int(255 * shadow)))
    art.write_png(out_path, SIZE, SIZE, rgba)
    print("wrote", out_path)


if __name__ == "__main__":
    main()
