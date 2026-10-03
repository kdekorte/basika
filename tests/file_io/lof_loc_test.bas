10 OPEN "lof_test.tmp" FOR OUTPUT AS #1
20 PRINT #1, "Hello World"
30 CLOSE #1
40 OPEN "lof_test.tmp" FOR INPUT AS #1
50 L = LOF(1)
60 IF L = 12 THEN PRINT "LOF PASS" ELSE PRINT "LOF FAIL: "; L
70 REM 12 bytes because of trailing newline (usually added by PRINT #n unless semicolon used)
80 GET #1, 1, 5, A$
90 P = LOC(1)
100 IF P = 5 THEN PRINT "LOC PASS" ELSE PRINT "LOC FAIL: "; P
110 CLOSE #1
120 KILL "lof_test.tmp"
130 END
