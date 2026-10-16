10 REM An operator with nothing after it is a syntax error (it crashed
20 REM basika), whether the statement ends there or at a colon.
30 ON ERROR GOTO 900
40 PRINT 1 +
50 PRINT 1 >
60 PRINT 1 AND
70 X = 2 *
80 PRINT (1 +
90 PRINT 1 +: PRINT "RESUMED AFTER THE COLON"
100 OPEN "tests/missing_operand.tmp" FOR APPEND AS #1 >
110 PRINT 1 + 2; 5 > 3
120 END
900 PRINT "Error"; ERR; "in"; ERL
910 RESUME NEXT
