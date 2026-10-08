' In palette modes a color number keeps only the mode's low bits, as on the
' VGA: in SCREEN 12, -4 is color 12 and 20 is color 4.
DIM PASS AS INTEGER
PASS = 1
OPEN "tests/color_wrap.result" FOR OUTPUT AS #1
SCREEN 12
PSET (10, 10), -4: IF POINT(10, 10) <> 12 THEN PASS = 0: PRINT #1, "-4 ->"; POINT(10, 10)
PSET (10, 10), 20: IF POINT(10, 10) <> 4 THEN PASS = 0: PRINT #1, "20 ->"; POINT(10, 10)
LINE (0, 0)-(5, 5), 31, BF: IF POINT(2, 2) <> 15 THEN PASS = 0: PRINT #1, "31 ->"; POINT(2, 2)
SCREEN 1
PSET (10, 10), 5: IF POINT(10, 10) <> 1 THEN PASS = 0: PRINT #1, "SCREEN 1: 5 ->"; POINT(10, 10)
SCREEN 13
PSET (10, 10), 200: IF POINT(10, 10) <> 200 THEN PASS = 0: PRINT #1, "SCREEN 13: 200 ->"; POINT(10, 10)
PRINT #1, PASS
CLOSE #1
SYSTEM
