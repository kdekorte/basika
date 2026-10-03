10 REM Test nested IF THEN ELSE
20 X = 0: Y = 0
30 IF 5 > 3 THEN IF 2 > 1 THEN X = 1 ELSE X = 2 ELSE Y = 1
40 PRINT "X="; X; "Y="; Y
50 X = 0: Y = 0
60 IF 3 > 5 THEN X = 1 ELSE IF 2 > 1 THEN Y = 1 ELSE Y = 2
70 PRINT "X="; X; "Y="; Y
80 END
