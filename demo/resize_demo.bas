' ============================================================================
'  RESIZE DEMO - a window the user can resize, redrawn to fit its new size
' ----------------------------------------------------------------------------
'  $RESIZE:ON makes the window resizable. Each time the user resizes it,
'  _RESIZE returns -1 once, and the program builds a new screen image the
'  size of the window (_RESIZEWIDTH x _RESIZEHEIGHT) and redraws.
'
'  Try $RESIZE:STRETCH or $RESIZE:SMOOTH instead: the window still resizes,
'  but the 640x480 picture is scaled to fit rather than redrawn.
'
'  Run with: basika -w demo/resize_demo.bas      (Esc quits)
' ============================================================================
$RESIZE:ON

SCREEN _NEWIMAGE(640, 480, 32)
_PRINTMODE _KEEPBACKGROUND
_TITLE "Resize me"
_AUTODISPLAY OFF
GOSUB Redraw

DO
    IF _RESIZE THEN
        SCREEN _NEWIMAGE(_RESIZEWIDTH, _RESIZEHEIGHT, 32)
        _PRINTMODE _KEEPBACKGROUND ' each new screen starts with _FILLBACKGROUND
        GOSUB Redraw
    END IF
    _LIMIT 30
LOOP UNTIL _KEYHIT = 27
END

Redraw:
    W = _WIDTH: H = _HEIGHT
    CLS
    ' A grid every 50 pixels and a border, so the new size is easy to see
    FOR X = 0 TO W - 1 STEP 50
        LINE (X, 0)-(X, H - 1), _RGB32(40, 40, 80)
    NEXT
    FOR Y = 0 TO H - 1 STEP 50
        LINE (0, Y)-(W - 1, Y), _RGB32(40, 40, 80)
    NEXT
    LINE (0, 0)-(W - 1, H - 1), _RGB32(255, 200, 0), B
    LINE (0, 0)-(W - 1, H - 1), _RGB32(80, 80, 120)
    LINE (W - 1, 0)-(0, H - 1), _RGB32(80, 80, 120)
    R = W: IF H < R THEN R = H
    CIRCLE (W \ 2, H \ 2), R \ 3, _RGB32(0, 220, 255)
    T$ = "Window size:" + STR$(W) + " x" + STR$(H)
    _PRINTSTRING ((W - _PRINTWIDTH(T$)) \ 2, H \ 2 - 8), T$
    _DISPLAY
RETURN
