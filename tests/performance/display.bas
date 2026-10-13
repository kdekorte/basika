' Frame benchmark: filled boxes and circles shown with _DISPLAY, without
' _AUTODISPLAY OFF, as QB64 programs are written. In a window, drawing
' between _DISPLAYs must not be presented too.
SCREEN _NEWIMAGE(800, 600, 32)
FOR frame = 1 TO 60
    CLS
    FOR i = 0 TO 199
        x = (i * 37 + frame * 3) MOD 760: y = (i * 53) MOD 560
        IF i MOD 2 THEN c~& = _RGBA32(255, i, 80, 120) ELSE c~& = _RGB32(i, 200, 255)
        LINE (x, y)-(x + 40, y + 40), c~&, BF
        CIRCLE (x + 20, y + 20), 18, _RGB32(255, 255, 255)
    NEXT
    _DISPLAY
NEXT
