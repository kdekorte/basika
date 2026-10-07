' &H, &O and &B literals follow QBasic INTEGER/LONG typing.
PRINT &HFF; &H7FFF; &HFFFF; &HFFFF&; &H10000
PRINT &HFF00FF00; &O777; &B1011; &h1f
' Whole-number literals in LONG range are exact.
PRINT 2147483647; 16777217; 4294967295
X& = 2147483647
PRINT X&
' HEX$ and OCT$ use 16-bit two's complement for INTEGER negatives.
PRINT HEX$(-1); " "; HEX$(-70000); " "; HEX$(&HFF00FF00); " "; HEX$(4294967295); " "; OCT$(-1)
CONST MASK = &HFFFF0000
PRINT MASK; HEX$(MASK)
' More than 7 significant digits makes a DOUBLE; E exponents and ! stay SINGLE.
PRINT 3.14159265358979; 0.000123456789; 1.23456789E+2; 1.23456789!; 3.1415926
' Assigning to INTEGER/LONG rounds half to even.
A% = 2.5: B% = 3.5: C& = -4.5
PRINT A%; B%; C&
