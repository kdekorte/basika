' RESTORE accepts a label, including from inside a SUB.
DIM s AS STRING
READ s: PRINT s
RESTORE Colors
READ s: PRINT s
ShowSizes
READ s: PRINT s
ON ERROR GOTO Trap
RESTORE Missing
END

Trap:
PRINT "Error"; ERR
END

DATA "first"
Colors:
DATA "red", "green"
Sizes:
DATA "small", "large"

SUB ShowSizes
    DIM t AS STRING
    RESTORE Sizes
    READ t: PRINT t
END SUB
