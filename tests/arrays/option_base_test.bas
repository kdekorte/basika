10 OPTION BASE 1
20 DIM A(5)
30 A(1) = 10 : A(5) = 50
40 PRINT "A(1)="; A(1); " A(5)="; A(5)
50 ON ERROR GOTO 100
60 PRINT "Testing bounds (should error):"
70 A(0) = 0
80 GOTO 120
100 PRINT "Error trapped correctly for lower bound"
110 RESUME NEXT
120 PRINT "Testing upper bounds (should error):"
130 A(6) = 0
140 END
150 PRINT "Error trapped correctly for upper bound"
160 RESUME NEXT