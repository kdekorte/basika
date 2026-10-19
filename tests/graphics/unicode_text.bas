' QB64 Unicode text: _UPRINTSTRING draws UTF-8 (or codepage, UTF-16,
' UTF-32) text at pixel positions, clipped to maxWidth; _UPRINTWIDTH,
' _UCHARPOS, _UFONTHEIGHT and _ULINESPACING measure it. _MAPUNICODE maps
' the codepage to Unicode (CP437 by default), _CONTROLCHR OFF prints
' control codes as glyphs and _BLINK switches blinking colors.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/unicode_text.result" FOR OUTPUT AS #1
ON ERROR GOTO bad
SCREEN _NEWIMAGE(320, 200, 32)

' The default codepage map is CP437.
IF _MAPUNICODE(1) <> &H263A THEN PASS = 0: PRINT #1, "map 1"; _MAPUNICODE(1)
IF _MAPUNICODE(65) <> 65 THEN PASS = 0: PRINT #1, "map 65"
IF _MAPUNICODE(127) <> &H2302 THEN PASS = 0: PRINT #1, "map 127"
IF _MAPUNICODE(219) <> &H2588 THEN PASS = 0: PRINT #1, "map 219"
_MAPUNICODE &H20AC TO 128
IF _MAPUNICODE(128) <> &H20AC THEN PASS = 0: PRINT #1, "remap"

IF _CONTROLCHR <> 0 THEN PASS = 0: PRINT #1, "controlchr on"
_CONTROLCHR OFF
IF _CONTROLCHR <> -1 THEN PASS = 0: PRINT #1, "controlchr off"
_CONTROLCHR ON
IF _BLINK <> -1 THEN PASS = 0: PRINT #1, "blink on"
_BLINK OFF
IF _BLINK <> 0 THEN PASS = 0: PRINT #1, "blink off"
_BLINK ON

' "Héllo" in UTF-8 is 6 bytes but 5 code points.
DIM p(10) AS LONG
t$ = "H" + CHR$(195) + CHR$(169) + "llo"
n = _UCHARPOS(t$, p(), 8)
IF n <> 5 THEN PASS = 0: PRINT #1, "charpos count"; n
IF p(0) <> 0 OR p(1) <= 0 OR p(5) <> _UPRINTWIDTH(t$, 8) THEN PASS = 0: PRINT #1, "charpos"; p(0); p(1); p(5)
FOR i = 1 TO 5
    IF p(i) <= p(i - 1) THEN PASS = 0: PRINT #1, "charpos order"; i
NEXT
' Filling can start at any element.
p(1) = 99
n = _UCHARPOS("ab", p(3))
IF n <> 2 OR p(3) <> 0 OR p(1) <> 99 THEN PASS = 0: PRINT #1, "charpos start"
' Invalid UTF-8 has no width and no code points, and leaves the array.
p(0) = 7
IF _UPRINTWIDTH(CHR$(255), 8) <> 0 THEN PASS = 0: PRINT #1, "bad utf8 width"
IF _UCHARPOS(CHR$(255), p(), 8) <> 0 OR p(0) <> 7 THEN PASS = 0: PRINT #1, "bad utf8 charpos"
' UTF-16 and UTF-32 (little-endian) measure like the same text in UTF-8.
IF _UPRINTWIDTH("A" + CHR$(0) + "B" + CHR$(0), 16) <> _UPRINTWIDTH("AB", 8) THEN PASS = 0: PRINT #1, "utf16"
IF _UPRINTWIDTH(MKL$(65) + MKL$(66), 32) <> _UPRINTWIDTH("AB", 8) THEN PASS = 0: PRINT #1, "utf32"
IF _UPRINTWIDTH("abc") <= 0 THEN PASS = 0: PRINT #1, "width"
IF _UFONTHEIGHT <= 0 OR _ULINESPACING < _UFONTHEIGHT THEN PASS = 0: PRINT #1, "metrics"; _UFONTHEIGHT; _ULINESPACING

' Drawing: text in white on black, clipped at maxWidth.
CLS
COLOR _RGB32(255, 255, 255), _RGB32(0, 0, 0)
_UPRINTSTRING (0, 0), STRING$(20, 219), 30
lit = 0: beyond = 0
FOR x = 0 TO 29
    IF POINT(x, _UFONTHEIGHT \ 2) = _RGB32(255, 255, 255) THEN lit = lit + 1
NEXT
FOR x = 31 TO 200
    IF POINT(x, _UFONTHEIGHT \ 2) <> _RGB32(0, 0, 0) THEN beyond = beyond + 1
NEXT
IF lit < 20 THEN PASS = 0: PRINT #1, "drawn"; lit
IF beyond <> 0 THEN PASS = 0: PRINT #1, "clipped"; beyond

' Bad arguments.
errors = 0
_MAPUNICODE 65 TO 256
x = _UPRINTWIDTH("a", 7)
_UPRINTSTRING (0, 0), "a", , , 12345
IF errors <> 3 THEN PASS = 0: PRINT #1, "errors"; errors

PRINT #1, PASS
CLOSE #1
SYSTEM
bad: errors = errors + 1: RESUME NEXT
