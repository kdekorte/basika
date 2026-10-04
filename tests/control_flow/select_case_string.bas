REM SELECT CASE with string expressions: value list, TO range, IS, and ELSE
DATA "alice", "bob", "mallory", "zed", "quux"
FOR N = 1 TO 5
  READ NAME$
  SELECT CASE NAME$
  CASE "alice", "bob"
    PRINT "known: "; NAME$
  CASE "charlie" TO "n"
    PRINT "mid-alpha: "; NAME$
  CASE IS > "z"
    PRINT "after-z: "; NAME$
  CASE ELSE
    PRINT "other: "; NAME$
  END SELECT
NEXT N
PRINT "DONE"
END
