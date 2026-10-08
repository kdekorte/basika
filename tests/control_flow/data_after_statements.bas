' DATA may follow other statements on its line (after a colon or ELSE), as
' in QBasic; DATA in a REM or a string does not count.
READ A, B$: PRINT A; B$
READ C: PRINT C
IF 0 THEN PRINT "x" ELSE DATA 1, hello
PRINT "y": DATA 2
REM DATA 99
PRINT "DATA in a string": READ D: PRINT D
DATA 3
