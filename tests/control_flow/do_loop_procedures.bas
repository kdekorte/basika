REM DO...LOOP inside a SUB and a FUNCTION
CALL CountUp(3)
PRINT SumTo(4)
PRINT "DONE"
END

SUB CountUp(N)
  I = 1
  DO WHILE I <= N
    PRINT I
    I = I + 1
  LOOP
END SUB

FUNCTION SumTo(N)
  TOTAL = 0
  I = 1
  DO UNTIL I > N
    TOTAL = TOTAL + I
    I = I + 1
  LOOP
  SumTo = TOTAL
END FUNCTION
