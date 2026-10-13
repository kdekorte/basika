' PAINT benchmark: per frame, outline 24 circles and fill each one, solid,
' translucent or tiled, half of them inside a VIEW.
SCREEN _NEWIMAGE(800, 600, 32)
DIM edge AS _UNSIGNED LONG
edge = _RGB32(255, 255, 255)
tile$ = STRING$(4, &HF0) + STRING$(4, &H0F)
FOR frame = 1 TO 90
    CLS
    FOR i = 0 TO 23
        cx = 60 + (i MOD 8) * 95: cy = 80 + (i \ 8) * 180
        CIRCLE (cx, cy), 40, edge
        IF i MOD 2 = 0 THEN
            PAINT (cx, cy), _RGB32((i * 37 + frame) MOD 256, 100, 200), edge
        ELSE
            VIEW SCREEN (cx - 45, cy - 45)-(cx + 45, cy + 45)
            IF i MOD 3 = 0 THEN
                COLOR _RGB32(200, 50, 50), _RGB32(0, 0, 0)
                PAINT (cx, cy), tile$, edge
            ELSE
                PAINT (cx, cy), _RGBA32(90, 255, 140, 120), edge
            END IF
            VIEW
        END IF
    NEXT
    _DISPLAY
NEXT
