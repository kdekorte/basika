10 REM KILL with a wildcard deletes every match and prints nothing.
20 OPEN "tests/kill_tmp1.tmp" FOR OUTPUT AS #1
30 PRINT #1 "x"
40 CLOSE #1
50 OPEN "tests/kill_tmp2.tmp" FOR OUTPUT AS #1
60 PRINT #1 "y"
70 CLOSE #1
80 KILL "tests/kill_tmp*.tmp"
90 ON ERROR GOTO 200
100 F$ = "tests/kill_tmp1.tmp": GOSUB 150
110 F$ = "tests/kill_tmp2.tmp": GOSUB 150
120 END
150 FOUND = -1
160 OPEN F$ FOR INPUT AS #1
170 IF FOUND THEN PRINT F$; " still exists": CLOSE #1 ELSE PRINT F$; " deleted"
180 RETURN
200 IF ERR = 53 THEN FOUND = 0: RESUME NEXT
210 PRINT "Error"; ERR: END
