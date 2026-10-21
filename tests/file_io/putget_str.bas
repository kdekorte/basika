10 REM QBasic PUT/GET with string variables: a variable-length string moves
20 REM its current bytes, a fixed-length one its declared size.
30 F$ = "tests/putget_str.tmp"
40 OPEN F$ FOR BINARY AS #1
50 A$ = "AB"
60 PUT #1, 1, A$
70 B$ = SPACE$(2)
80 GET #1, 1, B$
90 PRINT "VAR STRING:"; B$
100 DIM FS AS STRING * 4
110 FS = "XY"
120 PUT #1, 3, FS
130 PRINT "LOF:"; LOF(1)
140 DIM GS AS STRING * 4
150 GET #1, 3, GS
160 PRINT "FIXED:"; GS; "#"; LEN(GS)
170 DIM AR$(2)
180 AR$(1) = "one"
190 PUT #1, 8, AR$(1)
200 PRINT "LOF2:"; LOF(1)
210 EL$ = SPACE$(3)
220 GET #1, 8, EL$
230 PRINT "ELEMENT:"; EL$
240 CLOSE #1
250 KILL F$
260 OPEN F$ FOR RANDOM AS #1 LEN = 10
270 C$ = "AB"
280 PUT #1, 1, C$
290 PRINT "RANDOM LOF:"; LOF(1); "  LOC:"; LOC(1)
300 D$ = SPACE$(10)
310 GET #1, 1, D$
320 PRINT "RANDOM LEN:"; LEN(D$)
330 PRINT "FILLER:"; ASC(MID$(D$, 3, 1))
340 CLOSE #1
350 KILL F$
360 END
