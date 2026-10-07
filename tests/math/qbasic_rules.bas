' QBasic rules: rounded array subscripts, math-function errors, number
' formatting, EQV/IMP, and MKL$/CVL.
DIM a(5), b(2.6)
FOR i = 0 TO 5: a(i) = i * 10: NEXT
PRINT a(1.5); a(2.5); a(1.4); a(0.6); UBOUND(b)
PRINT .5; -.25; 1E+20; 1D+300; 1 / 4; STR$(.5); STR$(-.75)
WRITE .5, -1.5
PRINT -1 EQV -1; -1 EQV 0; 5 EQV 3; -1 IMP 0; 0 IMP 0; 5 IMP 3
PRINT 0 XOR -1 EQV 0; -1 IMP 0 IMP 0
IF 3 > 2 EQV 1 > 0 THEN PRINT "EQV in IF"
m$ = MKL$(-123456789)
PRINT LEN(m$); CVL(m$); CVL(MKL$(70000)); CVI(MKI$(2.5)); CVI(MKI$(-300))
PRINT SQR(16); LOG(1); (-2) ^ 3; EXP(0)
ON ERROR GOTO Trap
PRINT "a(5.5)": x = a(5.5)
PRINT "SQR(-1)": x = SQR(-1)
PRINT "LOG(0)": x = LOG(0)
PRINT "EXP(1000)": x = EXP(1000)
PRINT "(-8) ^ (1 / 3)": x = (-8) ^ (1 / 3)
PRINT "0 ^ -1": x = 0 ^ -1
PRINT "1D+300 * 1D+300": x# = 1D+300 * 1D+300
PRINT "SINGLE 1D+39": s! = 1D+39
PRINT "MKI$(40000)": m$ = MKI$(40000)
PRINT "done"
END
Trap:
PRINT "  error"; ERR
RESUME NEXT
