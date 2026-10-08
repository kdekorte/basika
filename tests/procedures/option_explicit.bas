' OPTION _EXPLICIT: a program that declares everything it uses runs.
OPTION _EXPLICIT
CONST LIMIT = 3
DIM SHARED total AS LONG
DIM i, names$(2)
TYPE Pt
    x AS INTEGER
END TYPE
DIM p AS Pt
p.x = 4
FOR i = 1 TO LIMIT: total = total + i: NEXT
names$(1) = "a"
IF total > 2 THEN GOSUB Report ELSE PRINT "small"
AddTo 5
PRINT Twice(total); p.x; LEN(names$(1))
END
Report: PRINT "total"; total: RETURN
SUB AddTo (n)
    DIM k
    k = n
    total = total + k
END SUB
FUNCTION Twice (v)
    STATIC calls
    calls = calls + 1
    Twice = v * 2
END FUNCTION
