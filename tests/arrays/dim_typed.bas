' DIM name AS type declares typed scalars and arrays without suffixes.
DIM Count AS INTEGER, Big AS LONG, Ratio AS SINGLE, Precise AS DOUBLE
DIM Label AS STRING, Tint AS _UNSIGNED LONG
DIM Scores(1 TO 3) AS INTEGER
DIM Names(2) AS STRING
DIM Grid(-1 TO 1, 0 TO 2) AS LONG
Count = 7.6
Big = 100000
Ratio = 1 / 3
Precise = 1 / 3
Label = "typed"
Tint = -1
PRINT Count; Big; Ratio; Precise
PRINT Label; " "; Tint
Tint = Tint + 2
PRINT Tint
Scores(1) = 1.4: Scores(3) = 3.6
Names(0) = "zero": Names(2) = "two"
Grid(-1, 0) = 70000: Grid(1, 2) = -5
PRINT Scores(1); Scores(3); Names(0); " "; Names(2); Grid(-1, 0); Grid(1, 2)
' Suffix forms address the same storage.
PRINT Count%; Big&; Label$
' Typed parameters convert their arguments.
CALL Report(9.6, "param", 3000000000)
PRINT Half&(9)
ON ERROR GOTO Overflowed
Count = 40000
PRINT "not reached"
END
Overflowed:
PRINT "Overflow trapped:"; ERR
END

SUB Report (n AS INTEGER, s AS STRING, c AS _UNSIGNED LONG)
    PRINT n; s; c
END SUB

FUNCTION Half& (v AS LONG)
    Half& = v / 2
END FUNCTION
