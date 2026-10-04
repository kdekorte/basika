REM Nested DO...LOOP within DO...LOOP; EXIT DO breaks only innermost
I = 1
DO WHILE I <= 2
  J = 1
  DO
    PRINT I; J
    IF J >= 2 THEN EXIT DO
    J = J + 1
  LOOP
  I = I + 1
LOOP
PRINT "DONE"
END
