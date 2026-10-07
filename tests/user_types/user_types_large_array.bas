' Large arrays of records fit in the variable table (one entry per field).
TYPE Particle
    x AS SINGLE
    y AS SINGLE
    vx AS SINGLE
    vy AS SINGLE
    tint AS _UNSIGNED LONG
END TYPE
DIM SHARED P(1 TO 1000) AS Particle
FOR i = 1 TO 1000: P(i).x = i: P(i).tint = &HFF000000 + i: NEXT
PRINT P(1000).x; HEX$(P(1000).tint); SumX

FUNCTION SumX
    FOR i = 1 TO 1000: s = s + P(i).x: NEXT
    SumX = s
END FUNCTION
