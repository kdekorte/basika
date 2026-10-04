REM SELECT CASE with CASE IS comparisons covering all operators
FOR N = 1 TO 6
  SELECT CASE N
  CASE IS = 1
    PRINT "eq-one"
  CASE IS <> 2
    IF N = 3 OR N = 4 OR N = 5 OR N = 6 THEN
      PRINT "ne-two"
    END IF
  CASE ELSE
    PRINT "is-two"
  END SELECT
NEXT N

SELECT CASE 10
CASE IS < 5
  PRINT "lt5"
CASE IS <= 10
  PRINT "le10"
CASE ELSE
  PRINT "unreached"
END SELECT

SELECT CASE 10
CASE IS > 20
  PRINT "gt20"
CASE IS >= 10
  PRINT "ge10"
CASE ELSE
  PRINT "unreached2"
END SELECT
PRINT "DONE"
END
