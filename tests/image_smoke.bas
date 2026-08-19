10 h = _LOADIMAGE("test_fonts.png", 32)
20 IF h = 0 THEN PRINT "LOADIMAGE FAILED"
30 _PUTIMAGE (0, 0), h
40 SCREENSHOT "tests/image_smoke.png"
50 PRINT "IMAGE OK"