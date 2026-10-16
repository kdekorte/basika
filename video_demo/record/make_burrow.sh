#!/usr/bin/env bash
# Makes build/burrow_rec.bas from demo/burrow/burrow.bas for the video. Run
# with --selftest N and BASIKA_DATA_DIR=build/ (so burrow.hi stays out of
# demo/): the title screen's demo plays for TITLE frames, then a real game
# starts and the autopilot plays it, and every frame is saved to
# build/frames/burrow/.
set -euo pipefail
cd "$(dirname "$0")/.."
TITLE=${1:-120}
python3 - "$TITLE" <<'PY'
import sys
title = sys.argv[1]
src = open('../demo/burrow/burrow.bas').read()
def once(old, new):
    global src
    assert src.count(old) == 1, 'burrow: not found once: ' + old
    src = src.replace(old, new)
once('    p$ = ARGV$(0)\n', '    p$ = "../demo/burrow/burrow.bas"\n')
once('    _DISPLAY\n    Frame = Frame + 1\n',
     '    _DISPLAY\n'
     '    SCREENSHOT "build/frames/burrow/" + RIGHT$("00000" + LTRIM$(STR$(Frame + 1)), 5) + ".png"\n'
     '    IF Frame = ' + title + ' THEN StartGame\n'
     '    Frame = Frame + 1\n')
# In the real game the autopilot holds the controls.
once('    InFirePressed = (InFire AND NOT LastFire) OR jPress\n    LastFire = InFire\nEND SUB\n',
     '    InFirePressed = (InFire AND NOT LastFire) OR jPress\n    LastFire = InFire\n    Autopilot\nEND SUB\n')
open('build/burrow_rec.bas', 'w').write(src)
PY
