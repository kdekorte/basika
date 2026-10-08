' NEXT j, i closes several FOR loops; statements after NEXT on its line run;
' a loop that runs zero times skips to the NEXT that closes it.
FOR i = 1 TO 2
FOR j = 1 TO 2
PRINT i; j;
NEXT j, i
PRINT "| done": PRINT "same line after NEXT:";
FOR k = 1 TO 3: PRINT k;: NEXT k: PRINT " after"
FOR a = 1 TO 2: FOR b = 5 TO 1: PRINT "never": NEXT b, a: PRINT "skip inner, a ="; a
FOR a = 3 TO 1: FOR b = 1 TO 2: PRINT "never": NEXT b, a: PRINT "skip outer, a ="; a
FOR i = 1 TO 2: FOR j = 1 TO 2: NEXT: NEXT: PRINT "bare NEXTs"; i; j
Inner
SUB Inner
    FOR x = 1 TO 2
        FOR y = 1 TO 2
            PRINT x * 10 + y;
    NEXT y, x
    FOR x = 4 TO 1: FOR y = 1 TO 2: NEXT y, x: PRINT "| sub skip ok"
END SUB
