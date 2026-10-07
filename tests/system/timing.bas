' _DELAY pauses for fractional seconds, _LIMIT paces a loop, SLEEP takes seconds.
T0# = TIMER
_DELAY .2
T1# = TIMER
FOR I = 1 TO 5
    _LIMIT 20
NEXT
T2# = TIMER
SLEEP 1
T3# = TIMER
' Allow for the clock passing midnight during the test.
IF T1# < T0# THEN T1# = T1# + 86400
IF T2# < T0# THEN T2# = T2# + 86400
IF T3# < T0# THEN T3# = T3# + 86400
IF T1# - T0# >= .18 AND T1# - T0# < 1 THEN PRINT "DELAY OK" ELSE PRINT "DELAY"; T1# - T0#
IF T2# - T1# >= .18 AND T2# - T1# < 1 THEN PRINT "LIMIT OK" ELSE PRINT "LIMIT"; T2# - T1#
IF T3# - T2# >= .9 AND T3# - T2# < 2 THEN PRINT "SLEEP OK" ELSE PRINT "SLEEP"; T3# - T2#
