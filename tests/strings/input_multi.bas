10 OPEN "tests/input_multi.tmp" FOR OUTPUT AS #1
20 PRINT #1, "123, 456, Hello"
30 CLOSE #1
40 OPEN "tests/input_multi.tmp" FOR INPUT AS #1
50 INPUT #1, A, B, C$
60 PRINT A
70 PRINT B
80 PRINT C$
90 CLOSE #1