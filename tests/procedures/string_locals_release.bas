' String parameters and locals are released when procedures return, so
' calling them many more times than the string pool has slots still works.
FOR i = 1 TO 6000
    Label "card", i
    s$ = Shout$("x")
NEXT
PRINT total; s$

SUB Label (title AS STRING, n AS INTEGER)
    SHARED total
    DIM padded AS STRING
    padded = title + STRING$(3, "-")
    total = total + LEN(padded)
END SUB

FUNCTION Shout$ (word AS STRING)
    DIM loud AS STRING
    loud = UCASE$(word) + "!"
    Shout$ = loud
END FUNCTION
