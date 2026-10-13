' EOF(n) is -1 (true) at the end of a file and 0 before it, as in QBasic,
' so DO WHILE NOT EOF(n) stops after the last line.
OPEN "tests/eof_loop.tmp" FOR OUTPUT AS #1
PRINT #1, "first": PRINT #1, "second"
CLOSE #1
OPEN "tests/eof_loop.tmp" FOR INPUT AS #1
PRINT "start"; EOF(1)
DO WHILE NOT EOF(1)
    LINE INPUT #1, a$
    PRINT a$
LOOP
PRINT "end"; EOF(1)
CLOSE #1
OPEN "tests/eof_loop.tmp" FOR OUTPUT AS #1: CLOSE #1
OPEN "tests/eof_loop.tmp" FOR INPUT AS #1
PRINT "empty"; EOF(1); NOT EOF(1)
CLOSE #1
KILL "tests/eof_loop.tmp"
