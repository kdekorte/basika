' A single-line IF runs only one branch inside a procedure, too, and a
' PRINT ending in ; or , stops at ELSE.
FUNCTION Wrap% (i AS INTEGER)
    IF i >= 15 THEN Wrap% = 0 ELSE Wrap% = i + 1
END FUNCTION
SUB Show (i AS INTEGER)
    IF i > 0 THEN PRINT "pos"; ELSE PRINT "neg";
    PRINT "|"
END SUB
PRINT Wrap%(14); Wrap%(15)
Show 1
Show -1
IF 1 THEN PRINT "a", ELSE PRINT "b",
PRINT "|"
