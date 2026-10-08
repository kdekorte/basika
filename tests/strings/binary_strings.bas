' Strings of binary data (MKI$, MKS$, CHR$(0), ...) keep their zero bytes in
' CVI/CVS, LEN, ASC, INSTR and VAL, including MID$ and expression arguments.
PRINT "CVS(MKS$(25)) ="; CVS(MKS$(25))
s$ = MKS$(25): PRINT "via var:"; CVS(s$)
a$ = "AB" + MKS$(25): PRINT "no NUL, MID$:"; CVS(MID$(a$, 3))
a$ = MKI$(5) + MKS$(25)
PRINT "LEN:"; LEN(a$); " MID$ len:"; LEN(MID$(a$, 3)); " CVS(MID$):"; CVS(MID$(a$, 3))
m$ = MID$(a$, 3): PRINT "MID$ into var:"; LEN(m$); CVS(m$)
PRINT "bytes:";
FOR i = 1 TO LEN(a$): PRINT ASC(MID$(a$, i, 1));: NEXT: PRINT
PRINT "CVI(a$) ="; CVI(a$); " CVI(MID$(a$,1)) ="; CVI(MID$(a$, 1))
PRINT INSTR("abc", "c"); INSTR(2, "abcabc", "a"); INSTR("abc", ""); INSTR(5, "abc", "a"); INSTR("ab", "abc")
x$ = "a" + CHR$(0) + "b": PRINT INSTR(x$, "b"); INSTR(x$, CHR$(0)); LEN(x$); ASC(MID$(x$, 2)); VAL("12.5x")
