REM Nested DO loops with EXIT DO breaking only the innermost loop
FOR I = 1 TO 2
  J = 1
  DO
    PRINT I; J
    IF J >= 2 THEN EXIT DO
    J = J + 1
  LOOP
NEXT I
PRINT "DONE"
END
