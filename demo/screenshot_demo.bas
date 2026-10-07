10 SCREEN 12: CLS
20 COLOR 11: PRINT "Generating graphics for screenshot..."
30 ' Draw some shapes
40 LINE (50,50)-(150,150), 1, BF
50 CIRCLE (320,240), 80, 13
55 PAINT (320,240), 13, 13
60 LINE (400,300)-(600,450), 2, B
70 ' Save the screenshot
80 PRINT "Saving to screenshot_demo.png..."
90 SCREENSHOT "screenshot_demo.png"
100 PRINT "Done!"
110 END