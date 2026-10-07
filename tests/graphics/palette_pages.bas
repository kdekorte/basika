' PALETTE recolors pixels already on screen (attributes stay the same);
' SCREEN apage/vpage and PCOPY give multiple pages for double buffering.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/palette_pages.result" FOR OUTPUT AS #1

SCREEN 13
LINE (0, 0)-(9, 9), 1, BF
PALETTE 1, 63                 ' red = 63, green = 0, blue = 0
IF POINT(5, 5) <> 1 THEN PASS = 0: PRINT #1, "attribute kept"
SCREENSHOT "tests/palette_pages.png"
DIM cycle(255) AS LONG
FOR i = 0 TO 255: cycle(i) = -1: NEXT
cycle(1) = 63 * 256           ' green
PALETTE USING cycle(0)
PALETTE
ON ERROR GOTO Trap
PALETTE 300, 0
PALETTE 1, 64
ON ERROR GOTO 0

' SCREEN 7 has 8 pages: draw on hidden page 1, then copy it to page 0.
SCREEN 7, 0, 1, 0
LINE (0, 0)-(20, 20), 14, BF
SCREEN , , 0, 0
IF POINT(10, 10) <> 0 THEN PASS = 0: PRINT #1, "page 0 untouched"
PCOPY 1, 0
IF POINT(10, 10) <> 14 THEN PASS = 0: PRINT #1, "pcopy"
FOR f = 1 TO 4
    SCREEN 7, 0, f MOD 2, 1 - f MOD 2
NEXT
SCREEN 7, 0, 1, 1
IF POINT(10, 10) <> 14 THEN PASS = 0: PRINT #1, "repeat SCREEN keeps pages"
ON ERROR GOTO Trap
SCREEN 7, 0, 8, 0
PCOPY 0, 9
ON ERROR GOTO 0
IF errors <> 4 THEN PASS = 0: PRINT #1, "errors"; errors
PRINT #1, PASS
CLOSE #1
END

Trap:
IF ERR = 5 THEN errors = errors + 1
RESUME NEXT
