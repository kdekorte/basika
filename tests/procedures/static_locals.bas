' SUB ... STATIC keeps all locals between calls; a STATIC statement keeps the
' listed ones. Arrays and records in a STATIC procedure are allocated once.
TYPE Rec
    v AS INTEGER
END TYPE
FOR k = 1 TO 3
    Counter
    Keep
    Memo k
NEXT
PRINT NextId; NextId; NextId

SUB Counter STATIC
    n = n + 1
    PRINT "static sub n ="; n
END SUB

SUB Keep
    STATIC total, label AS STRING
    other = other + 1
    total = total + 10
    label = label + "*"
    PRINT "STATIC total ="; total; " other ="; other; " "; label
END SUB

SUB Memo (n) STATIC
    DIM seen(1 TO 5)
    DIM last AS Rec
    seen(n) = n * 100
    PRINT "memo"; n; seen(1); seen(2); seen(3); last.v
    last.v = n
END SUB

FUNCTION NextId
    STATIC id AS INTEGER
    id = id + 1
    NextId = id
END FUNCTION
