' _ROUND rounds halves to even. Whole numbers up to 2^32 - 1, such as 32-bit
' colors, keep their bit pattern in a LONG as in QB64; floats still Overflow.
PRINT _ROUND(2.5); _ROUND(3.5); _ROUND(-2.5); _ROUND(2.6); _ROUND(123456.7)
DEFLNG A-Z
c = _RGB32(255, 255, 255): d = _RGBA32(255, 0, 0, 255)
PRINT c; d; HEX$(d); _RED32(d); _ALPHA32(c)
x& = &HFFFFFFFF: PRINT x&
Keep _RGB32(0, 0, 255)
PRINT _RGB32(255, 255, 255)
ON ERROR GOTO bad
y& = 3000000000#
z& = 5000000000
END
bad: PRINT "Overflow"; ERR: RESUME NEXT
SUB Keep (v)
    PRINT "param"; v
END SUB
