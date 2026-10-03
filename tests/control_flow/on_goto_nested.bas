10 A = 1
20 B = 1
30 C = 3
40 ON A GOTO 100, 200, 300
50 PRINT "Should not reach here"
100 PRINT "Jumped to 100 (A=1)"
110 ON B GOTO 150, 160
120 PRINT "Should not reach here after ON B"
150 PRINT "Jumped to 150 (B=2, first target)"
160 PRINT "This is after 150"
170 GOTO 400
200 PRINT "Jumped to 200 (A=2)"
210 GOTO 400
300 PRINT "Jumped to 300 (A=3)"
310 GOTO 400
400 PRINT "Finished ON GOTO tests"
410 END