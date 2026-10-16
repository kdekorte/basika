' The performance check: about 1.5 to 2 seconds of the interpreter's hot
' paths, as a guard against something becoming much slower. It runs
'   - a counting loop (the interpreter's statement loop),
'   - per-pixel math from a waving-flag demo (compiled expressions, SIN, COS,
'     INT, _ROUND, arrays),
'   - texture mapping with POINT and PSET, and
'   - CIRCLE and PAINT fills, solid, translucent and inside a VIEW,
' then writes a checksum of the results to tests/benchmark.result, so a
' wrong answer fails as well as a slow one. (PRINT goes to the graphics
' screen once there is one, so the result is a file.)
DEFLNG A-Z
DIM t AS DOUBLE, z AS DOUBLE, xx AS DOUBLE, yy AS DOUBLE, dx AS DOUBLE, dy AS DOUBLE
DIM pal(15)

' The statement loop.
count = 0
FOR i = 1 TO 3000000
    count = count + 1
NEXT

' The math.
FOR i = 0 TO 15: pal(i) = i * 17: NEXT
a = 40: w = 300: h = 160
FOR frame = 1 TO 10
    t = t + .2
    FOR y = 0 TO h
        FOR x = 0 TO w
            z = (.1 + .4 * (x / w)) * a * SIN(x / 35 - y / 70 - t) + .5 * a
            dz = 50 * a * COS(x / 35 - y / 70 - t) / 35
            xx = x + z * .707 - a * .707
            yy = y - z * .707
            IF INT(xx) >= 0 AND INT(xx) < w - 1 AND INT(yy) >= 0 AND INT(yy) < h - 1 THEN
                dx = xx - INT(xx)
                dy = yy - INT(yy)
                r = _ROUND((1 - dy) * ((1 - dx) * pal(INT(xx) MOD 16) + dx * pal((INT(xx) + 1) MOD 16)) + dy * pal(INT(yy) MOD 16)) + dz
                IF r < 0 THEN r = 0
                IF r > 255 THEN r = 255
                sum = (sum + r) MOD 1000000
            END IF
        NEXT
    NEXT
NEXT

' Texture mapping: four POINTs of an image for every PSET on the screen.
SCREEN _NEWIMAGE(320, 200, 32)
img = _NEWIMAGE(160, 100, 32)
_DEST img
FOR i = 0 TO 9
    LINE (0, i * 10)-(159, i * 10 + 4), _RGB32(i * 25, 255 - i * 25, 128), BF
NEXT
_DEST 0
_SOURCE img
FOR frame = 1 TO 30
    CLS
    FOR y = 0 TO 98
        FOR x = 0 TO 158
            c = POINT(x, y): r = POINT(x + 1, y): d = POINT(x, y + 1): e = POINT(x + 1, y + 1)
            PSET (x * 2 + frame MOD 12, y * 2), _RGB32((_RED32(c) + _RED32(e)) \ 2, _GREEN32(r), _BLUE32(d))
        NEXT
    NEXT
    _DISPLAY
NEXT
_SOURCE 0
FOR y = 0 TO 199 STEP 7
    FOR x = 0 TO 319 STEP 7
        pixels = (pixels + (POINT(x, y) AND &HFFFFFF) MOD 9973) MOD 1000000
    NEXT
NEXT

' PAINT: outline 24 circles and fill each one.
SCREEN _NEWIMAGE(800, 600, 32)
DIM edge AS _UNSIGNED LONG
edge = _RGB32(255, 255, 255)
FOR frame = 1 TO 250
    CLS
    FOR i = 0 TO 23
        cx = 60 + (i MOD 8) * 95: cy = 80 + (i \ 8) * 180
        CIRCLE (cx, cy), 40, edge
        IF i MOD 2 = 0 THEN
            PAINT (cx, cy), _RGB32((i * 37 + frame) MOD 256, 100, 200), edge
        ELSE
            VIEW SCREEN (cx - 45, cy - 45)-(cx + 45, cy + 45)
            PAINT (cx, cy), _RGBA32(90, 255, 140, 120), edge
            VIEW
        END IF
    NEXT
    _DISPLAY
NEXT
FOR i = 0 TO 23
    cx = 60 + (i MOD 8) * 95: cy = 80 + (i \ 8) * 180
    pixels = (pixels + (POINT(cx, cy) AND &HFFFFFF) MOD 9973) MOD 1000000
NEXT

OPEN "tests/benchmark.result" FOR OUTPUT AS #1
PRINT #1, "Count:"; count; "Checksum:"; sum; "Pixels:"; pixels
CLOSE #1
SYSTEM
