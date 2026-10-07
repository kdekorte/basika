' LOCK and UNLOCK take a whole file, one record or a TO range.
OPEN "tests/lock_unlock.tmp" FOR RANDOM AS #1 LEN = 16
LOCK #1
UNLOCK #1
LOCK #1, 2
UNLOCK #1, 2
LOCK #1, 3 TO 5
UNLOCK #1, 3 TO 5
LOCK #1, TO 4
UNLOCK #1, TO 4
PRINT "locks ok; ERDEV ="; ERDEV; "["; ERDEV$; "]"
ON ERROR GOTO Trap
LOCK #1, 0
LOCK #1, 5 TO 2
LOCK #9
CLOSE #1
KILL "tests/lock_unlock.tmp"
END
Trap:
PRINT "error"; ERR
RESUME NEXT
