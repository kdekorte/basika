10 REM Variable suffix tests: %, !, #
20 I% = 10.5: PRINT "I% (10.5):"; I%
30 I% = 10.4: PRINT "I% (10.4):"; I%
40 F! = 1.2345678: PRINT "F!:"; F!
50 D# = 1.234567890123456#: PRINT "D#:"; D#
60 A = 1 : A% = 2 : A# = 3
70 PRINT "A (default):"; A
80 PRINT "A%:"; A%
90 PRINT "A#:"; A#
100 A! = 10 : PRINT "A after A! assignment:"; A
110 ON ERROR GOTO 140
120 SWAP I%, F!
130 PRINT "Error: SWAP allowed mismatch" : END
140 PRINT "Caught expected SWAP type mismatch"
150 RESUME 160
160 END