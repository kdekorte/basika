10 REM PUT from array element
20 DIM S$(2)
30 S$(1)="foo": S$(2)="bar"
40 OPEN "tests/putfrom.tmp" FOR RANDOM AS #1
50 PUT #1,1,3,S$(1)
60 PUT #1,2,3,S$(2)
70 GET #1,1,3,A$
80 GET #1,2,3,B$
90 PRINT A$
100 PRINT B$
110 CLOSE #1
120 END
