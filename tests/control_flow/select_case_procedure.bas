REM SELECT CASE inside a SUB and a FUNCTION
CALL Describe(1)
CALL Describe(5)
CALL Describe(99)
PRINT Classify$(3)
PRINT Classify$(30)
PRINT "DONE"
END

SUB Describe(N)
  SELECT CASE N
  CASE 1
    PRINT "one"
  CASE 2 TO 10
    PRINT "small"
  CASE ELSE
    PRINT "large"
  END SELECT
END SUB

FUNCTION Classify$(N)
  SELECT CASE N
  CASE IS < 10
    Classify$ = "single-digit"
  CASE ELSE
    Classify$ = "multi-digit"
  END SELECT
END FUNCTION
