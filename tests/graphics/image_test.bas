10 SCREEN 2
20 image% = _LOADIMAGE("tests/image_fixture.bmp", 32)
30 IF image% < -1 THEN PRINT "LOADIMAGE PASS"
40 _PUTIMAGE (10, 10), image%
50 PRINT "PUTIMAGE PASS"
60 SCREENSHOT "tests/image_test.png"
70 _FREEIMAGE image%
80 PRINT "FREEIMAGE PASS"
90 image2% = _LOADIMAGE("tests/image_fixture.bmp", 32)
100 IF image2% = image% THEN PRINT "HANDLE REUSE PASS"
110 _FREEIMAGE image2%
120 PRINT "IMAGE TEST PASS"
130 END