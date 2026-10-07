' QB64 integer types: _BYTE, _INTEGER64 and their _UNSIGNED forms, the
' suffixes %%, ~%, ~%%, && and ~&&, and DEFLNG. Unsigned types wrap; signed
' ones report Overflow.
DIM a AS _INTEGER64, b AS _UNSIGNED INTEGER, c AS _BYTE, d AS _UNSIGNED _BYTE
a = 9007199254740992: b = -1: c = 100: d = 300
PRINT a; b; c; d
x&& = 123456789012#: y~% = 70000: z%% = -128: w~%% = 256
PRINT x&&; y~%; z%%; w~%%
DEFLNG L
L1 = 3000000000# - 900000000: PRINT L1
TYPE R
    i AS _INTEGER64
    b AS _BYTE
    u AS _UNSIGNED INTEGER
    s AS _UNSIGNED _BYTE
END TYPE
DIM r AS R, q AS R
r.i = -5000000000#: r.b = -3: r.u = 65000: r.s = 250
OPEN "tests/qb64_types.tmp" FOR RANDOM AS #1 LEN = 12
PUT #1, 1, r
GET #1, 1, q
PRINT q.i; q.b; q.u; q.s; "record bytes"; LOF(1)
CLOSE #1
KILL "tests/qb64_types.tmp"
ON ERROR GOTO Trap
c = 200
END
Trap:
PRINT "overflow"; ERR
RESUME NEXT
