' Compiled expressions next to record fields, DIM AS STRING variables and
' errors: names that are not plain numbers are left to the parser.
TYPE P
    x AS INTEGER
    y AS SINGLE
END TYPE
DIM pts(3) AS P, one AS P
pts(1).x = 5: pts(1).y = 2.5: one.x = 9
PRINT pts(1).x * 2 + pts(1).y, one.x + 1, INT(pts(1).y) * one.x
DIM s AS STRING
s = "abc"
IF s = "abc" AND LEN(s) = 3 THEN PRINT "dimstr ok"
DIM w(3) AS STRING
w(1) = "z"
IF w(1) = "z" THEN PRINT "strarr ok"
DIM h AS DOUBLE
h = 1 / 3
PRINT h, h * 2 + SIN(0), INT(h * 3)
ON ERROR GOTO handler
PRINT SQR(-1) + 1
PRINT LOG(0) * 2
PRINT 1 / 0 + 2
END
handler:
PRINT "err"; ERR
RESUME NEXT
