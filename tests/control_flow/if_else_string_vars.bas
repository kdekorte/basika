10 REM Test ELSE with string variables
20 A$ = ""
30 IF 1 = 1 THEN A$ = "MATCH" ELSE A$ = "NO MATCH"
40 PRINT A$
50 IF 1 = 0 THEN A$ = "MATCH" ELSE A$ = "NO MATCH"
60 PRINT A$
70 END
