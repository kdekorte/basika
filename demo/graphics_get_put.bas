10 SCREEN 12: CLS
20 COLOR 15: PRINT "Graphics GET/PUT Demo"
30 ' Draw a source pattern
40 LINE (10,30)-(50,70), 2, BF : ' Green box
50 CIRCLE (30,50), 15, 14: PAINT (30,50), 14, 14 : ' Yellow circle inside
60 ' Capture the 41x41 area
70 ' Size needed: 2 + (width * height) = 2 + (41 * 41) = 1683
80 DIM IMG(1682)
90 GET (10,30)-(50,70), IMG
100 ' Draw a background to demonstrate transparency/actions
110 LINE (0,100)-(639,200), 1, BF
120 ' Show different PUT actions
130 LOCATE 6, 1: PRINT "  PSET          PRESET          AND             OR              XOR"
140 PUT (10, 130), IMG, PSET
150 PUT (130, 130), IMG, PRESET
160 PUT (250, 130), IMG, AND
170 PUT (370, 130), IMG, OR
180 PUT (490, 130), IMG, XOR
190 ' Demonstrate XOR animation
200 LOCATE 20, 1: PRINT "Press any key to see XOR animation..."
210 SLEEP: K$ = INKEY$
220 FOR X = 0 TO 590 STEP 10
230   PUT (X, 380), IMG, XOR
240   _DELAY .05
250   PUT (X, 380), IMG, XOR : ' Erase by XORing again
260 NEXT X
270 PUT (300, 380), IMG, PSET
280 PRINT "Demo Complete. Press any key."
290 SLEEP
