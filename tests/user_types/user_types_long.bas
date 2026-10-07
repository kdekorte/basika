' LONG and _UNSIGNED LONG fields store 32-bit values, including in records.
TYPE Sprite
    id AS LONG
    tint AS _UNSIGNED LONG
    frames(1 TO 2) AS LONG
    label AS STRING * 4
END TYPE
DIM s AS Sprite, t AS Sprite
s.id = -2000000000
s.tint = &HFF8040C0
s.frames(1) = 123456789
s.frames(2) = -1
s.label = "ORB"
PRINT s.id; s.tint; HEX$(s.tint); s.frames(1); s.frames(2)
OPEN "tests/user_types_long.tmp" FOR RANDOM AS #1
PUT #1, 1, s
GET #1, 1, t
CLOSE #1
KILL "tests/user_types_long.tmp"
PRINT t.id; t.tint; t.frames(1); t.frames(2)
