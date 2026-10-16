#!/usr/bin/env bash
# Makes build/paint_rec.bas from demo/qb64_paint.bas for the video: its
# SelfTest SUB is replaced by record/paint_director.bas, which paints a
# picture with a gliding pointer and saves every frame to build/frames/paint/.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PY'
src = open('../demo/qb64_paint.bas').read()
start = src.index('\nSUB SelfTest\n')
end = src.index('\nEND SUB\n', start) + len('\nEND SUB\n')
shared = 'DIM SHARED RecFrame AS LONG, PX AS INTEGER, PY AS INTEGER\n'
anchor = 'DIM SHARED BarDrag AS INTEGER, BarGrab AS INTEGER\n'
assert src.count(anchor) == 1, 'paint: anchor for the recording variables not found'
src = src.replace(anchor, anchor + shared)
start = src.index('\nSUB SelfTest\n')
end = src.index('\nEND SUB\n', start) + len('\nEND SUB\n')
director = open('record/paint_director.bas').read()
open('build/paint_rec.bas', 'w').write(src[:start] + '\n' + src[end:] + '\n' + director)
PY
