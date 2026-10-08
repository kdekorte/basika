' EXIT DO leaves loops ending in LOOP UNTIL / LOOP WHILE, from a block IF and
' inside procedures; a DO WHILE that is false at the start skips its loop.
DO
    n = n + 1
    IF n = 2 THEN EXIT DO
LOOP UNTIL n > 10: PRINT "same line after LOOP"
PRINT "n ="; n
m = 0
IF 1 THEN
    DO
        m = m + 1
        IF m = 2 THEN
            EXIT DO
        END IF
    LOOP WHILE m < 9
    PRINT "m ="; m
ELSE
    PRINT "wrong branch"
END IF
DO WHILE 0
    PRINT "never"
LOOP UNTIL 1
PRINT "skipped DO WHILE 0"
T
SUB T
    DO
        k = k + 1
        IF k = 4 THEN EXIT DO
    LOOP UNTIL k > 9
    PRINT "sub k ="; k
END SUB
