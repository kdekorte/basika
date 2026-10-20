' Exercises the assignment and concatenation paths that move string buffers
' instead of copying them; BASICA value semantics must survive every move:
' a copy must stay independent of its source, parameters must be by value
' unless aliased on purpose, and fixed-length strings must still truncate
' and pad.
DIM a AS STRING * 5
a = "abcdefgh"
PRINT "[" + a + "]"

b$ = "hello"
c$ = b$
PRINT c$
b$ = "world"
IF c$ = "hello" THEN PRINT "cmp-ok"

s$ = "ab"
FOR i = 1 TO 3
    s$ = s$ + "-"
    s$ = s$ + CHR$(65 + i)
NEXT
PRINT s$

x$ = "The quick brown fox"
y$ = LEFT$(x$, 3) + MID$(x$, 5, 5) + RIGHT$(x$, 3) + UCASE$(LEFT$(x$, 3))
PRINT y$

r$ = STRING$(5, "*") + SPACE$(3) + "." + STRING$(2, "ab")
PRINT r$

e$ = ""
f$ = e$ + e$
PRINT "LEN" + STR$(LEN(f$))

PRINT mk$(2)
PRINT mk$(3)
PRINT mk$(2)

z$ = "keep"
SUBTEST "pre-" + z$
PRINT z$

DIM arr$(3)
arr$(0) = "one"
arr$(1) = arr$(0) + "-two"
arr$(2) = arr$(1)
arr$(0) = "changed"
PRINT arr$(1); arr$(2)

SELECT CASE LEFT$(x$, 3)
    CASE "The": PRINT "sel-ok"
    CASE ELSE: PRINT "sel-bad"
END SELECT

DIM d AS STRING = "init-value"
PRINT d

END

FUNCTION mk$ (n)
    mk$ = STRING$(n, "f")
END FUNCTION

SUB SUBTEST (p$)
    PRINT p$
    p$ = "modified"
END SUB
