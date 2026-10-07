' Off-screen images: _NEWIMAGE handles, _DEST/_SOURCE, _PUTIMAGE between
' images (native size, stretched, mirrored), _COPYIMAGE, _WIDTH/_HEIGHT,
' SCREEN handle, plus the idle QB64 keyboard and mouse functions.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/offscreen_images.result" FOR OUTPUT AS #1
SCREEN _NEWIMAGE(320, 200, 32)
_TITLE "offscreen images"
sprite = _NEWIMAGE(20, 20, 32)
IF sprite >= -1 THEN PASS = 0: PRINT #1, "handle"
IF _WIDTH(sprite) <> 20 OR _HEIGHT(sprite) <> 20 OR _WIDTH(0) <> 320 OR _WIDTH <> 320 THEN PASS = 0: PRINT #1, "sizes"

_DEST sprite
IF _DEST <> sprite OR _WIDTH <> 20 THEN PASS = 0: PRINT #1, "dest"
LINE (0, 0)-(4, 4), _RGB32(255, 0, 0), BF
LINE (5, 5)-(19, 19), _RGB32(0, 255, 0), BF
_DEST 0
LINE (0, 0)-(319, 199), _RGB32(0, 0, 80), BF

' POINT reads _SOURCE: new 32-bit images start transparent.
_SOURCE sprite
IF POINT(2, 2) <> _RGB32(255, 0, 0) OR POINT(19, 0) <> 0 THEN PASS = 0: PRINT #1, "source"
_SOURCE 0

_PUTIMAGE (10, 10), sprite
IF POINT(12, 12) <> _RGB32(255, 0, 0) OR POINT(29, 10) <> _RGB32(0, 0, 80) THEN PASS = 0: PRINT #1, "native"
_PUTIMAGE (60, 10)-(99, 49), sprite
IF POINT(65, 15) <> _RGB32(255, 0, 0) OR POINT(97, 47) <> _RGB32(0, 255, 0) THEN PASS = 0: PRINT #1, "stretched"
_PUTIMAGE (139, 10)-(100, 49), sprite
IF POINT(136, 13) <> _RGB32(255, 0, 0) THEN PASS = 0: PRINT #1, "mirrored"

copy = _COPYIMAGE(sprite)
_SOURCE copy
IF POINT(2, 2) <> _RGB32(255, 0, 0) THEN PASS = 0: PRINT #1, "copy"
_SOURCE 0
_PUTIMAGE (200, 100), copy, 0, (5, 5)-(9, 9)
IF POINT(202, 102) <> _RGB32(0, 255, 0) THEN PASS = 0: PRINT #1, "source rectangle"
_FREEIMAGE copy

pal = _NEWIMAGE(8, 8, 256)
_DEST pal: PSET (1, 1), 12: _DEST 0
_SOURCE pal
IF POINT(1, 1) <> 12 THEN PASS = 0: PRINT #1, "palette image"
_SOURCE 0
SCREEN pal
IF _WIDTH <> 8 OR POINT(1, 1) <> 12 THEN PASS = 0: PRINT #1, "screen handle"

IF _KEYDOWN(27) <> 0 OR _KEYHIT <> 0 OR _MOUSEINPUT <> 0 OR _MOUSEBUTTON(1) <> 0 OR _MOUSEWHEEL <> 0 THEN PASS = 0: PRINT #1, "input idle"

ON ERROR GOTO Bad
_DEST -99
_PUTIMAGE , -99
x = _WIDTH(-99)
ON ERROR GOTO 0
IF errors <> 3 THEN PASS = 0: PRINT #1, "errors"; errors
PRINT #1, PASS
CLOSE #1
END
Bad:
IF ERR = 5 THEN errors = errors + 1
RESUME NEXT
