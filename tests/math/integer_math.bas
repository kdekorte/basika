' Integer arithmetic is exact and follows QBasic: INTEGER and LONG results
' that do not fit are Overflow errors, _INTEGER64 is exact over its whole
' range, and _UNSIGNED _INTEGER64 wraps. Also covers operator precedence.
DECLARE SUB Swapper (a AS LONG, b AS LONG)
DEF FNHALF% (X) = X / 2
PRINT 2 ^ 3; 2 ^ 3 ^ 2; -2 ^ 2; 2 ^ -1
PRINT 7 MOD 4 * 2; 10 \ 3 * 2; 10 - 7 MOD 4; 17 \ 5 MOD 2
big&& = 9007199254740993: PRINT big&&; big&& + 1; big&& * 2 - 1
max&& = 9223372036854775807: PRINT max&&; -max&& - 1
CONST HUGE = 123456789012345678: PRINT HUGE; HUGE MOD 1000
u~&& = 18446744073709551615: PRINT u~&&; u~&& + 1; u~&& \ 3
PRINT big&& > 9007199254740992; big&& = 9007199254740992#; 3 < 3.5
PRINT HEX$(-1); " "; HEX$(-1&); " "; HEX$(-1&&); " "; HEX$(&H7FFF); " "; OCT$(8)
PRINT HEX$(&HFFFFFFFFFFFFFFFF~&&); " "; HEX$(&HFFFFFFFF&&); " "; HEX$(&HFFFF&)
PRINT CINT(2.5); CINT(3.5); CINT(-2.5); CLNG(100000.5); CSNG(0.1); CDBL(0.5)
PRINT STR$(1234567); STR$(-42); STR$(big&&); STR$(0.25)
x% = 300: PRINT CLNG(x%) * x%
PRINT 5 \ 2; -7 \ 2; -7 MOD 2; 7.6 \ 2; 7 MOD -3
PRINT NOT 0; NOT 1.7; 5 AND 3; 5 OR 3; 5 XOR 3; &HF0F0 AND &HFF
PRINT FNHALF%(5); FNHALF%(7)
PRINT Total&(4000)
FOR i% = 1 TO 3: PRINT i%;: NEXT: PRINT
FOR k& = 10 TO 1 STEP -3: PRINT k&;: NEXT: PRINT
FOR q% = 1 TO 2.6: PRINT q%;: NEXT: PRINT
READ d&&, e&&: PRINT d&&; e&& - d&&
DIM p AS LONG, r AS LONG
p = 1: r = 2: Swapper p, r: PRINT p; r
ON ERROR GOTO Trap
a% = 200: b% = 200
PRINT "INTEGER * INTEGER": c& = a% * b%
PRINT "LONG + 1": z& = 2147483647: z& = z& + 1
PRINT "_INTEGER64 + 1": w&& = max&& + 1
PRINT "negate -32768": n% = -32768: n% = -n%
PRINT "-32768 \ -1": n% = -32768: n% = n% \ -1
PRINT "FOR past 32767": FOR n% = 32766 TO 32767: NEXT
PRINT "store 40000": n% = 40000
PRINT "done"
END
Trap:
PRINT "  Overflow"; ERR
RESUME NEXT
DATA 9007199254740993, 9007199254740999

FUNCTION Total& (n AS LONG)
    Total = n * 100000
END FUNCTION

SUB Swapper (a AS LONG, b AS LONG)
    DIM t1 AS LONG, t2 AS LONG
    t1 = a: t2 = b
    SWAP t1, t2
    a = t1: b = t2
END SUB
