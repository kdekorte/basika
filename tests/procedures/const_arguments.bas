' CONST names passed to procedures are values, not by-reference variables.
CONST WIDTH_PX = 320
CONST SCALE = 1.5
ShowArgs WIDTH_PX, SCALE
CALL ShowArgs(WIDTH_PX, SCALE)
PRINT Area(WIDTH_PX, SCALE)

SUB ShowArgs (w AS INTEGER, s AS SINGLE)
    PRINT w; s
    w = 0
END SUB

FUNCTION Area (w, s)
    Area = w * s
END FUNCTION
