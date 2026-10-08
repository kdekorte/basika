' A FUNCTION declared with a type suffix can be called, and given its result,
' without the suffix, as in QB64.
DIM P AS LONG
P = Mk(0)
PRINT P; Mk&(1); Mk(2) + 1
PRINT Greet("Bo"); "|"; Greet$("Al"); "|"; LEN(Greet("x"))
PRINT Half(9); Half#(3)
ShowLong Mk(4)
a$ = "abc": DIM s$(3): s$(2) = "four"
PRINT LEN(Greet$("x")); LEN(a$ + "xy"); LEN(a$); LEN(s$(2)); LEN(s$(2) + "!")
SUB ShowLong (v AS LONG)
    PRINT "sub got"; v
END SUB
FUNCTION Mk& (n AS LONG)
    Mk& = -5 - n
END FUNCTION
FUNCTION Greet$ (n$)
    Greet = "hi " + n$
END FUNCTION
FUNCTION Half# (v AS DOUBLE)
    Half = v / 2
END FUNCTION
