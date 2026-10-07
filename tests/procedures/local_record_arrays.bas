' Record arrays DIMmed in a procedure are local: they may reuse a module
' array's name, take different bounds on each call, and hold many elements.
TYPE Rec
    v AS INTEGER
    w AS SINGLE
END TYPE
DIM r(1 TO 2) AS Rec
r(1).v = 77
Big 100
Big 300
PRINT "module r(1).v ="; r(1).v
total = 5
PRINT "shared in function:"; Twice

SUB Big (n)
    DIM r(1 TO n) AS Rec
    PRINT "big starts"; r(1).v; r(n).v;
    FOR i = 1 TO n: r(i).v = i: r(i).w = i / 2: NEXT
    PRINT " sum"; r(1).v + r(n).v; r(n).w
END SUB

FUNCTION Twice
    SHARED total
    Twice = total * 2
END FUNCTION
