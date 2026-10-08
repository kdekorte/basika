' OPTION _EXPLICIT: a variable never declared in its scope stops the program
' before it runs. M is declared in the module but not SHARED with the SUB.
OPTION _EXPLICIT
DIM m
PRINT "never printed"
Show
SUB Show
    PRINT m
END SUB
