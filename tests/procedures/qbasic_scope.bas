REM Test QBasic Scoping & SHARED variables
G = 100
L = 50
CALL TestScope((L))
IF L <> 50 THEN PRINT "ERROR: caller variable modified when passed byval": END
IF G <> 200 THEN PRINT "ERROR: SHARED variable G not updated in SUB": END
PRINT "QBASIC SCOPE PASS"
END

SUB TestScope(Param)
  SHARED G
  L = 999
  G = G + 100
END SUB
