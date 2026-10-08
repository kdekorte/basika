10 REM A line number after THEN, ELSE or GOTO (IF x GOTO n) jumps to it.
20 P = 0
30 IF P THEN PRINT "t" ELSE 50
40 PRINT "no"
50 PRINT "else jumped"
60 IF 1 GOTO 80
70 PRINT "no"
80 IF 0 THEN 100 ELSE IF 1 THEN 110
90 PRINT "no"
100 PRINT "no"
110 PRINT "nested ok": X = 5: IF X THEN 130
120 PRINT "no"
130 PRINT "done"
