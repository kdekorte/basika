' "Name:" is a label only at the start of a line. After THEN, or after
' another statement and a colon, it calls SUB Name.
DIM SHARED W AS INTEGER
W = 1: Grow: PRINT "mid-line"; W
W = 1: IF W THEN Grow: PRINT "after THEN"; W
n = 0
Again: n = n + 1: IF n < 3 THEN GOTO Again
PRINT "label"; n
SUB Grow
    W = W + 9
END SUB
