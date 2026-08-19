PRINT "_DEFLATE$ / _INFLATE$ Demo"
source$ = "BASIKA compresses and restores text with zlib."
source$ = source$ + " Compression works well on repeated words and patterns. "
source$ = source$ + "BASIKA compresses and restores text with zlib. "
source$ = source$ + "Compression works well on repeated words and patterns."
packed$ = _DEFLATE$(source$)
restored$ = _INFLATE$(packed$)
PRINT "Original:  "; LEN(source$); " characters"
PRINT "Packed:    "; LEN(packed$); " characters"
PRINT "Restored:  "; restored$
IF restored$ = source$ THEN PRINT "Round trip: PASS"
IF LEN(_INFLATE$("not compressed data")) = 0 THEN PRINT "Invalid input: PASS"
END