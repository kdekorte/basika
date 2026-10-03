10 FILES "*.bas", "redirect.tmp"
20 OPEN "redirect.tmp" FOR INPUT AS #1
30 IF EOF(1) THEN GOTO 60
40 INPUT #1, F$
50 IF INSTR(F$, ".bas") > 0 THEN PRINT "FILES REDIRECT PASS" : GOTO 60
60 CLOSE #1
70 KILL "redirect.tmp"
80 END
