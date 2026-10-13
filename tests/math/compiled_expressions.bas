' Expressions the interpreter compiles must give what the parser gives:
' values, number kinds (PRINT shows 16 digits for doubles) and side effects.
CONST N = 3
CONST BIG# = 1.5
DIM a(10), d#(5), m(3, 4)
FOR i = 0 TO 10: a(i) = i * i: NEXT
FOR i = 0 TO 3: FOR j = 0 TO 4: m(i, j) = i * 10 + j: NEXT: NEXT
d#(2) = 1 / 3
x = 2.5: y = -7: k% = 5: l& = 100000: q# = 1 / 7
' A lone variable subscript reads the variable, not the CONST: a(N) is a(0).
PRINT a(N), a(k% - 2) + 1, m(2, 3) * 2, m(N - 1, k% - 1)
PRINT d#(2), d#(2) * 3, a(x), a(INT(x))
PRINT 2 ^ 10, 2 ^ -1, -2 ^ 2, 2 ^ 3 ^ 2
PRINT x AND 3, y OR 1, NOT k%, k% XOR 3, 5 EQV 3, 0 IMP 7
PRINT x > 1 AND y < 0, NOT x = 2.5, (x = 2.5) OR (y = 0)
PRINT SIN(x) + COS(y), TAN(1) * 2, ATN(1) * 4, SQR(16) + 1, EXP(1) * 1, LOG(10) + 0
PRINT INT(-2.5) + 0, FIX(-2.5) + 0, ABS(y) + 0, SGN(y) * 2, CINT(2.5) + 0, CINT(3.5) + 0, CLNG(1E6 + .5) + 1
PRINT CSNG(q#) * 1, CDBL(1 / 3) * 1, q# * 2, k% * k%, l& + l&
PRINT _RGB32(255, 0, 0) + 0, _RGB32(10) + 0, _RED32(&HFF102030) + 0, _GREEN32(&HFF102030) * 1, _BLUE32(&HFF102030) + 0
PRINT _ROUND(2.5) + 0, _ROUND(3.5) * 1, _PI * 2, _PI(2) + 0
PRINT 7 MOD 3 + 1, 7 \ 2 * 2, 10 / 4 - 1, -7 MOD 3 + 0
PRINT BIG# * 2, N * 2 + 1, N ^ 2
RANDOMIZE 42
PRINT RND * 1, RND(1) * 1, RND(0) * 1, RND(-3) * 1, RND * 1
s = 0
FOR i = 1 TO 1000: s = s + a(i MOD 11) * SIN(i) - INT(i / 3) MOD 5: NEXT
PRINT s
IF a(3) = 9 AND SIN(0) = 0 THEN PRINT "ok1" ELSE PRINT "bad1"
WHILE a(1) + INT(1.5) < 2: a(1) = a(1) + 1: WEND
PRINT a(1)
z = (1 + 2) * (3 + (4 - 5) * 6) / 7 + ABS(-(2 ^ 2))
PRINT z, -z, - -z
PRINT TIMER >= 0 AND TIMER < 86400
' Each RND in one expression is drawn once.
RANDOMIZE 7
r1# = RND: r2# = RND
RANDOMIZE 7
PRINT RND + RND * 10 = r1# + r2# * 10
