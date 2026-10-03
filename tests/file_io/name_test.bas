10 OPEN "test_old.tmp" FOR OUTPUT AS #1
20 PRINT #1, "Hello World"
30 CLOSE #1
40 NAME "test_old.tmp" AS "test_new.tmp"
50 OPEN "test_new.tmp" FOR INPUT AS #1
60 INPUT #1, A$
70 PRINT A$
80 CLOSE #1
90 KILL "test_new.tmp"
100 PRINT "Rename successful"
