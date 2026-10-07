10 SCREEN 12
20 CLS
30 REM Happy emoji demo using CIRCLE and PAINT commands
40 CX = 320 : CY = 240 : R = 150
50 REM Draw the face outline and fill it with PAINT
60 CIRCLE (CX, CY), R, 3
70 PAINT (CX, CY), 3, 3
80 REM Draw left eye as a filled circle
90 CIRCLE (CX - 50, CY - 60), 12, 1
95 PAINT (CX - 50, CY - 60), 1, 1
100 REM Draw right eye as a filled circle
110 CIRCLE (CX + 50, CY - 60), 12, 1
115 PAINT (CX + 50, CY - 60), 1, 1
120 REM Draw the smile as a thick arc across the bottom of a circle
130 PI = 3.14159
140 FOR T = 0 TO 4
150   CIRCLE (CX, CY + 5), 80 + T, 1, PI * 1.15, PI * 1.85
160 NEXT T
170 PRINT "HAPPY EMOJI"

