10 REM QBasic PUT/GET with numeric variables in BINARY files:
20 REM each variable records exactly its type's bytes at a byte position.
30 F$ = "tests/putget_num.tmp"
40 OPEN F$ FOR BINARY AS #1
50 I% = -1234: L& = 123456789: S! = 1.5: D# = 0.3#: U = 42
60 B%% = -100: UB~%% = 200
70 A$ = "Hello!"
80 PUT #1, 1, I%
90 PUT #1, , L&
100 PUT #1, , S!
110 PUT #1, , D#
120 PUT #1, , U
130 PUT #1, , B%%
140 PUT #1, , UB~%%
150 PUT #1, , A$
160 PRINT "LOF:"; LOF(1); "  LOC:"; LOC(1)
170 J% = 0: M& = 0: T! = 0: E# = 0: V = 0: BB%% = 0: UBB~%% = 0
180 C$ = SPACE$(6)
190 GET #1, 1, J%
200 GET #1, , M&
210 GET #1, , T!
220 GET #1, , E#
230 GET #1, , V
240 GET #1, , BB%%
250 GET #1, , UBB~%%
260 GET #1, , C$
270 PRINT "INTEGER:"; J%
280 PRINT "LONG:"; M&
290 PRINT "SINGLE:"; T!
300 PRINT "DOUBLE:"; E#
310 PRINT "UNTYPED:"; V
320 PRINT "BYTE:"; BB%%
330 PRINT "UBYTE:"; UBB~%%
340 PRINT "STRING:"; C$
350 GET #1, 1000, Z%
360 PRINT "PAST EOF:"; Z%
370 CLOSE #1
380 KILL F$
390 END
