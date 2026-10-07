' RANDOM files hold LEN-byte records (default 128) in QBasic's packed
' little-endian layout; BINARY files are addressed by byte position.
TYPE Rec
    id AS INTEGER
    label AS STRING * 5
    score AS SINGLE
    big AS LONG
    precise AS DOUBLE
END TYPE
DIM r AS Rec, q AS Rec
F$ = "tests/random_records.tmp"
OPEN F$ FOR RANDOM AS #1 LEN = 23
r.id = -2: r.label = "AB": r.score = 1.5: r.big = 70000: r.precise = 1 / 3
PUT #1, 2, r
r.id = 7
PUT #1, , r
PRINT "LOF"; LOF(1); "LOC"; LOC(1)
GET #1, 2, q
PRINT q.id; "["; q.label; "]"; q.score; q.big; q.precise
GET #1, , q
PRINT "next record"; q.id; "LOC"; LOC(1)
CLOSE #1

' Reopening keeps the data; read record 2's bytes one at a time.
OPEN F$ FOR RANDOM AS #2 LEN = 1
FIELD #2, 1 AS B$
FOR i = 24 TO 37
    GET #2, i
    PRINT HEX$(ASC(B$)); " ";
NEXT
PRINT
CLOSE

' BINARY positions are bytes: the record written at byte 24 reads back.
OPEN F$ FOR BINARY AS #3
GET #3, 24, q
PRINT "binary"; q.id; q.big; "LOC"; LOC(3)
CLOSE #3

' The GW-BASIC form of OPEN and the default record length of 128:
' record 2 starts at byte 129.
OPEN "R", #4, F$
PUT #4, 2, r
PRINT "default LEN"; LOF(4)
CLOSE

ON ERROR GOTO Trap
OPEN F$ FOR RANDOM AS #5 LEN = 10
PUT #5, 1, r
FIELD #5, 6 AS X$
PUT #5, 0
FIELD #5, 6 AS X$, 6 AS Y$
OPEN F$ FOR RANDOM AS #5
CLOSE
KILL F$
END
' 59 Bad record length, 63 Bad record number, 50 FIELD overflow,
' 55 File already open.
Trap:
PRINT "error"; ERR
RESUME NEXT
