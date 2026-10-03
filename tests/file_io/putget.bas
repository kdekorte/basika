10 REM PUT/GET fixed-length record IO
20 OPEN "tests/putget.tmp" FOR RANDOM AS #1
30 PUT #1, 1, 3, "abc"
40 PUT #1, 2, 3, "def"
50 GET #1, 1, 3, A$
60 GET #1, 2, 3, B$
70 PRINT A$
80 PRINT B$
90 CLOSE #1
100 END
