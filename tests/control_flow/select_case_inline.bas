' Statements may follow CASE and CASE ELSE on the same line.
FOR i = 0 TO 2
    SELECT CASE i
        CASE 0: c = 10: PRINT "zero";
        CASE ELSE: c = 30: PRINT "other";
    END SELECT
    PRINT c
NEXT
