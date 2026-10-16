' QB64 math and bit functions. Values come from the QB64 Phoenix wiki
' examples where it gives them (_D2G(60) = 66.66667, _SETBIT(0, 6) = 64,
' ...); the rest are worked out by hand.
ON ERROR GOTO Trap
DIM SHARED e AS INTEGER

PRINT "MIN MAX:"; _MIN(3, 7); _MAX(3, 7); _MIN(-2.5, 4); _MAX(5.0001, 5); _MIN(4, 4)
big&& = 9007199254740993
IF _MAX(big&&, 1) = big&& THEN PRINT "MAX keeps an _INTEGER64 exact"
PRINT "CLAMP:"; _CLAMP(5, 1, 10); _CLAMP(-3, 1, 10); _CLAMP(99, 1, 10)
PRINT "CEIL:"; _CEIL(2.5); _CEIL(-2.5); _CEIL(7); _CEIL(2.0001)
PRINT "ROUND:"; _ROUND(2.5); _ROUND(-2.5); _ROUND(3.7)
IF _ROUND(big&&) = big&& THEN PRINT "ROUND keeps an _INTEGER64 exact"
PRINT "HYPOT ATAN2:"; _HYPOT(3, 4); _ATAN2(1, 1); _ATAN2(0, -1); _ATAN2(0, 0); _ATAN2(-1, 0)
PRINT "ACOS ASIN:"; _ACOS(.5); _ASIN(.5); _ACOS(1); _ASIN(-1)
PRINT "ACOSH ASINH ATANH:"; _ACOSH(1); _ASINH(0); _ATANH(0)
PRINT "COSH SINH TANH:"; _COSH(0); _SINH(0); _TANH(0)
PRINT "SEC CSC COT:"; _SEC(0); _CSC(_PI / 2); _COT(_PI / 4)
PRINT "ARCSEC ARCCSC ARCCOT:"; _ARCSEC(1); _ARCCSC(1); _ARCCOT(1)
PRINT "SECH:"; _SECH(0)
PRINT "angles:"; _D2R(180); _R2D(_PI); _D2G(60); _G2D(60); _R2G(.5); _G2R(200)
PRINT "double precision:"; _HYPOT(1#, 1#)

PRINT "SHL SHR:"; _SHL(1, 8); _SHR(256, 4); _SHR(-16, 2); _SHL(1, 40)
b~%% = 1
PRINT "SHL of a byte:"; _SHL(b~%%, 8)
b~%% = _SHL(b~%%, 8)
PRINT "stored in the byte:"; b~%%
r~%% = &B10000001
PRINT "ROL ROR _UNSIGNED _BYTE:"; _ROL(r~%%, 1); _ROR(r~%%, 1)
DIM w AS _UNSIGNED INTEGER
w = &H8001
PRINT "ROL ROR _UNSIGNED INTEGER:"; _ROL(w, 4); _ROR(w, 4)
DIM i AS INTEGER
i = 1
PRINT "ROL INTEGER:"; _ROL(i, 15); _ROL(i, 16)
l& = 1
PRINT "ROL ROR LONG:"; _ROL(l&, 31); _ROR(l&, 1)
PRINT "SETBIT RESETBIT TOGGLEBIT:"; _SETBIT(0, 6); _RESETBIT(255, 0); _TOGGLEBIT(0, 4); _TOGGLEBIT(16, 4)
PRINT "READBIT:"; _READBIT(5, 2); _READBIT(5, 1); _READBIT(-1, 63)
FOR k = 1 TO 3: t& = t& + _SHL(1, k) + _MAX(k, 2): NEXT
PRINT "in a loop:"; t&
' Inside a larger expression _ROL and _ROR are compiled; they must agree
' with the parser for every width and sign.
i% = 1: b~%% = 129: s%% = -127: l& = 1: u~& = 1
FOR k = 0 TO 17
    c1 = _ROL(i%, k) + 0: c2 = _ROR(b~%%, k) + 0: c3 = _ROL(s%%, k) + 0
    c4 = _ROR(l&, k) + 0: c5 = _ROL(u~&, k) + 0
    IF c1 <> _ROL(i%, k) OR c2 <> _ROR(b~%%, k) OR c3 <> _ROL(s%%, k) OR c4 <> _ROR(l&, k) OR c5 <> _ROL(u~&, k) THEN bad = bad + 1
NEXT
PRINT "compiled ROL/ROR differs from the parser:"; bad
PRINT "ROL at the edges:"; _ROL(s%%, 1); _ROL(u~&, 31)

PRINT "--- errors"
x = _ACOS(2): PRINT "ACOS(2):"; e: e = 0
x = _ATANH(1): PRINT "ATANH(1):"; e: e = 0
x = _COTH(0): PRINT "COTH(0):"; e: e = 0
x = _MIN(1): PRINT "MIN of one:"; e: e = 0
x = _SETBIT(1, -1): PRINT "SETBIT bit -1:"; e: e = 0
END

Trap:
e = ERR
RESUME NEXT
