10 REM Debug PUT from array element
20 DIM S$(2)
30 S$(1)="foo": S$(2)="bar"
35 PRINT "DBG1:"; S$(1)
36 PRINT "DBG2:"; S$(2)
40 OPEN "tests/putfrom.tmp" FOR RANDOM AS #1
50 PUT #1,1,3,S$(1)
60 PUT #1,2,3,S$(2)
70 CLOSE #1
80 OPEN "tests/putfrom.tmp" FOR RANDOM AS #1
90 GET #1,1,3,A$
100 GET #1,2,3,B$
110 PRINT A$
120 PRINT B$
130 CLOSE #1
140 END
