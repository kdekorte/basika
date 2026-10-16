# BASIKA demo video

`build.sh` makes `basika_demo.mp4`, a 2-minute tour of basika for YouTube:
a title card, a short intro typed on a DOS screen, the 3D wave, fractal and
boing ball demos, then BASIKA Paint, Jukebox, Vortex and Burrow being used,
and a closing card with the GitHub link. It has an 80s/90s look (a
synthwave title, the IBM VGA font, a CRT monitor frame, scanlines and glow)
and an original synth-pop soundtrack.

```sh
make                      # in the repository root, to build basika
video_demo/build.sh       # about 7 minutes
video_demo/build.sh --skip-record   # reuse the recorded footage
```

It needs ffmpeg and Python 3 with Pillow.

## How it works

Nothing is screen-recorded. Each demo runs headless and saves every frame
as a PNG with `SCREENSHOT`, so the footage is sharp, smooth and the same on
every build. The demos in `demo/` are not changed; `record/` makes a copy
of each for the video:

| Clip | Recording | What it shows |
|---|---|---|
| 3D wave | `record/3dwave.bas` | the surface drawn line by line, then turned |
| Fractal | `record/fractal.bas` | the Mandelbrot set filling in |
| Boing ball | `record/make_boing.sh` | real time steps, no FPS counter |
| Paint | `record/make_paint.sh` + `paint_director.bas` | a pointer picks tools and colors and paints a picture |
| Jukebox | `record/make_jukebox.sh` + `jukebox_director.bas` | a pointer plays, pauses and switches songs, and opens Reset; it uses its own `build/jukebox/jukebox.dat` |
| Vortex | `record/make_vortex.sh` | the title screen, then a game the autopilot plays, with a superzapper |
| Burrow | `record/make_burrow.sh` | the title screen, then a round the autopilot plays |

The `make_*.sh` scripts edit a copy of each program with exact,
checked replacements, so a change to a demo that breaks one fails loudly.

`tools/make_cards.py` draws the title, intro and closing cards, the
captions and the monitor frame. `tools/make_music.py` synthesizes the
music (A minor, 112 BPM: drums with a gated snare, pulse bass, saw pads, an
arpeggio and a square-wave lead) with the Python standard library, to the
length of the video. `build.sh` puts it together with ffmpeg and sets the
loudness to YouTube's -14 LUFS.

To change the words, edit `INTRO` and `CAPTIONS` in `tools/make_cards.py`;
the caption timings are the `clip` lines in `build.sh`.
