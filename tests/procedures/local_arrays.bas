' Arrays DIMmed in a procedure are local and created afresh on every call.
FOR k = 1 TO 3
    Work k
NEXT
DIM g(2)
g(1) = 99
Work 4
PRINT "module g(1) still"; g(1)
PRINT Total(5); Total(6); Steps(0); Steps(3)

SUB Work (n)
    DIM g(1 TO 3), names(2) AS STRING
    PRINT "call"; n; "starts at"; g(1);
    g(1) = n * 10
    names(2) = "n" + LTRIM$(STR$(n))
    PRINT g(1); names(2); LEN(names(2))
    ERASE g
END SUB

FUNCTION Total (n)
    DIM v(1 TO n)
    FOR i = 1 TO n: v(i) = i: NEXT
    s = 0
    FOR i = 1 TO n: s = s + v(i): NEXT
    Total = s
END FUNCTION

' Single-line FOR loops in a FUNCTION, including one that runs zero times.
FUNCTION Steps (n)
    FOR i = 1 TO n: s = s + i: NEXT: s = s * 10
    FOR j = 1 TO 2
        s = s + 1
    NEXT j
    Steps = s
END FUNCTION
