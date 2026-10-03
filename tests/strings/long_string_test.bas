10 source$ = STRING$(200, "A") + STRING$(200, "B")
20 packed$ = _DEFLATE$(source$)
30 restored$ = _INFLATE$(packed$)
40 PRINT "LONG STRING: "; LEN(source$) = 400
50 IF restored$ = source$ THEN PRINT "LONG ROUND TRIP: -1"
60 IF LEN(packed$) < LEN(source$) THEN PRINT "COMPRESSION: -1"
70 END