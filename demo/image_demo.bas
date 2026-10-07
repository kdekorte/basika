10 SCREEN 12
20 CLS
30 COLOR 15
40 PRINT "_LOADIMAGE / _PUTIMAGE / _FREEIMAGE demo"
50 image% = _LOADIMAGE("tests/image_fixture.bmp", 32)
60 IF image% = -1 THEN PRINT "Unable to load image": END
70 PRINT "Full image"
80 _PUTIMAGE (40, 55), image%
90 PRINT "Scaled image"
100 _PUTIMAGE (280, 45)-(340, 80), image%
110 PRINT "Cropped source"
120 _PUTIMAGE (400, 55)-(560, 175), image%, , (0, 0)-(79, 119)
130 _FREEIMAGE image%
140 END