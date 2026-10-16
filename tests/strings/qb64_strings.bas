' QB64 string functions: _TRIM$, _TOSTR$, _BIN$, _INSTRREV, _STRCMP and
' _STRICMP, as the QB64 Phoenix wiki describes them.
ON ERROR GOTO Trap
DIM SHARED e AS INTEGER

' _TRIM$ removes spaces only.
PRINT "["; _TRIM$("  hi there  "); "] ["; _TRIM$(CHR$(9) + " x " + CHR$(9)); "] ["; _TRIM$("   "); "]"
' _TOSTR$ is STR$ without the leading space; digits limit floats only.
PRINT "["; _TOSTR$(42); "] ["; _TOSTR$(-7); "] ["; _TOSTR$(1.5); "] ["; _TOSTR$(1 / 3); "] ["; _TOSTR$(1# / 3); "]"
PRINT "["; _TOSTR$(_PI, 3); "] ["; _TOSTR$(_PI(10), 2); "] ["; _TOSTR$(12345, 2); "]"
' _BIN$: no leading zeros; negatives at their type's width; floats lose
' their fraction.
PRINT _BIN$(0); " "; _BIN$(5); " "; _BIN$(255); " "; _BIN$(-1); " "; _BIN$(-123.45)
x& = -1: PRINT _BIN$(x&)
y&& = -2: PRINT LEN(_BIN$(y&&)); RIGHT$(_BIN$(y&&), 4)
' _INSTRREV([start,] text$, find$): searches back from start, or the end.
PRINT _INSTRREV("abcabc", "bc"); _INSTRREV(4, "abcabc", "bc"); _INSTRREV(0, "abcabc", "c"); _INSTRREV(1, "abcabc", "a")
PRINT _INSTRREV("abc", "z"); _INSTRREV("", "a"); _INSTRREV("abc", ""); _INSTRREV("ab", "abc")
s$ = "one two three": PRINT "last word: "; MID$(s$, _INSTRREV(s$, " ") + 1)
' _STRCMP compares bytes; _STRICMP ignores case.
PRINT _STRCMP("a", "b"); _STRCMP("b", "a"); _STRCMP("a", "a"); _STRCMP("A", "a"); _STRCMP("ab", "a")
PRINT _STRICMP("A", "a"); _STRICMP("Apple", "apricot"); _STRICMP("Z", "a")
IF _STRCMP("x", "y") < 0 THEN PRINT "in IF ok"
t$ = "<" + _TRIM$("  y  ") + ">" + _TOSTR$(3): PRINT t$

PRINT "--- errors"
x$ = _TRIM$(5): PRINT "TRIM$ of a number:"; e: e = 0
x$ = _TOSTR$(1.5, -1): PRINT "TOSTR$ negative digits:"; e: e = 0
x = _STRCMP("a", 1): PRINT "STRCMP of a number:"; e: e = 0
END

Trap:
e = ERR
RESUME NEXT
