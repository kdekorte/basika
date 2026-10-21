10 REM QBasic PUT/GET with variables in RANDOM files: records are LEN
20 REM bytes, zero-filled past the variable, and larger data errors with
30 REM Bad record length (59). A whole array is Illegal function call (5).
40 ON ERROR GOTO Handler
50 F$ = "tests/putget_rnd.tmp"
60 OPEN F$ FOR RANDOM AS #1 LEN = 5
70 X& = 42
80 PUT #1, 1, X&
90 PRINT "LOF:"; LOF(1); "  LOC:"; LOC(1)
100 Y! = 2.5
110 PUT #1, 2, Y!
120 PRINT "LOF:"; LOF(1); "  LOC:"; LOC(1)
130 GET #1, 1, Z&
140 GET #1, 2, W!
150 PRINT "Z:"; Z&; " W:"; W!
160 D# = 1.1
170 PUT #1, 3, D#
180 PRINT "AFTER ERR:"; ERR
190 CLOSE #1
200 KILL F$
210 DEFINT A-N
220 N = 300
230 OPEN F$ FOR BINARY AS #2
240 PUT #2, 1, N
250 PRINT "DEFINT LOF:"; LOF(2)
260 GET #2, 1, N2
270 PRINT "DEFINT N2:"; N2
280 DIM WA%(5)
290 WA%(2) = 9
300 PUT #2, 1, WA%
310 CLOSE #2
320 KILL F$
330 END
340 Handler:
350 PRINT "ERR:"; ERR
360 RESUME NEXT
