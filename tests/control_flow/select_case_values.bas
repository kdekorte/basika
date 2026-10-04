REM SELECT CASE with single values and comma-separated value lists
FOR N = 1 TO 5
  SELECT CASE N
  CASE 1
    PRINT "one"
  CASE 2, 3
    PRINT "two-or-three"
  CASE 4, 5
    PRINT "four-or-five"
  END SELECT
NEXT N
PRINT "DONE"
END
