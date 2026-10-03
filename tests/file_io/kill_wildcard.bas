10 OPEN "tests/kill_tmp1.tmp" FOR OUTPUT AS #1
20 PRINT #1 "x"
30 CLOSE #1
40 OPEN "tests/kill_tmp2.tmp" FOR OUTPUT AS #1
50 PRINT #1 "y"
60 CLOSE #1
70 KILL "tests/kill_tmp*.tmp"
80 END
