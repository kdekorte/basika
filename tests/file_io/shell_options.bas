10 ' SHELL _HIDE hides the output, _DONTWAIT returns at once, _SHELLHIDE gives the exit code
20 SHELL _HIDE "echo this must not appear"
30 PRINT _SHELLHIDE("true"); _SHELLHIDE("false")
40 PRINT _SHELLHIDE("exit 7")
50 T0 = TIMER
60 SHELL _HIDE _DONTWAIT "sleep 1"
70 T1 = TIMER
80 IF T1 < T0 THEN T1 = T1 + 86400
90 IF T1 - T0 < .8 THEN PRINT "DONTWAIT OK" ELSE PRINT "WAITED"; T1 - T0
100 PRINT _SHELLHIDE("echo also hidden")
110 END
