' QBasic graphics syntax: CIRCLE arcs/aspect, LINE styles, STEP, PRESET,
' PAINT tiles, COLOR foreground/background and POINT cursor queries.
SCREEN 12
_AUTODISPLAY OFF
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/qbasic_primitives.result" FOR OUTPUT AS #1

' Primitives default to the foreground color.
LINE (0, 0)-(5, 0)
IF POINT(2, 0) <> 15 THEN PASS = 0: PRINT #1, "default color"

CIRCLE (100, 100), 50, 14
IF POINT(150, 100) <> 14 OR POINT(100, 50) <> 14 THEN PASS = 0: PRINT #1, "circle"

' Aspect below 1 squashes the vertical radius.
CIRCLE (300, 100), 60, 10, , , .5
IF POINT(360, 100) <> 10 OR POINT(300, 70) <> 10 OR POINT(300, 40) <> 0 THEN PASS = 0: PRINT #1, "aspect"

' Start/end angles draw an arc counterclockwise from 3 o'clock.
CIRCLE (500, 100), 40, 12, 0, 3.14159
IF POINT(500, 60) <> 12 OR POINT(500, 140) <> 0 THEN PASS = 0: PRINT #1, "arc"

' Negative angles add radius lines, making a pie wedge.
CIRCLE (100, 300), 40, 13, -.7854, -5.4978
IF POINT(100, 300) <> 13 OR POINT(120, 300) <> 0 THEN PASS = 0: PRINT #1, "pie"

' The style mask draws four pixels on, four off.
LINE (0, 400)-(15, 400), 11, , &HF0F0
IF POINT(0, 400) <> 11 OR POINT(3, 400) <> 11 OR POINT(4, 400) <> 0 OR POINT(8, 400) <> 11 THEN PASS = 0: PRINT #1, "style"

' STEP coordinates are relative to the last point referenced.
PSET (200, 200), 9
LINE -STEP(10, 0), 9
IF POINT(210, 200) <> 9 OR POINT(0) <> 210 OR POINT(1) <> 200 THEN PASS = 0: PRINT #1, "step"
LINE (250, 250)-STEP(20, 20), 2, B
IF POINT(270, 270) <> 2 OR POINT(260, 260) <> 0 THEN PASS = 0: PRINT #1, "box"

' PRESET draws in the background color.
PRESET (250, 250)
IF POINT(250, 250) <> 0 THEN PASS = 0: PRINT #1, "preset"

' PAINT fills to the border color.
CIRCLE (400, 300), 30, 4
PAINT (400, 300), 6, 4
IF POINT(400, 300) <> 6 OR POINT(440, 300) <> 0 THEN PASS = 0: PRINT #1, "paint"

' A PAINT tile row of four bit planes: plane 0 set gives color 1.
LINE (500, 250)-(560, 310), 15, B
PAINT (530, 280), CHR$(&HFF) + CHR$(0) + CHR$(0) + CHR$(0), 15
IF POINT(530, 280) <> 1 OR POINT(570, 280) <> 0 THEN PASS = 0: PRINT #1, "tile"
' A solid PAINT can repaint the tiled area; STEP(0, 0) reuses the last point.
PAINT STEP(0, 0), 5, 15
IF POINT(530, 280) <> 5 THEN PASS = 0: PRINT #1, "repaint tile"

' PAINT inside a VIEW fills only the view.
VIEW SCREEN (20, 420)-(60, 440)
PAINT (30, 430), 3, 15
VIEW
IF POINT(30, 430) <> 3 OR POINT(20, 440) <> 3 OR POINT(70, 430) <> 0 OR POINT(19, 430) <> 0 THEN PASS = 0: PRINT #1, "paint view"

' POINT(2)/POINT(3) report logical WINDOW coordinates.
WINDOW (0, 0)-(100, 100)
PSET (50, 25), 15
IF POINT(2) <> 50 OR POINT(3) <> 25 OR POINT(0) <> 320 THEN PASS = 0: PRINT #1, "window cursor"
WINDOW

' COLOR sets the default drawing color and the CLS background.
COLOR 14, 1
LINE (0, 450)-(5, 450)
IF POINT(2, 450) <> 14 THEN PASS = 0: PRINT #1, "color fg"
CLS
IF POINT(5, 5) <> 1 THEN PASS = 0: PRINT #1, "cls bg"

' Angles outside -2*PI..2*PI are an illegal function call.
ON ERROR GOTO BadAngle
CIRCLE (100, 100), 10, 15, 7
PASS = 0: PRINT #1, "angle range"
AfterAngle:
SCREENSHOT "tests/qbasic_primitives.png"
PRINT #1, PASS
CLOSE #1
END

BadAngle:
IF ERR <> 5 THEN PASS = 0: PRINT #1, "angle error"; ERR
RESUME AfterAngle
