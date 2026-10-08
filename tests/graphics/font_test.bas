10 SCREEN 2
20 fontHandle% = _LOADFONT("fonts/ModernDOS8x16.ttf", 24)
30 IF fontHandle% < 0 THEN PRINT "Failed to load font": END
40 _FONT fontHandle%
50 _PRINTSTRING (10, 10), "Hello Custom Font!"
60 _FONT 0
70 _PRINTSTRING (10, 50), "Hello Default Font!"
80 _FREEFONT fontHandle%
90 END
