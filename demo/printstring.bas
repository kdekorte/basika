10 width = 640
20 height = 480
30 SCREEN _NEWIMAGE(width, height, 256)
40 COLOR 15
50 _PRINTSTRING (20, 20), "=== _LOADFONT & _FONT Demo (3 Fonts) ==="
60 fontHandle1% = _LOADFONT("fonts/ModernDOS8x16.ttf", 24)
70 IF fontHandle1% = 0 THEN PRINT "Failed to load Font 1: Modern DOS": GOTO 200
80 _FONT fontHandle1%
90 COLOR 14
100 _PRINTSTRING (30, 80), "Font 1: Modern DOS (Size 24)"
110 fontHandle2% = _LOADFONT("/System/Library/Fonts/Monaco.ttf", 20)
130 IF fontHandle2% = 0 THEN PRINT "Failed to load Font 2: Monaco System Monospace": GOTO 200
140 _FONT fontHandle2%
150 COLOR 11
160 _PRINTSTRING (30, 150), "Font 2: Monaco System Monospace (Size 20)"
170 fontHandle3% = _LOADFONT("/System/Library/Fonts/Geneva.ttf", 28)
180 IF fontHandle3% = 0 THEN PRINT "Failed to load Font 3: Geneva Sans-Serif": GOTO 200
190 _FONT fontHandle3%
195 COLOR 10: _PRINTSTRING (30, 220), "Font 3: Geneva Sans-Serif (Size 28)"
200 _FONT 0
210 COLOR 7
220 _PRINTSTRING (30, 320), "Font 0: Default Retro Font (Reset)"
230 IF fontHandle1% > 0 THEN _FREEFONT fontHandle1%
240 IF fontHandle2% > 0 THEN _FREEFONT fontHandle2%
250 IF fontHandle3% > 0 THEN _FREEFONT fontHandle3%
260 SCREENSHOT "tests/printstring.png"
270 END
