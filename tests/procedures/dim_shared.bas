' DIM SHARED makes module variables and arrays visible in every procedure.
TYPE Vec
    x AS SINGLE
    y AS SINGLE
END TYPE
DIM SHARED Total AS LONG, Title AS STRING
DIM SHARED Items(1 TO 3) AS INTEGER
DIM SHARED Origin AS Vec
DIM SHARED Hits
DIM Hidden AS INTEGER
Hidden = 99
Title = "shared"
Origin.x = 1.5
CALL Fill
CALL Accumulate(5)
PRINT Title; Total; Items(1); Items(3); Origin.x; Origin.y; Hits
PRINT LocalOnly; Hidden
PRINT Sum3

SUB Fill
    DIM LocalOnly AS INTEGER
    FOR i = 1 TO 3
        Items(i) = i * 10
    NEXT
    Origin.y = Origin.x * 2
    Hits = Hits + 1
    LocalOnly = 42
    Hidden = 1
    Title = Title + "!"
END SUB

SUB Accumulate (n AS INTEGER)
    Total = Total + n * 1000000
    Hits = Hits + 1
END SUB

FUNCTION Sum3
    Sum3 = Items(1) + Items(2) + Items(3)
END FUNCTION
