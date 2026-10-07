10 SCREEN 12
20 CLS
30 COLOR 15
40 PRINT "DRAW Command Demo"

50 ' Draw a simple house
60 PSET (100, 150), 0
70 DRAW "C14 U50 R60 D50 L60" : ' Yellow walls
80 DRAW "BM+0,-50 C12 E30 F30" : ' Red roof

90 ' Draw a smiley face using relative movement and color changes
100 PSET (300, 100), 0
110 DRAW "C15" : ' White head (outline box for demo)
120 DRAW "R40 D40 L40 U40"

130 ' Eyes
140 DRAW "BM+10,10 C9 R2 BM+16,0 R2"

150 ' Smile
160 DRAW "BM-15,20 C10 D5 R10 U5"

170 ' Scale and rotation test
180 PSET (100, 300), 0
190 FOR A = 0 TO 3
200   DRAW "A" + STR$(A) + " C" + STR$(9+A) + " S" + STR$(8+A*4) + " U10 R10 D10 L10"
210 NEXT A

220 LOCATE 24, 1
230 PRINT "Press any key to exit"
240 SLEEP
250 K$ = INKEY$
260 END
