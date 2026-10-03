10 A$ = "apple"
20 B$ = "banana"
30 IF A$ = "apple" THEN PRINT "1"
40 IF A$ <> B$ THEN PRINT "2"
50 IF A$ < B$ THEN PRINT "3"
60 IF B$ > A$ THEN PRINT "4"
70 IF A$ <= "apple" THEN PRINT "5"
80 IF B$ >= "banana" THEN PRINT "6"
90 IF A$ + "pie" = "applepie" THEN PRINT "7"
100 IF "CAT" < "DOG" THEN PRINT "8"
110 IF "DOG" > "CAT" THEN PRINT "9"
120 IF (A$ = "apple") THEN PRINT "10"
130 IF "zebra" > "apple" THEN PRINT "11"
140 IF "100" < "2" THEN PRINT "12" ' Numeric string comparison
145 LINENUM = 1
150 IF "line" + STR$(LINENUM) = "line 1" THEN PRINT "13" ' String concat with numeric var
160 END