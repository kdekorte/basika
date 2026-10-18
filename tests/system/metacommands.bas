' Metacommands: $INCLUDE, $INCLUDEONCE, $LET and $IF
$LET DEBUG = 1
$LET LEVEL = 3
'$INCLUDE:'include/lib.bi'
$INCLUDE: 'include/lib.bi'
$IF DEBUG THEN
PRINT "debug on"
$ELSE
PRINT "debug off"
$END IF
$IF LEVEL < 2 THEN
PRINT "low"
$ELSEIF LEVEL = 3 AND NOT WIN THEN
PRINT "three"
$ELSEIF LEVEL = 3 THEN
PRINT "three again"
$ELSE
PRINT "high"
$END IF
$IF UNDEFINED_THING THEN
PRINT "wrong"
$END IF
$IF DEBUG THEN
$IF LEVEL > 5 THEN
PRINT "nested wrong"
$ELSE
PRINT "nested right"
$END IF
$END IF
$CONSOLE
Hello 5
PRINT Greeting$
