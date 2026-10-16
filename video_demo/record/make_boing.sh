#!/usr/bin/env bash
# Makes build/boing_rec.bas from demo/boing_ball.bas for the video: the ball
# moves exactly 1/30 second per frame (two of its 1/60 second steps), PLAY is
# silent, there is no FPS counter, and every frame is saved to
# build/frames/boing/ until FRAMES frames have been saved.
set -euo pipefail
cd "$(dirname "$0")/.."
FRAMES=${1:-270}
sed -e 's/^	KEY\$ = INKEY\$$/	KEY$ = ""/' \
    -e 's/^	STEPS = (NOW_TICK# - LAST_TICK#) \* 60$/	STEPS = 2/' \
    -e 's/PLAY "MBT240[^"]*"/REM/' \
    -e 's/^		CALL DrawFps$/		REM no FPS counter in the video/' \
    -e "s#^		_DISPLAY\$#		_DISPLAY: RecFrame = RecFrame + 1: SCREENSHOT \"build/frames/boing/\" + RIGHT\$(\"00000\" + LTRIM\$(STR\$(RecFrame)), 5) + \".png\": IF RecFrame >= $FRAMES THEN SYSTEM#" \
    ../demo/boing_ball.bas > build/boing_rec.bas
# Every edit must have matched exactly once.
for pat in 'KEY\$ = ""' 'STEPS = 2' 'no FPS counter' 'RecFrame >= '; do
    n=$(grep -c "$pat" build/boing_rec.bas || true)
    [ "$n" = 1 ] || { echo "make_boing.sh: expected one match for $pat, found $n" >&2; exit 1; }
done
[ "$(grep -c 'PLAY "' build/boing_rec.bas || true)" = 0 ] || { echo "make_boing.sh: PLAY left in" >&2; exit 1; }
