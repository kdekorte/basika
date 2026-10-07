' WAIT &H3DA, 8 waits for the simulated 60 Hz vertical retrace; other ports
' read as 0, so a WAIT on them returns at once.
T# = TIMER
FOR I = 1 TO 30
    WAIT &H3DA, 8, 8
    WAIT &H3DA, 8
NEXT
E# = TIMER - T#
IF E# > .4 AND E# < .7 THEN PRINT "30 retraces ok" ELSE PRINT "retrace time"; E#
T# = TIMER
WAIT &H378, 1
WAIT 0, 255
PRINT "other ports return"; TIMER - T# < .05
