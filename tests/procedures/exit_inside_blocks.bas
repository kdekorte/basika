' EXIT SUB from inside FOR, WHILE, DO, SELECT CASE and a block IF leaves
' nothing open behind it, whether the SUB runs from the main program or
' from a FUNCTION; repeating it used to end in "Out of memory".
SUB ExitFor (n AS INTEGER)
    DIM i AS INTEGER
    FOR i = 1 TO 10
        IF i = n THEN EXIT SUB
    NEXT
END SUB
SUB ExitWhile
    DIM i AS INTEGER
    WHILE i < 10
        i = i + 1
        IF i = 3 THEN EXIT SUB
    WEND
END SUB
SUB ExitDo
    DO
        EXIT SUB
    LOOP
END SUB
SUB ExitBlocks (k$)
    IF LEN(k$) > 0 THEN
        SELECT CASE k$
            CASE "a": EXIT SUB
        END SELECT
    END IF
END SUB
SUB Count (total AS INTEGER)
    DIM j AS INTEGER
    FOR j = 1 TO 4
        total = total + 1
    NEXT
END SUB
FUNCTION Loops% (times AS INTEGER)
    DIM t AS INTEGER, total AS INTEGER
    FOR t = 1 TO times
        ExitFor 2: ExitWhile: ExitDo: ExitBlocks "a"
        Count total
    NEXT
    Loops% = total
END FUNCTION
DIM r AS INTEGER, total AS INTEGER
FOR r = 1 TO 50
    ExitFor 3: ExitWhile: ExitDo: ExitBlocks "a"
    Count total
NEXT
PRINT "main"; total
PRINT "function"; Loops%(50)
