10 REM DELETE file test
20 OPEN "tests/delete_test.tmp" FOR OUTPUT AS #1
30 PRINT #1 "x"
40 CLOSE #1
50 DELETE "tests/delete_test.tmp"
60 END
