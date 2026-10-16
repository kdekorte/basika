#!/usr/bin/env bash
# Makes build/jukebox_rec.bas from demo/jukebox/jukebox.bas for the video:
# its SelfTestStep is replaced by record/jukebox_director.bas, its database
# is a fresh build/jukebox/jukebox.dat (the real one is not touched), it
# reads its fonts and songs from demo/, and every frame, with a pointer on
# top, is saved to build/frames/jukebox/. Run it with sound on (no
# --no-audio) so the songs play and the progress bar moves.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PY'
src = open('../demo/jukebox/jukebox.bas').read()
def once(old, new):
    global src
    assert src.count(old) == 1, 'jukebox: not found once: ' + old
    src = src.replace(old, new)
once('    p$ = ARGV$(0)\n', '    p$ = "../demo/jukebox/jukebox.bas"\n')
# A fresh database named jukebox.dat, in build/jukebox/ (the footer shows
# the name), so the real one keeps its play counts.
once('    DbName = "jukebox_selftest.dat"\n    OPEN BaseDir + DbName FOR OUTPUT AS #1',
     '    DbName = "jukebox.dat"\n    OPEN "build/jukebox/" + DbName FOR OUTPUT AS #1')
once('    OPEN BaseDir + DbName FOR RANDOM AS #f LEN = REC_LEN',
     '    OPEN "build/jukebox/" + DbName FOR RANDOM AS #f LEN = REC_LEN')
once('DIM SHARED Accent(1 TO MAX_SONGS)\n',
     'DIM SHARED Accent(1 TO MAX_SONGS)\n'
     'DIM SHARED RecX, RecY, RecSX, RecSY, RecTX, RecTY, RecT0, RecT1, RecClick\n')
once('    Render\n    _DISPLAY\n    Frame = Frame + 1\n',
     '    Render\n    DrawPointer RecX, RecY\n    _DISPLAY\n'
     '    SCREENSHOT "build/frames/jukebox/" + RIGHT$("00000" + LTRIM$(STR$(Frame + 1)), 5) + ".png"\n'
     '    _LIMIT 30\n'
     '    Frame = Frame + 1\n')
# The real song end check, so the player empties when a song finishes.
once('    IF Current = 0 OR Paused OR SelfTest THEN EXIT SUB\n', '    IF Current = 0 OR Paused THEN EXIT SUB\n')
start = src.index('\nSUB SelfTestStep\n')
end = src.index('\nEND SUB\n', start) + len('\nEND SUB\n')
src = src[:start] + '\n' + src[end:] + '\n' + open('record/jukebox_director.bas').read()
open('build/jukebox_rec.bas', 'w').write(src)
PY
