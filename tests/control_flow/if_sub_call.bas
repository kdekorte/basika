' A SUB can be called without CALL after THEN, after ELSE, or after a colon.
P = 0
IF P = 0 THEN Show 3
IF P = 1 THEN Show 4 ELSE Show 5
IF P = 0 THEN Show 6: PRINT "same line"
A = 1: Show 7: PRINT "after colon"
Outer

SUB Outer
    IF 1 THEN Show 8 ELSE Show 9
    X = 2: Show X * 5
END SUB

SUB Show (v)
    PRINT "show"; v
END SUB
