' Compiled expressions follow DEFINT, DEFSTR and DEFDBL run after the program
' was loaded: the same names change type part way through.
x = 7: y = 2
PRINT x / y, x * 1.5
DEFINT A-Z
DIM v(5)
FOR i = 0 TO 5: v(i) = i * 3: NEXT
t = 7: u = 2
PRINT t / u, t \ u, v(u) + v(t - 4), SIN(t) * 100, INT(t / u) * 2
x = 7.6: PRINT x * 2
DEFSTR S
s = "hi"
IF s = "hi" AND t = 7 THEN PRINT "str ok"
PRINT LEN(s) + t
DEFDBL D
d = 1 / 3
PRINT d * 3, d + INT(d)
