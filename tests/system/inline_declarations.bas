' DIM x AS type = value and FOR i AS type = start TO end, as in FreeBASIC and QB64
OPTION _EXPLICIT
DIM n AS LONG = 1000000
DIM big AS DOUBLE = 2 ^ 40
DIM s AS STRING = "hel" + "lo"
DIM a AS LONG = 3, b AS LONG = 4, c AS LONG
DIM w AS _UNSIGNED _BYTE = 200
DIM fixed AS STRING * 3 = "abcdef"
PRINT n; big; s; a + b; c; w; fixed

FOR i AS LONG = 1 TO 3
    PRINT i;
NEXT i
PRINT

FOR j AS INTEGER = 10 TO 1 STEP -4: PRINT j;: NEXT j
PRINT

FOR k AS LONG = 5 TO 1: PRINT "never": NEXT k

' The counter keeps its declared type after the loop.
FOR m AS LONG = 1 TO 2: NEXT m
m = 100000
PRINT m

' A SUB gets its own counter each time it runs.
SUB Count
    FOR t AS LONG = 1 TO 2: PRINT t;: NEXT t
    PRINT
END SUB
Count
Count
