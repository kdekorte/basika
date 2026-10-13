' PAINT fills: each scene prints a checksum of the pixels in a rectangle and
' a count of those in a color. PAINT uploads only the part it painted, so
' fills that reach the edges of the screen or of a VIEW are included.
DIM SHARED edge AS _UNSIGNED LONG
SCREEN _NEWIMAGE(320, 240, 32)
OPEN "tests/paint_fill.result" FOR OUTPUT AS #1
edge = _RGB32(255, 255, 255)

CIRCLE (60, 60), 40, edge
PAINT (60, 60), _RGB32(30, 90, 200), edge
check "solid", 15, 15, 105, 105, _RGB32(30, 90, 200)

LINE (130, 20)-(200, 100), _RGB32(200, 40, 40), BF
LINE (140, 30)-(190, 90), edge, B
PAINT (165, 60), _RGBA32(90, 255, 140, 120), edge
check "alpha", 125, 15, 205, 105, edge

COLOR _RGB32(200, 50, 50), _RGB32(0, 0, 0)
LINE (220, 20)-(300, 100), edge, B
PAINT (260, 60), STRING$(4, &HF0) + STRING$(4, &H0F), edge
check "tile", 215, 15, 305, 105, _RGB32(200, 50, 50)

' A VIEW with no border inside is filled to its edges.
VIEW SCREEN (20, 130)-(100, 200)
PAINT (60, 160), _RGB32(10, 200, 10), edge
VIEW
check "view", 10, 120, 110, 210, _RGB32(10, 200, 10)

' Starting on the border paints nothing.
PAINT (60, 20), _RGB32(1, 2, 3), edge
check "border", 15, 15, 105, 105, _RGB32(1, 2, 3)

' A shape cut off by the screen edges: the fill reaches x = 319 and y = 239.
CIRCLE (300, 230), 40, edge
PAINT (310, 235), _RGB32(250, 200, 0), edge
check "corner", 255, 185, 319, 239, _RGB32(250, 200, 0)

' The rest of the screen around everything drawn so far.
PAINT (130, 220), _RGB32(40, 0, 60), edge
check "rest", 0, 0, 319, 239, _RGB32(40, 0, 60)

SCREEN 12
CIRCLE (100, 100), 50, 15
LINE (90, 60)-(110, 140), 15, B
PAINT (60, 100), 4, 15
PAINT (100, 100), 2, 15
check "12 solid", 45, 45, 155, 155, 4
LINE (300, 50)-(400, 150), 14, B
PAINT (350, 100), CHR$(&HAA) + CHR$(&H55), 14
check "12 tile", 295, 45, 405, 155, 14
VIEW (200, 200)-(300, 300), , 13
CIRCLE (50, 50), 30, 12
PAINT (50, 50), 9, 12
PAINT (5, 5), 6, 13
VIEW
check "12 view", 195, 195, 305, 305, 6

SCREEN 13
CIRCLE (160, 100), 40, 15
PAINT (160, 100), 40, 15
check "13", 115, 55, 205, 145, 40
CLOSE #1
END

SUB check (label$, x0, y0, x1, y1, colr AS _UNSIGNED LONG)
    DIM s AS LONG, n AS LONG, c AS _UNSIGNED LONG
    FOR y = y0 TO y1
        FOR x = x0 TO x1
            c = POINT(x, y)
            s = (s * 7 + (c AND &HFFFFFF)) MOD 999983
            IF c = colr THEN n = n + 1
        NEXT
    NEXT
    PRINT #1, label$; s; n
END SUB
