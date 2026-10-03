REM Test QBasic FUNCTION subroutines
PRINT DoubleVal(5)
PRINT Greet$("World")
PRINT CheckPositive(-5)
PRINT CheckPositive(10)
PRINT DoubleVal(DoubleVal(3))
PRINT "QBASIC FUNCTION PASS"
END

FUNCTION DoubleVal(X)
  DoubleVal = X * 2
END FUNCTION

FUNCTION Greet$(N$)
  Greet$ = "Hello, " + N$
END FUNCTION

FUNCTION CheckPositive(N)
  IF N <= 0 THEN CheckPositive = 0: EXIT FUNCTION
  CheckPositive = N * 100
END FUNCTION
