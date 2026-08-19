10 source$ = "Basika compression round trip"
20 packed$ = _DEFLATE$(source$)
30 restored$ = _INFLATE$(packed$)
40 IF LEN(packed$) > 0 THEN PRINT "COMPRESSED: -1"
50 IF restored$ = source$ THEN PRINT "ROUND TRIP: -1"
60 IF LEN(_INFLATE$("invalid")) = 0 THEN PRINT "INVALID: -1"
70 END