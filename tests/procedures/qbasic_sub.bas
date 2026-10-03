REM Test QBasic SUB subroutines
X = 10: Y = 20
CALL AddTen(X)
IF X <> 20 THEN PRINT "ERROR: SUB byref failed": END
AddTen Y
IF Y <> 30 THEN PRINT "ERROR: implicit SUB byref failed": END
CALL AddTen((X))
IF X <> 20 THEN PRINT "ERROR: SUB byval failed": END
PRINT "QBASIC SUB PASS"
END

SUB AddTen(N)
  N = N + 10
END SUB
