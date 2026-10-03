10 PRINT "Testing MKI$ and CVI"
20 I$ = MKI$(16961)
30 PRINT "I$ len:"; LEN(I$)
40 PRINT "CVI(I$):"; CVI(I$)
50 PRINT "Byte 1:"; ASC(LEFT$(I$, 1))
60 PRINT "Byte 2:"; ASC(RIGHT$(I$, 1))
70 PRINT "CVI with CHR$:"; CVI(CHR$(2) + CHR$(255))
80 PRINT "MKI$(-254) Byte 1:"; ASC(LEFT$(MKI$(-254), 1))
90 PRINT "MKI$(-254) Byte 2:"; ASC(RIGHT$(MKI$(-254), 1))
100 PRINT "Testing MKS$ and CVS"
110 S$ = MKS$(-123.45)
120 PRINT "S$ len:"; LEN(S$)
130 PRINT "CVS(S$):"; CVS(S$)
140 PRINT "S$ Byte 1:"; ASC(MID$(S$, 1, 1))
150 PRINT "S$ Byte 2:"; ASC(MID$(S$, 2, 1))
160 PRINT "S$ Byte 3:"; ASC(MID$(S$, 3, 1))
170 PRINT "S$ Byte 4:"; ASC(MID$(S$, 4, 1))
180 PRINT "Testing MKD$ and CVD"
190 D$ = MKD$(0.3#)
200 PRINT "D$ len:"; LEN(D$)
210 PRINT "CVD(D$):"; CVD(D$)
220 PRINT "D$ Byte 1:"; ASC(MID$(D$, 1, 1))
230 PRINT "D$ Byte 2:"; ASC(MID$(D$, 2, 1))
240 PRINT "D$ Byte 3:"; ASC(MID$(D$, 3, 1))
250 PRINT "D$ Byte 4:"; ASC(MID$(D$, 4, 1))
260 PRINT "D$ Byte 5:"; ASC(MID$(D$, 5, 1))
270 PRINT "D$ Byte 6:"; ASC(MID$(D$, 6, 1))
280 PRINT "D$ Byte 7:"; ASC(MID$(D$, 7, 1))
290 PRINT "D$ Byte 8:"; ASC(MID$(D$, 8, 1))
300 PRINT "Testing with RANDOM file and FIELD"
310 OPEN "tests/mki_mks_mkd.tmp" FOR RANDOM AS #1
320 FIELD #1, 2 AS FI$, 4 AS FS$, 8 AS FD$
330 LSET FI$ = MKI$(16961)
340 LSET FS$ = MKS$(-123.45)
350 LSET FD$ = MKD$(0.3#)
360 PUT #1, 1
370 LSET FI$ = ""
380 LSET FS$ = ""
390 LSET FD$ = ""
400 GET #1, 1
410 PRINT "CVI(FI$):"; CVI(FI$)
420 PRINT "CVS(FS$):"; CVS(FS$)
430 PRINT "CVD(FD$):"; CVD(FD$)
440 CLOSE #1
450 KILL "tests/mki_mks_mkd.tmp"
460 END
