' WRITE quotes strings and separates items with commas; LINE INPUT reads a
' whole line; INPUT honors quoted fields; INPUT$ reads raw characters.
F$ = "tests/line_input_write.tmp"
OPEN F$ FOR OUTPUT AS #1
WRITE #1, "a,b", 42, -1.5
WRITE #1, "plain"
PRINT #1, "  spaced, line  "
CLOSE #1
OPEN F$ FOR INPUT AS #1
LINE INPUT #1, first$
PRINT "["; first$; "]"
INPUT #1, word$
PRINT "["; word$; "]"
LINE INPUT #1, raw$
PRINT "["; raw$; "]"
CLOSE #1
OPEN F$ FOR INPUT AS #1
INPUT #1, a$, n, x
PRINT "["; a$; "]"; n; x
chunk$ = INPUT$(4, #1)
PRINT "["; chunk$; "]"
CLOSE #1
WRITE 1, "two", 3
WIDTH 80
CLEAR
PRINT "cleared"; n; "["; a$; "]"
KILL "tests/line_input_write.tmp"
