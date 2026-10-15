' PRINT shows a number as its sign (a space when positive), its digits
' and one trailing space; ; adds nothing between items, as in QBasic and
' GW-BASIC. The brackets show where each line starts and ends.
PRINT "["; 1; 2; "]"
PRINT "["; -1; -2; "]"
PRINT "["; 1; -2; 3; "]"
PRINT "["; 1.5; .25; -0.75; "]"
PRINT "["; 2 * 3; 10 \ 3; "]"
A = 7: B$ = "x"
PRINT "["; A; B$; A; "]"
PRINT "["; 1;
PRINT 2; "]"
PRINT 1, 2; "]"
PRINT "[" + STR$(5) + STR$(-5) + "]"
OPEN "tests/print_number_spacing.tmp" FOR OUTPUT AS #1
PRINT #1, 1; 2; -3
CLOSE #1
OPEN "tests/print_number_spacing.tmp" FOR INPUT AS #1
LINE INPUT #1, L$
CLOSE #1
PRINT "[" + L$ + "]"
