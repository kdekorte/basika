10 source$ = "Basika compression round trip"
20 packed$ = _DEFLATE$(source$)
30 restored$ = _INFLATE$(packed$)
40 IF LEN(packed$) > 0 THEN PRINT "COMPRESSED: -1"
50 IF restored$ = source$ THEN PRINT "ROUND TRIP: -1"
60 IF LEN(_INFLATE$("invalid")) = 0 THEN PRINT "INVALID: -1"
70 large$ = STRING$(1048576, "Z")
80 packed_large$ = _DEFLATE$(large$)
90 restored_large$ = _INFLATE$(packed_large$)
100 IF LEN(restored_large$) = 1048576 THEN PRINT "LARGE LENGTH: -1"
110 IF LEFT$(restored_large$, 1) = "Z" THEN PRINT "LARGE FIRST: -1"
120 IF RIGHT$(restored_large$, 1) = "Z" THEN PRINT "LARGE LAST: -1"
130 END