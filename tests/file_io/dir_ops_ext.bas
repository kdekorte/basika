10 ON ERROR GOTO 500
20 D$ = "test_dir_ext"
30 PRINT "Testing MKDIR with variable"
40 MKDIR D$
50 PRINT "Testing MKDIR existing"
60 MKDIR D$
70 PRINT "Testing CHDIR"
80 CHDIR D$
90 OPEN "temp.txt" FOR OUTPUT AS #1
100 PRINT #1, "hello"
110 CLOSE #1
120 CHDIR ".."
130 PRINT "Testing RMDIR non-empty"
140 RMDIR D$
150 PRINT "Cleaning up file"
160 KILL D$ + "/temp.txt"
170 PRINT "Testing RMDIR empty"
180 RMDIR D$
190 PRINT "Success"
200 END
500 PRINT "Error trapped"
510 RESUME NEXT