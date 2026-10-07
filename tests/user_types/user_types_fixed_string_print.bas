' Fixed-length string fields work in PRINT, comparisons and LEN, including in
' arrays of records and through SHARED inside a procedure.
TYPE Rec
    id AS INTEGER
    label AS STRING * 4
    tags(1 TO 2) AS STRING * 3
END TYPE
DIM r AS Rec, items(1 TO 2) AS Rec
DIM nick AS STRING * 5
r.label = "ORB": r.tags(2) = "ab"
items(2).label = "SUN"
nick = "kd"
PRINT r.label; "|"; r.tags(2); "|"; items(2).label; "|"; nick; "|"
PRINT LEN(r.label); LEN(nick)
IF r.label = "ORB " AND nick = "kd   " THEN PRINT "compare ok"
IF items(2).label = "SUN " AND items(2).label <> "MOON" THEN PRINT "array compare ok"
PRINT items(2).label + "!"
Show

SUB Show
    SHARED items() AS Rec, r AS Rec
    PRINT "sub:"; items(2).label; r.id
END SUB
