' QB64 color functions in 32-bit images and palette modes.
DIM PASS AS INTEGER
DIM C AS _UNSIGNED LONG
PASS = 1
OPEN "tests/rgb_functions.result" FOR OUTPUT AS #1

SCREEN _NEWIMAGE(320, 200, 32)
IF HEX$(_RGB32(1, 2, 3)) <> "FF010203" THEN PASS = 0: PRINT #1, "rgb32"
IF HEX$(_RGBA32(1, 2, 3, 4)) <> "4010203" THEN PASS = 0: PRINT #1, "rgba32"
IF HEX$(_RGB32(128)) <> "FF808080" THEN PASS = 0: PRINT #1, "rgb32 gray"
IF HEX$(_RGB32(10, 20)) <> "140A0A0A" THEN PASS = 0: PRINT #1, "rgb32 gray alpha"
IF HEX$(_RGB32(300, -5, 7, 999)) <> "FFFF0007" THEN PASS = 0: PRINT #1, "rgb32 clamp"
IF HEX$(_RGB(255, 0, 0)) <> "FFFF0000" THEN PASS = 0: PRINT #1, "rgb in 32-bit"
C = &HC0102030
IF _RED32(C) <> 16 OR _GREEN32(C) <> 32 OR _BLUE32(C) <> 48 OR _ALPHA32(C) <> 192 THEN PASS = 0: PRINT #1, "components32"
IF _RED(C) <> 16 OR _ALPHA(C) <> 192 THEN PASS = 0: PRINT #1, "components"
IF _WIDTH <> 320 OR _HEIGHT <> 200 THEN PASS = 0: PRINT #1, "size"
IF ABS(_PI - 3.14159265358979#) > 1D-12 OR ABS(_PI(2) - 6.28318530717959#) > 1D-12 THEN PASS = 0: PRINT #1, "pi"

' The foreground starts opaque white and CLS uses the background color.
LINE (0, 0)-(3, 0)
C = POINT(1, 0)
IF C <> _RGB32(255, 255, 255) THEN PASS = 0: PRINT #1, "default fg"
COLOR _RGB32(255, 255, 0), _RGB32(0, 0, 128)
CLS
C = POINT(10, 10)
IF C <> _RGB32(0, 0, 128) THEN PASS = 0: PRINT #1, "cls bg"
' Negative LONG literals are the same colors as their unsigned forms.
PSET (20, 20), &HFF00FF00
C = POINT(20, 20)
IF C <> _RGB32(0, 255, 0) THEN PASS = 0: PRINT #1, "long color"
' Fully transparent drawing leaves pixels unchanged.
PSET (20, 20), _RGBA32(255, 0, 0, 0)
IF POINT(20, 20) <> C THEN PASS = 0: PRINT #1, "alpha zero"

' In palette modes _RGB picks the nearest palette entry.
SCREEN 13
IF _RGB(255, 255, 255) <> 15 THEN PASS = 0: PRINT #1, "nearest white"
IF _RED(12) <> 255 OR _GREEN(12) <> 85 OR _BLUE(12) <> 85 THEN PASS = 0: PRINT #1, "palette components"
SCREEN 12
IF _RGB(170, 0, 0) <> 4 THEN PASS = 0: PRINT #1, "16 color nearest"

PRINT #1, PASS
CLOSE #1
END
