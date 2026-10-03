10 OPEN "tests/filo_test.tmp" FOR OUTPUT AS #1
20 PRINT #1 "alpha"
30 PRINT #1 "beta"
40 CLOSE #1
50 OPEN "tests/filo_test.tmp" FOR INPUT AS #1
60 INPUT #1 A$
70 PRINT "LITERAL"
80 PRINT A$
90 CLOSE #1
100 END
