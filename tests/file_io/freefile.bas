' FREEFILE gives the lowest file number not in use (1-15), and "Too many
' files" when all are open.
OPTION _EXPLICIT
DIM f AS INTEGER, g AS INTEGER, i AS INTEGER
f = FREEFILE
PRINT "first"; f
OPEN "tests/freefile1.tmp" FOR OUTPUT AS #f
g = FREEFILE
PRINT "next"; g
OPEN "tests/freefile2.tmp" FOR OUTPUT AS #g
CLOSE #f
PRINT "reused"; FREEFILE; FREEFILE + 1
CLOSE #g
FOR i = 1 TO 15
    OPEN "tests/freefile1.tmp" FOR INPUT AS #i
NEXT
ON ERROR GOTO full
PRINT FREEFILE
CLOSE
PRINT "after CLOSE"; FREEFILE
END
full:
PRINT "error"; ERR
RESUME NEXT
