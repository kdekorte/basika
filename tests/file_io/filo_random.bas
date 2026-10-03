10 REM File I/O random access via SEEK
20 OPEN "tests/filo_test.tmp" FOR OUTPUT AS #1
30 PRINT #1 "one"
40 PRINT #1 "two"
50 PRINT #1 "three"
60 CLOSE #1
70 OPEN "tests/filo_test.tmp" FOR INPUT AS #1
80 SEEK #1, 4
90 INPUT #1 A$
100 PRINT A$
110 CLOSE #1
120 END
