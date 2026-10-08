' A QB64 function named alone as a SUB argument is called and its value
' passed, not taken as a by-reference variable of that name.
Show _PI, _PI(2)
CALL Show(_PI, _RESIZEWIDTH)
Show _RGB32(1, 2, 3), _MOUSEX
SUB Show (a AS DOUBLE, b AS DOUBLE)
    PRINT a; b
END SUB
