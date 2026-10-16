#!/usr/bin/env bash
# Builds basika_demo.mp4: records each demo headlessly, frame by frame,
# draws the cards and captions, synthesizes the music, and puts it all
# together with ffmpeg. Run from anywhere; needs ffmpeg, python3 with
# Pillow, and a built ../basika.
#
#   ./build.sh            everything
#   ./build.sh --skip-record   reuse the recorded frames in build/frames
set -euo pipefail
cd "$(dirname "$0")"
BASIKA=../basika
VERSION=$($BASIKA --version | awk '{print $2}')
FPS=30
SKIP_RECORD=0
[ "${1:-}" = "--skip-record" ] && SKIP_RECORD=1

[ -x "$BASIKA" ] || { echo "Build basika first (make in the repository root)" >&2; exit 1; }
mkdir -p build/frames build/clips build/jukebox

record() { # name, then the command that records it
    local name=$1; shift
    echo "== recording $name"
    mkdir -p "build/frames/$name"
    find "build/frames/$name" -name '*.png' -delete
    "$@" </dev/null
    echo "   $(ls "build/frames/$name" | wc -l | tr -d ' ') frames"
}

if [ $SKIP_RECORD = 0 ]; then
    record 3dwave  $BASIKA --headless -q --no-audio record/3dwave.bas
    record fractal $BASIKA --headless -q --no-audio record/fractal.bas
    record/make_boing.sh 270
    record boing   $BASIKA --headless -q --no-audio build/boing_rec.bas
    record/make_paint.sh
    record paint   $BASIKA --headless -q --no-audio build/paint_rec.bas --selftest
    record/make_jukebox.sh
    # Sound on, so the songs play and the progress bar moves.
    record jukebox $BASIKA --headless -q build/jukebox_rec.bas --selftest 780
    record/make_vortex.sh 150 600
    record vortex  $BASIKA --headless -q --no-audio build/vortex_rec.bas --selftest 1200
    record/make_burrow.sh 120
    BASIKA_DATA_DIR=build/ record burrow $BASIKA --headless -q --no-audio build/burrow_rec.bas --selftest 1500
    # The demos' own selftest screenshots land in the current directory.
    rm -f jukebox_selftest.png vortex_selftest.png burrow_selftest.png basika_paint_selftest.png
fi

echo "== cards and captions"
python3 tools/make_cards.py build "$VERSION"

# Arithmetic with a leading zero (bc prints .4; ffmpeg wants 0.4).
num() { echo "scale=3; $1" | bc | sed 's/^\./0./; s/^-\./-0./'; }

# A demo clip: the footage scaled into the monitor's screen (nearest
# neighbor, so pixels stay sharp), the backdrop on top, and up to two
# captions, each shown for a stretch of the clip with a fade.
#   clip name source_fps speed caption1 from1 to1 [caption2 from2 to2]
clip() {
    local name=$1 rate=$2 speed=$3
    shift 3
    local inputs=(-framerate "$rate" -i "build/frames/$name/%05d.png" -loop 1 -i build/backdrop.png)
    local graph="[0:v]setpts=PTS/$speed,fps=$FPS,scale=880:660:flags=neighbor,pad=1280:720:200:30:black[f];[f][1:v]overlay=shortest=1[v0]"
    local last=v0 n=2 i=0
    while [ $# -ge 3 ]; do
        local cap=$1 from=$2 to=$3
        shift 3
        inputs+=(-loop 1 -i "build/captions/$cap.png")
        graph="$graph;[$n:v]format=rgba,fade=in:st=$from:d=0.4:alpha=1,fade=out:st=$(num "$to - 0.4"):d=0.4:alpha=1[c$i];[$last][c$i]overlay=shortest=1:enable='between(t,$from,$to)'[v$((i + 1))]"
        last=v$((i + 1)); n=$((n + 1)); i=$((i + 1))
    done
    ffmpeg -v error -y "${inputs[@]}" -filter_complex "$graph" -map "[$last]" \
        -c:v libx264 -preset medium -crf 16 -pix_fmt yuv420p -r $FPS "build/clips/$name.mp4"
}

card() { # name
    ffmpeg -v error -y -framerate $FPS -i "build/cards/$1/%05d.png" \
        -c:v libx264 -preset medium -crf 16 -pix_fmt yuv420p "build/clips/$1.mp4"
}

echo "== clips"
card title
card intro
clip 3dwave  30 1   3dwave 0.5 6.8
clip fractal 30 1   fractal 0.5 6.0
clip boing   30 1   boing 0.5 8.5
clip paint   30 1.5 paint 0.4 8.5   paint2 9.5 18
clip jukebox 30 1   jukebox 0.4 9   jukebox2 11 24
clip vortex  60 1   vortex 0.4 7    vortex2 8 19
clip burrow  60 1   burrow 0.4 9    burrow2 11 24
card closing

# Join the clips, each fading into the next through a white flash.
ORDER=(title intro 3dwave fractal boing paint jukebox vortex burrow closing)
echo "== joining ${#ORDER[@]} clips"
inputs=()
for c in "${ORDER[@]}"; do inputs+=(-i "build/clips/$c.mp4"); done
graph="" offset=0 prev="0:v" X=0.5
for ((k = 1; k < ${#ORDER[@]}; k++)); do
    len=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "build/clips/${ORDER[k - 1]}.mp4")
    offset=$(num "$offset + $len - $X")
    graph="$graph[$prev][$k:v]xfade=transition=fadewhite:duration=$X:offset=$offset[x$k];"
    prev="x$k"
done
# Over everything, like a CRT: a glow around bright things only (the curve
# keeps dark areas out of it, so black stays black), faint scanlines and a
# light vignette. It runs in RGB: in YUV, black is not 0, and the glow
# would tint it purple.
graph="$graph[$prev]format=gbrp,split[a][b];[b]curves=all='0/0 0.55/0 1/1',gblur=sigma=7[g];[a][g]blend=all_mode=screen:all_opacity=0.35,"
graph="${graph}drawgrid=w=iw:h=3:t=1:c=black@0.10,vignette=angle=PI/7[out]"
ffmpeg -v error -y "${inputs[@]}" -filter_complex "$graph" -map "[out]" \
    -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p build/video.mp4
total=$(ffprobe -v error -show_entries format=duration -of csv=p=0 build/video.mp4)
echo "   $total s"

echo "== music"
python3 tools/make_music.py build/music.wav "$total"

echo "== final mix"
ffmpeg -v error -y -i build/video.mp4 -i build/music.wav \
    -filter_complex "[1:a]loudnorm=I=-14:TP=-1.5:LRA=11[a]" -map 0:v -map "[a]" \
    -c:v copy -c:a aac -b:a 192k -ar 48000 -movflags +faststart -shortest basika_demo.mp4
echo "== wrote video_demo/basika_demo.mp4 ($(du -h basika_demo.mp4 | cut -f1))"
