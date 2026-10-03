10 REM File I/O block (sequential)
20 OPEN "tests/filo_test.tmp" FOR OUTPUT AS #1
30 PRINT #1 "alpha"
40 PRINT #1 "beta"
50 CLOSE #1
60 OPEN "tests/filo_test.tmp" FOR INPUT AS #1
70 INPUT #1 A$
80 INPUT #1 B$
90 PRINT A$
100 PRINT B$
110 CLOSE #1
120 END
