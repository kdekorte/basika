' _LOADFONT follows QB64: loaded fonts get handles from 32, a font that cannot
' be loaded is -1, styles may be given, _FONT returns the font in use, and
' QB64's built-in handles (8, 14, 16, ...) or 0 select the default font.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/font_handles.result" FOR OUTPUT AS #1
SCREEN _NEWIMAGE(400, 200, 32)
f& = _LOADFONT("fonts/ModernDOS8x16.ttf", 24)
b& = _LOADFONT("fonts/ModernDOS8x16.ttf", 24, "BOLD, UNDERLINE")
m& = _LOADFONT("fonts/ModernDOS8x16.ttf", 24, "monospace")
bad& = _LOADFONT("no_such_font.ttf", 20)
IF f& < 32 OR b& < 32 OR m& < 32 OR bad& <> -1 THEN PASS = 0: PRINT #1, "handles"; f&; b&; m&; bad&
IF _FONT <> 16 THEN PASS = 0: PRINT #1, "default"; _FONT
w = _PRINTWIDTH("Hello")
_FONT f&
IF _FONT <> f& OR _PRINTWIDTH("Hello") <= w THEN PASS = 0: PRINT #1, "select"; _FONT
_FONT 16: IF _FONT <> 16 OR _PRINTWIDTH("Hello") <> w THEN PASS = 0: PRINT #1, "back to 16"
_FONT 8: _FONT 0: _FONT 14: IF _FONT <> 16 THEN PASS = 0: PRINT #1, "built-in handles"
_FREEFONT m&
errors = 0
ON ERROR GOTO bad
_FONT m&
_FONT 5
_FREEFONT 16
x& = _LOADFONT("fonts/ModernDOS8x16.ttf", 10, "WIGGLY")
ON ERROR GOTO 0
IF errors <> 4 THEN PASS = 0: PRINT #1, "errors"; errors
PRINT #1, PASS
CLOSE #1
SYSTEM
bad: IF ERR = 5 THEN errors = errors + 1
RESUME NEXT
