' POINT benchmark: read four pixels of an image for every pixel drawn on the
' screen, as a texture-mapping demo does.
SCREEN _NEWIMAGE(320, 200, 32)
img = _NEWIMAGE(160, 100, 32)
_DEST img
FOR i = 0 TO 9
    LINE (0, i * 10)-(159, i * 10 + 4), _RGB32(i * 25, 255 - i * 25, 128), BF
NEXT
_DEST 0
_SOURCE img
FOR frame = 1 TO 12
    CLS
    FOR y = 0 TO 98
        FOR x = 0 TO 158
            c = POINT(x, y): r = POINT(x + 1, y): d = POINT(x, y + 1): e = POINT(x + 1, y + 1)
            PSET (x * 2 + frame, y * 2), _RGB32((_RED32(c) + _RED32(e)) \ 2, _GREEN32(r), _BLUE32(d))
        NEXT
    NEXT
    _DISPLAY
NEXT
