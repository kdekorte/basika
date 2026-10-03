10 REM Test graphics primitives, pixel buffers via GET, and screenshot
20 SCREEN 2
30 PSET (5, 5), 3
40 LINE (0, 0)-(10, 0), 2
50 CIRCLE (20, 20), 5, 1
60 DIM BUFFER(200)
70 GET (0, 0)-(10, 10), BUFFER
80 PRINT BUFFER(0); BUFFER(1)
90 PRINT BUFFER(2); BUFFER(7); BUFFER(12)
100 PRINT BUFFER(62)
110 PUT (40, 40), BUFFER, PSET
120 SCREENSHOT "tests/graphics_primitives.png"
130 END
