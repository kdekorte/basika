' PEN reads the mouse as a light pen and STICK reads joysticks; with neither
' in use (headless) they report no press and no joystick.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/pen_stick.result" FOR OUTPUT AS #1
SCREEN 12
ON PEN GOSUB Pressed
PEN ON
FOR n = 0 TO 3
    IF n = 0 OR n = 3 THEN
        IF PEN(n) <> 0 THEN PASS = 0: PRINT #1, "pen"; n
    END IF
NEXT
IF PEN(6) < 1 OR PEN(9) < 1 THEN PASS = 0: PRINT #1, "pen text cell"
PEN STOP
PEN OFF
FOR n = 0 TO 3
    IF STICK(n) <> 0 THEN PASS = 0: PRINT #1, "stick"; n
NEXT
IF STRIG(0) <> 0 THEN PASS = 0: PRINT #1, "strig"
ON ERROR GOTO Bad
x = PEN(10)
y = STICK(4)
ON ERROR GOTO 0
IF errors <> 2 THEN PASS = 0: PRINT #1, "range errors"; errors
PRINT #1, PASS
CLOSE #1
END
Pressed:
RETURN
Bad:
IF ERR = 5 THEN errors = errors + 1
RESUME NEXT
