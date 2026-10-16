#!/usr/bin/env bash
# Makes build/vortex_rec.bas from demo/vortex/vortex.bas for the video. Run
# with --selftest N: the title screen's attract demo plays for TITLE frames,
# then a real game starts and the autopilot plays it (with one superzapper
# at ZAP), and every frame is saved to build/frames/vortex/.
set -euo pipefail
cd "$(dirname "$0")/.."
TITLE=${1:-150}
ZAP=${2:-420}
python3 - "$TITLE" "$ZAP" <<'PY'
import sys
title, zap = sys.argv[1], sys.argv[2]
src = open('../demo/vortex/vortex.bas').read()
def once(old, new):
    global src
    assert src.count(old) == 1, 'vortex: not found once: ' + old
    src = src.replace(old, new)
# Assets come from the real demo folder, wherever this copy is.
once('    p$ = ARGV$(0)\n', '    p$ = "../demo/vortex/vortex.bas"\n')
# Save every frame; start a real game after the title.
once('    _DISPLAY\n    Frame = Frame + 1\n',
     '    _DISPLAY\n'
     '    SCREENSHOT "build/frames/vortex/" + RIGHT$("00000" + LTRIM$(STR$(Frame + 1)), 5) + ".png"\n'
     '    IF Frame = ' + title + ' THEN StartGame\n'
     '    Frame = Frame + 1\n')
# In the real game the autopilot holds the controls.
once('        InFire = _KEYDOWN(32) OR STRIG(1)\n    END IF\n',
     '        InFire = _KEYDOWN(32) OR STRIG(1)\n'
     '        Autopilot\n'
     '        IF Frame = ' + zap + ' THEN InZap = -1\n'
     '    END IF\n')
open('build/vortex_rec.bas', 'w').write(src)
PY
