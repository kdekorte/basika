' EXIT FOR leaves the innermost loop and continues after its NEXT: from
' the main program, a SUB and a FUNCTION, inside block IFs and SELECT CASE,
' with NEXT b, a lists and on one line. A FOR that runs zero times skips
' a NEXT list correctly too.
DIM SHARED hits AS INTEGER
SUB Nested
    DIM i AS INTEGER, j AS INTEGER
    FOR i = 1 TO 3
        FOR j = 1 TO 5
            IF j = 2 THEN
                IF i > 0 THEN
                    EXIT FOR
                END IF
            END IF
            hits = hits + 1
        NEXT j
        hits = hits + 100
    NEXT i
END SUB
FUNCTION Find% (target AS INTEGER)
    DIM k AS INTEGER, r AS INTEGER
    r = -1
    FOR k = 0 TO 9
        SELECT CASE k
            CASE target
                r = k
                EXIT FOR
        END SELECT
    NEXT
    Find% = r
END FUNCTION
Nested
PRINT "nested:"; hits
PRINT "find:"; Find%(4); Find%(42)
FOR a = 1 TO 3: FOR b = 1 TO 3: IF b = 2 THEN EXIT FOR
NEXT b, a
PRINT "next list: a ="; a; "b ="; b
FOR a = 1 TO 5: IF a = 3 THEN EXIT FOR
NEXT: PRINT "one line: a ="; a
c = 0
FOR a = 1 TO 50
    FOR b = 1 TO 2
        IF b = 1 THEN
            EXIT FOR
        END IF
    NEXT
    c = c + 1
NEXT
PRINT "block IF inside, 50 times:"; c
FOR a = 1 TO 2: FOR b = 5 TO 1: PRINT "no";
NEXT b, a
PRINT "zero-run list: a ="; a
