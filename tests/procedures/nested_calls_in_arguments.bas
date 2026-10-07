' Procedure calls inside another call's arguments keep every argument, a SUB
' called from a FUNCTION returns to it, and FUNCTION names (with or without
' parentheses) pass values.
PRINT Add(1, Twice(3)); Add(Twice(2), 5); Add(Twice(Twice(1)), Add(1, 1))
Show 1, Twice(4)
Show Answer, Seven~&()
PRINT "result"; Outer(5)
IF 1 THEN Show Twice(1), 0

SUB Show (a, b)
    PRINT "show"; a; b
END SUB

SUB Helper (v)
    PRINT "helper"; v
END SUB

FUNCTION Outer (n)
    Helper n
    Show n, Twice(n)
    Outer = n * 10
END FUNCTION

FUNCTION Twice (n)
    Twice = n * 2
END FUNCTION

FUNCTION Add (a, b)
    Add = a + b
END FUNCTION

FUNCTION Answer
    Answer = 42
END FUNCTION

FUNCTION Seven~& ()
    Seven~& = 7
END FUNCTION
