#!/usr/bin/env python3
"""Draws the video's title card, intro, closing card and caption overlays
as PNG frames, at 1280 x 720, 30 frames a second, in an 80s/90s style: a
synthwave sunset with a neon grid, chrome lettering and a DOS screen being
typed on, in the IBM VGA font from fonts/.

Usage: make_cards.py build_dir version
Writes build_dir/cards/{title,intro,closing}/NNNNN.png and
build_dir/captions/<name>.png (transparent, one per caption), as listed in
CAPTIONS below.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H, FPS = 1280, 720, 30
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, '..', '..', 'fonts', 'Px437_IBM_VGA_8x16.ttf')

MAGENTA = (255, 46, 196)
CYAN = (60, 230, 255)
YELLOW = (255, 214, 80)
DOS_BLUE = (0, 0, 170)
DOS_GREY = (170, 170, 170)

INTRO = [
    "C:\\BASIKA> TYPE HELLO.TXT",
    "",
    "I have always had a soft spot for BASIC.",
    "It is the language I first learned to program in,",
    "and I have done a lot with it since -- including",
    "professional development with tools like Visual BASIC.",
    "",
    "I built BASIKA to give people an easy entry",
    "into the world of programming.",
    "",
    "Let me show you some of the things it can do...",
]

# Captions shown over the demos: file name -> (title, line of detail[,
# 'top' to sit at the top of the frame]).
CAPTIONS = {
    '3dwave': ('3D WAVE', 'SCREEN 1 CGA graphics: a sin(d)/d surface in 20 lines of BASIC', 'top'),
    'fractal': ('MANDELBROT FRACTAL', '640 x 480 with a 256-color VGA palette'),
    'boing': ('BOING BALL', 'The Amiga classic, drawn with _PUTIMAGE sprites'),
    'paint': ('BASIKA PAINT', 'A MacPaint-style app: tools, palette, fill, opacity, mirror'),
    'paint2': ('BASIKA PAINT', 'Shapes preview as you drag; brushes blend at 50% opacity'),
    'jukebox': ('JUKEBOX', 'Plays WAV files with _SNDPLAY; stats kept in a random-access file'),
    'jukebox2': ('JUKEBOX', 'Pause, resume, switch songs -- every play is saved with GET and PUT'),
    'vortex': ('VORTEX', 'A Tempest-style tube shooter in vector graphics'),
    'vortex2': ('VORTEX', 'The claw moves round the rim -- and here is the superzapper'),
    'burrow': ('BURROW', 'A Dig Dug-style game with sprites, sound and joystick support'),
    'burrow2': ('BURROW', 'Dig tunnels, pump up the monsters, clear the round'),
}


def font(size):
    return ImageFont.truetype(FONT, size)


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- synthwave


def synthwave_background(scroll):
    """A night sky, a striped sun on the horizon and a scrolling neon grid."""
    img = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(img)
    horizon = int(H * .58)
    for y in range(horizon):
        t = y / horizon
        d.line([(0, y), (W, y)], fill=(int(10 + 60 * t), int(4 + 8 * t), int(30 + 60 * t)))
    # A few stars.
    for i in range(70):
        x = (i * 397) % W
        y = (i * 151) % (horizon - 120)
        b = 120 + (i * 53) % 135
        d.point((x, y), fill=(b, b, b))
    # The sun: a vertical gradient, with bands cut out of its lower half.
    sun_r, pad = 130, 60
    cx, cy = W // 2, horizon + 30  # half set below the horizon
    size = 2 * (sun_r + pad)
    sun = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sun)
    for y in range(2 * sun_r):
        t = y / (2 * sun_r)
        col = (255, int(220 - 160 * t), int(70 + 90 * t), 255)
        half = int(math.sqrt(max(0, sun_r ** 2 - (y - sun_r) ** 2)))
        sd.line([(pad + sun_r - half, pad + y), (pad + sun_r + half, pad + y)], fill=col)
    for k in range(6):
        y0 = pad + sun_r - 40 + k * 20
        sd.rectangle([0, y0, size, y0 + 3 + k * 2], fill=(0, 0, 0, 0))
    glow = sun.filter(ImageFilter.GaussianBlur(26))
    sky = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    sky.alpha_composite(glow)
    sky.alpha_composite(sun)
    # Only the part above the horizon shows; the floor covers the rest.
    img.paste(sky, (cx - sun_r - pad, cy - sun_r - pad), sky)
    # The floor below the horizon, with a perspective grid.
    d.rectangle([0, horizon, W, H], fill=(14, 0, 28))
    grid = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for i in range(-24, 25):
        x_far = W / 2 + i * 26
        x_near = W / 2 + i * 260
        gd.line([(x_far, horizon), (x_near, H)], fill=MAGENTA + (255,), width=2)
    for k in range(18):
        z = (k + scroll) / 18.0
        y = horizon + (H - horizon) * (z ** 2.2)
        gd.line([(0, y), (W, y)], fill=MAGENTA + (255,), width=2)
    gblur = grid.filter(ImageFilter.GaussianBlur(4))
    img.paste(gblur, (0, 0), gblur)
    img.paste(grid, (0, 0), grid)
    d.line([(0, horizon), (W, horizon)], fill=(255, 120, 230), width=2)
    return img


def chrome_text(img, text, size, cy, reveal=1.0):
    """Big chrome lettering with a pink outline glow, centered at height cy."""
    f = font(size)
    box = f.getbbox(text)
    tw, th = box[2] - box[0], box[3] - box[1]
    x0 = (W - tw) // 2 - box[0]
    y0 = cy - th // 2 - box[1]
    mask = Image.new('L', (W, H), 0)
    ImageDraw.Draw(mask).text((x0, y0), text, font=f, fill=255)
    # Wipe the text on from left to right.
    if reveal < 1:
        cut = int((W - tw) / 2 + tw * reveal)
        ImageDraw.Draw(mask).rectangle([cut, 0, W, H], fill=0)
    glow = Image.new('RGBA', (W, H), MAGENTA + (0,))
    glow.putalpha(mask.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(10)))
    img.paste(glow, (0, 0), glow)
    outline = Image.new('RGBA', (W, H), (40, 0, 60, 0))
    outline.putalpha(mask.filter(ImageFilter.MaxFilter(7)))
    img.paste(outline, (0, 0), outline)
    # The chrome: sky blue over white over a dark band over gold.
    fill = Image.new('RGB', (W, H))
    fd = ImageDraw.Draw(fill)
    top, bottom = cy - th // 2, cy + th // 2
    for y in range(top - 2, bottom + 3):
        t = (y - top) / max(1, th)
        if t < .48:
            u = t / .48
            col = (int(70 + 185 * u), int(130 + 125 * u), 255)
        elif t < .52:
            col = (40, 20, 70)
        else:
            u = (t - .52) / .48
            col = (255, int(250 - 120 * u), int(200 - 170 * u))
        fd.line([(0, y), (W, y)], fill=col)
    img.paste(fill, (0, 0), mask)


def scanlines(img, strength=.18):
    """Darkens every other row, like a CRT."""
    over = Image.new('RGBA', img.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    a = int(255 * strength)
    for y in range(0, img.size[1], 2):
        od.line([(0, y), (img.size[0], y)], fill=(0, 0, 0, a))
    img.paste(over, (0, 0), over)
    return img


def centered(d, text, f, y, fill):
    box = f.getbbox(text)
    d.text(((W - (box[2] - box[0])) // 2 - box[0], y), text, font=f, fill=fill)


def plate(img, y0, y1, width=900):
    """A translucent dark band with neon edges, so text reads over the sky."""
    over = Image.new('RGBA', img.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    x0 = (W - width) // 2
    od.rectangle([x0, y0, x0 + width, y1], fill=(8, 0, 24, 200))
    od.line([(x0, y0), (x0 + width, y0)], fill=CYAN + (200,), width=2)
    od.line([(x0, y1), (x0 + width, y1)], fill=MAGENTA + (200,), width=2)
    img.paste(over, (0, 0), over)


def title_card(out, version, seconds=6):
    n = seconds * FPS
    for i in range(n):
        t = i / FPS
        img = synthwave_background((t * 1.2) % 1.0)
        chrome_text(img, 'BASIKA', 150, 160, reveal=ease(t / 1.2))
        if t > 1.2:
            plate(img, 268, 380)
        d = ImageDraw.Draw(img)
        if t > 1.3:
            a = ease((t - 1.3) / .6)
            col = tuple(int(c * a) for c in CYAN)
            centered(d, 'D E M O   O F   B A S I K A   ' + version, font(32), 288, col)
        if t > 2.0:
            a = ease((t - 2.0) / .6)
            col = tuple(int(c * a) for c in YELLOW)
            centered(d, 'A BASICA / QBasic / QB64-compatible interpreter', font(24), 336, col)
        img = scanlines(img)
        fade_in_out(img, i, n, 12, 15).save(os.path.join(out, f'{i + 1:05d}.png'))


def closing_card(out, seconds=8):
    n = seconds * FPS
    for i in range(n):
        t = i / FPS
        img = synthwave_background((t * 1.2) % 1.0)
        chrome_text(img, 'BASIKA', 110, 110)
        plate(img, 190, 400, 960)
        d = ImageDraw.Draw(img)
        centered(d, 'Get it, star it, and write some BASIC:', font(28), 212, YELLOW)
        url = 'https://github.com/kdekorte/basika'
        # The link in a DOS-style box, with a blinking block cursor after it.
        f = font(32)
        box = f.getbbox(url)
        tw = box[2] - box[0]
        x0, y0 = (W - tw) // 2 - 24, 256
        d.rectangle([x0, y0, x0 + tw + 48, y0 + 56], fill=DOS_BLUE, outline=CYAN, width=3)
        d.text((x0 + 24 - box[0], y0 + 12), url, font=f, fill=(255, 255, 255))
        if int(t * 2) % 2 == 0:
            cx = x0 + 24 + tw + 6
            d.rectangle([cx, y0 + 14, cx + 14, y0 + 44], fill=(255, 255, 255))
        centered(d, 'brew install kdekorte/basika/basika', font(24), 330, CYAN)
        centered(d, 'Thanks for watching!', font(24), 364, (255, 160, 230))
        img = scanlines(img)
        fade_in_out(img, i, n, 15, 30).save(os.path.join(out, f'{i + 1:05d}.png'))


# ------------------------------------------------------------- the DOS intro


def intro_card(out):
    """The intro text typed onto a blue DOS screen, then held."""
    f = font(30)
    line_h = 40
    chars_per_frame = 1.6
    lines = INTRO
    total = sum(len(s) for s in lines) + 6 * len(lines)  # a pause per line
    typing = int(total / chars_per_frame) + 1
    n = typing + 4 * FPS
    for i in range(n):
        img = Image.new('RGB', (W, H), DOS_BLUE)
        d = ImageDraw.Draw(img)
        # A DOS-window title bar.
        d.rectangle([0, 0, W, 44], fill=DOS_GREY)
        centered(d, 'BASIKA 0.99.8  -  HELLO.TXT', font(26), 10, (0, 0, 0))
        budget = i * chars_per_frame
        y = 92
        cursor = None
        for s in lines:
            if budget <= 0:
                break
            shown = s[:int(budget)]
            colour = YELLOW if s.startswith('C:\\') else (255, 255, 255)
            d.text((80, y), shown, font=f, fill=colour)
            cursor = (80 + int(f.getlength(shown)), y)
            budget -= len(s) + 6
            y += line_h
        if cursor and (i // 8) % 2 == 0:
            d.rectangle([cursor[0] + 2, cursor[1] + 26, cursor[0] + 18, cursor[1] + 31], fill=(255, 255, 255))
        img = scanlines(img, .22)
        fade_in_out(img, i, n, 10, 15).save(os.path.join(out, f'{i + 1:05d}.png'))
    return n


def fade_in_out(img, i, n, fin, fout):
    a = min(1.0, (i + 1) / fin, (n - i) / fout)
    if a >= 1:
        return img
    return Image.blend(Image.new('RGB', img.size), img, a)


# --------------------------------------------------------- the demo backdrop

# Where demo footage goes in the 1280 x 720 frame: a 4:3 screen, 880 x 660.
SCREEN_BOX = (200, 30, 1080, 690)


def backdrop(path):
    """A dimmed synthwave scene with a beige CRT bezel around SCREEN_BOX,
    whose screen is transparent so the footage shows through."""
    img = synthwave_background(0).convert('RGBA')
    img = Image.blend(img, Image.new('RGBA', img.size, (0, 0, 0, 255)), .45)
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = SCREEN_BOX
    d.rounded_rectangle([x0 - 34, y0 - 26, x1 + 34, y1 + 26], radius=26, fill=(196, 188, 168, 255))
    d.rounded_rectangle([x0 - 34, y0 - 26, x1 + 34, y1 + 26], radius=26, outline=(120, 112, 96, 255), width=3)
    d.rounded_rectangle([x0 - 10, y0 - 8, x1 + 10, y1 + 8], radius=14, fill=(40, 40, 44, 255))
    # A power light.
    d.ellipse([x1 + 12, y1 + 6, x1 + 22, y1 + 16], fill=(60, 230, 90, 255))
    d.rectangle([x0, y0, x1, y1], fill=(0, 0, 0, 0))
    img.save(path)


# ------------------------------------------------------------------ captions


def caption(path, title, detail, where='bottom'):
    """A caption in a DOS-window style, on a transparent frame: a lower third,
    or at the top for footage with its own text along the bottom."""
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ft, fd = font(34), font(22)
    tw = max(ft.getlength(title), fd.getlength(detail))
    x0, y0 = 48, (H - 150 if where == 'bottom' else 44)
    x1, y1 = x0 + int(tw) + 56, y0 + 106
    # A drop shadow, then the box with a title tab.
    d.rectangle([x0 + 8, y0 + 8, x1 + 8, y1 + 8], fill=(0, 0, 0, 150))
    d.rectangle([x0, y0, x1, y1], fill=(0, 0, 120, 225), outline=CYAN + (255,), width=3)
    d.rectangle([x0, y0, x0 + 14, y1], fill=MAGENTA + (255,))
    d.text((x0 + 32, y0 + 14), title, font=ft, fill=YELLOW + (255,))
    d.text((x0 + 32, y0 + 62), detail, font=fd, fill=(235, 235, 255, 255))
    img.save(path)


def main():
    build, version = sys.argv[1], sys.argv[2]
    for name in ('title', 'intro', 'closing'):
        folder = os.path.join(build, 'cards', name)
        os.makedirs(folder, exist_ok=True)
        for f in os.listdir(folder):
            os.remove(os.path.join(folder, f))
    title_card(os.path.join(build, 'cards', 'title'), version)
    intro_card(os.path.join(build, 'cards', 'intro'))
    closing_card(os.path.join(build, 'cards', 'closing'))
    backdrop(os.path.join(build, 'backdrop.png'))
    cap = os.path.join(build, 'captions')
    os.makedirs(cap, exist_ok=True)
    for name, spec in CAPTIONS.items():
        caption(os.path.join(cap, name + '.png'), *spec)


if __name__ == '__main__':
    main()
