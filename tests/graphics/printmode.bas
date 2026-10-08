' _PRINTMODE: _FILLBACKGROUND (the default, also for each new screen) fills
' the box behind PRINT and _PRINTSTRING text with the COLOR background,
' _KEEPBACKGROUND leaves it, _ONLYBACKGROUND draws only the box. The mode
' belongs to each image, and _PRINTMODE(handle) reads it back.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/printmode.result" FOR OUTPUT AS #1
SCREEN _NEWIMAGE(200, 100, 32)
IF _PRINTMODE <> 3 THEN PASS = 0: PRINT #1, "default"; _PRINTMODE
LINE (0, 0)-(199, 99), _RGB32(0, 0, 255), BF
COLOR _RGB32(255, 255, 255), _RGB32(255, 0, 0)
_PRINTSTRING (10, 10), "Hi"
IF POINT(10, 10) <> _RGB32(255, 0, 0) THEN PASS = 0: PRINT #1, "fill"; HEX$(POINT(10, 10))
_PRINTMODE _KEEPBACKGROUND
IF _PRINTMODE <> 1 THEN PASS = 0: PRINT #1, "keep mode"
_PRINTSTRING (10, 40), "Hi"
IF POINT(10, 40) <> _RGB32(0, 0, 255) THEN PASS = 0: PRINT #1, "keep"; HEX$(POINT(10, 40))
_PRINTMODE _ONLYBACKGROUND
_PRINTSTRING (100, 10), "Hi"
w = _PRINTWIDTH("Hi")
FOR x = 100 TO 100 + w - 1
    IF POINT(x, 18) <> _RGB32(255, 0, 0) THEN PASS = 0
NEXT
IF PASS = 0 THEN PRINT #1, "only"
img& = _NEWIMAGE(20, 20, 32)
IF _PRINTMODE(img&) <> 3 THEN PASS = 0: PRINT #1, "new image"
_PRINTMODE _KEEPBACKGROUND, img&
IF _PRINTMODE(img&) <> 1 OR _PRINTMODE <> 2 THEN PASS = 0: PRINT #1, "per image"
SCREEN _NEWIMAGE(200, 100, 32)
IF _PRINTMODE <> 3 THEN PASS = 0: PRINT #1, "new screen"
errors = 0
ON ERROR GOTO bad
_PRINTMODE _KEEPBACKGROUND, 12345
x = _PRINTMODE(-99)
ON ERROR GOTO 0
IF errors <> 2 THEN PASS = 0: PRINT #1, "errors"; errors
PRINT #1, PASS
CLOSE #1
SYSTEM
bad: errors = errors + 1: RESUME NEXT
