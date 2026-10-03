10 REM GET into array element
20 DIM T$(2)
30 OPEN "tests/getinto.tmp" FOR RANDOM AS #1
40 PUT #1,1,3,"aaa"
50 PUT #1,2,3,"bbb"
60 GET #1,1,3,T$(1)
70 GET #1,2,3,T$(2)
80 PRINT T$(1)
90 PRINT T$(2)
100 CLOSE #1
110 END

