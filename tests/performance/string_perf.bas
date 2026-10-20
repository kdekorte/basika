' String performance probe: large assignments and concatenations.
' Prints the elapsed seconds of three hot string patterns so the cost of
' copying large strings can be compared between builds.
DIM t0 AS DOUBLE, t1 AS DOUBLE, t2 AS DOUBLE, t3 AS DOUBLE, t4 AS DOUBLE, t5 AS DOUBLE

' 1) Large assignment: a$ = b$ (1 MB, 500 times)
b$ = STRING$(1048576, "b")
t0 = TIMER
FOR i = 1 TO 500
    a$ = b$
NEXT
t1 = TIMER

' 2) Growing concatenation: s$ = s$ + p$ (256 KB base, 2000 appends of 1 KB)
s$ = STRING$(262144, "s")
p$ = STRING$(1024, "p")
t2 = TIMER
FOR i = 1 TO 2000
    s$ = s$ + p$
NEXT
t3 = TIMER

' 3) Multi-term concatenation: t$ = a$ + p$ + p$ (300 times, 1 MB result)
t4 = TIMER
FOR i = 1 TO 300
    t$ = a$ + p$ + p$
NEXT
t5 = TIMER

PRINT USING "assign: #.### s  append: #.### s  concat: #.### s"; t1 - t0; t3 - t2; t5 - t4
PRINT "lengths:"; LEN(a$); LEN(s$); LEN(t$); ASC(RIGHT$(a$, 1)); ASC(RIGHT$(s$, 1)); ASC(RIGHT$(t$, 1))
SYSTEM
