' Without CALL, parentheses wrap a SUB's arguments only when they close at
' the end of the statement; otherwise they belong to the first argument.
Show (1 + 2) * 10, 5
Show (7), 8
Show(5, 6)
CALL Show(3, 4)
IF 1 THEN Show (2) * 2, 9 ELSE PRINT "no"
SUB Show (a, b)
    PRINT a; b
END SUB
