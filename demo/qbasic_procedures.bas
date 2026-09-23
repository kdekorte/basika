' QBasic SUB and FUNCTION Demo for Basika
' Demonstrates modern modular QBasic style programming in Basika

CLS
CALL PrintBanner("QBASIC SUB & FUNCTION DEMO")

PRINT "--- Numeric Functions ---"
PRINT "Factorial(5) = "; Factorial(5)
PRINT "Factorial(7) = "; Factorial(7)
PRINT "GCD(48, 18)  = "; GCD(48, 18)
PRINT

PRINT "--- String Functions ---"
PRINT CenterText$("Basika Interpreter", 40)
PRINT PadRight$("Item:", 15); "Passed"
PRINT

PRINT "--- Subroutine Pass-by-Reference & SHARED ---"
SHARED_SCORE = 100
PRINT "Initial Score: "; SHARED_SCORE
CALL AddPoints(SHARED_SCORE, 25)
PRINT "Score after AddPoints: "; SHARED_SCORE

VAL_VAR = 10
CALL TryModifyValue((VAL_VAR))
PRINT "ByVal variable after call (should be 10): "; VAL_VAR
PRINT

PRINT "--- All QBasic Procedure Demos Completed Successfully! ---"
END

FUNCTION Factorial(N)
  IF N <= 1 THEN Factorial = 1: EXIT FUNCTION
  Factorial = N * Factorial(N - 1)
END FUNCTION

FUNCTION GCD(A, B)
  WHILE B <> 0
    TEMP = B
    B = A MOD B
    A = TEMP
  WEND
  GCD = A
END FUNCTION

FUNCTION CenterText$(T$, WidthVal)
  IF LEN(T$) >= WidthVal THEN CenterText$ = T$: EXIT FUNCTION
  PadLen = (WidthVal - LEN(T$)) \ 2
  CenterText$ = SPACE$(PadLen) + T$
END FUNCTION

FUNCTION PadRight$(T$, WidthVal)
  IF LEN(T$) >= WidthVal THEN PadRight$ = T$: EXIT FUNCTION
  PadRight$ = T$ + SPACE$(WidthVal - LEN(T$))
END FUNCTION

SUB PrintBanner(Title$)
  BORDER$ = STRING$(LEN(Title$) + 6, "=")
  PRINT BORDER$
  PRINT "=  "; Title$; "  ="
  PRINT BORDER$
END SUB

SUB AddPoints(Score, Pts)
  Score = Score + Pts
END SUB

SUB TryModifyValue(X)
  X = X + 999
END SUB

