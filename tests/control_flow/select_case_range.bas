REM SELECT CASE with TO ranges, including a check that only the first
REM matching branch executes even when ranges overlap
FOR N = 0 TO 12 STEP 4
  SELECT CASE N
  CASE 0 TO 4
    PRINT "low"
  CASE 4 TO 8
    PRINT "mid"
  CASE ELSE
    PRINT "high"
  END SELECT
NEXT N
PRINT "DONE"
END
