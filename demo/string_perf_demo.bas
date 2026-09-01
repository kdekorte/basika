10 ITERATIONS = 50000
20 START = TIMER
30 S$ = ""
40 FOR I = 1 TO ITERATIONS
50   S$ = S$ + "A"
60   T$ = LEFT$(S$, 64)
70   U$ = UCASE$(T$)
80   V$ = LCASE$(U$)
90   W$ = LEFT$(V$, 32) + RIGHT$(V$, 32)
100 NEXT I
110 FINISH = TIMER
120 PRINT "50000 string operations completed in "; (FINISH - START); " seconds"
130 PRINT "Final string length: "; LEN(S$)
140 PRINT "First char: "; ASC(LEFT$(S$, 1)); " Last char: "; ASC(RIGHT$(S$, 1))
150 PRINT "Checksum: "; LEN(W$)
