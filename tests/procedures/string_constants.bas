' String constants (CONST name = "text"), several constants in one CONST, and
' module constants seen inside procedures.
CONST G = "hi", N = 5
CONST H$ = "suffixed"
CONST J = "a" + CHR$(66)
CONST K = 2 * 3
PRINT G; H$; J; K; LEN(G)
Show
SUB Show
    CONST L = "local"
    PRINT G; " "; H$; " "; L; K
END SUB
