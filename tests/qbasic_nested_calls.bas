CALL CheckLoopValues
PRINT "NESTED LOOP PASS"
END

SUB CheckLoopValues
  FOR LoopIndex = 1 TO 2
    CALL PrintLoopValue((LoopIndex))
  NEXT LoopIndex
END SUB

SUB PrintLoopValue(Value)
  PRINT "Loop index: "; STR$(Value)
  PRINT "Function result: "; STR$(AddOffset(Value))
END SUB

FUNCTION AddOffset(Value)
  AddOffset = Value + 10
END FUNCTION