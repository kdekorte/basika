' Compiled expressions in a program with procedures: FUNCTION calls look like
' array elements, and SUB locals resolve per call.
DECLARE FUNCTION sq (x)
DIM SHARED g(5)
g(2) = 4
PRINT sq(3) + 1, sq(g(2)) * 2, g(2) + sq(1)
CALL sub1(5)
CALL sub1(6)
FUNCTION sq (x)
    sq = x * x + g(2)
END FUNCTION
SUB sub1 (n)
    lc(1) = n * 2
    PRINT lc(1) + n, SIN(n) * lc(1), lc(n - 4) * 3
    t = n: PRINT t * INT(n / 2)
END SUB
