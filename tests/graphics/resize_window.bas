' $RESIZE and _RESIZE: the window becomes user-resizable, _RESIZEWIDTH and
' _RESIZEHEIGHT report its size, and a size the program sets itself (SCREEN
' _NEWIMAGE) is not reported by _RESIZE as a user resize.
$RESIZE:ON
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/resize_window.result" FOR OUTPUT AS #1
SCREEN _NEWIMAGE(400, 300, 32)
FOR i = 1 TO 5: _LIMIT 100: NEXT
IF _RESIZE THEN PASS = 0: PRINT #1, "program resize reported"
IF _RESIZEWIDTH <> 400 OR _RESIZEHEIGHT <> 300 THEN PASS = 0: PRINT #1, "size"; _RESIZEWIDTH; _RESIZEHEIGHT
SCREEN _NEWIMAGE(_RESIZEWIDTH, _RESIZEHEIGHT, 32)
IF _WIDTH <> 400 OR _HEIGHT <> 300 THEN PASS = 0: PRINT #1, "newimage"
IF _RESIZE <> 0 THEN PASS = 0: PRINT #1, "flag"

' The statement form switches resizing and the scaling method.
_RESIZE ON, _STRETCH
_RESIZE ON, _SMOOTH
_RESIZE OFF
_RESIZE
_RESIZE ON
ON ERROR GOTO bad
_RESIZE ON, _BOGUS
ON ERROR GOTO 0
PASS = 0: PRINT #1, "bad method accepted"
done:
ON ERROR GOTO 0
PRINT #1, PASS
CLOSE #1
END
bad:
IF ERR <> 2 THEN PASS = 0: PRINT #1, "err"; ERR
RESUME done
