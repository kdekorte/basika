' String comparisons combine with AND, OR and NOT like numeric ones.
k$ = " "
IF k$ <> "" AND k$ <> " " THEN PRINT "wrong1" ELSE PRINT "ok1"
k$ = "q"
IF k$ <> "" AND k$ <> " " THEN PRINT "ok2" ELSE PRINT "wrong2"
IF k$ = "x" OR k$ = "q" THEN PRINT "ok3"
IF NOT k$ = "x" THEN PRINT "ok4"
IF LEN(k$) = 1 AND k$ >= "a" THEN PRINT "ok5"
x = (k$ = "q") * 5
PRINT x; (k$ < "r") AND 7
DO
LOOP UNTIL k$ <> "" AND k$ <> " "
PRINT "ok6"
a$ = "ab" + "c"
IF a$ + "d" = "abcd" THEN PRINT "ok7"
SELECT CASE k$
    CASE "q": PRINT "ok8"
END SELECT
SUB Foo
END SUB
