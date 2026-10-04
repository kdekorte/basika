REM SELECT CASE inside a loop, with EXIT DO from within a CASE branch
I = 0
DO
  I = I + 1
  SELECT CASE I
  CASE 1, 2
    PRINT "small"; I
  CASE 3
    PRINT "exiting at"; I
    EXIT DO
  CASE ELSE
    PRINT "unreached"; I
  END SELECT
LOOP
PRINT "after loop"

FOR J = 1 TO 5
  SELECT CASE J
  CASE 1 TO 2
    PRINT "j-low"; J
  CASE ELSE
    PRINT "j-high"; J
  END SELECT
NEXT J
PRINT "DONE"
END
