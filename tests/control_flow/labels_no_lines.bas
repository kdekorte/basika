PRINT "Start"
A = 1
GOTO my_label
PRINT "Skipped"

my_label:
PRINT "At my_label"
GOSUB my_sub
PRINT "Back from sub"
IF A = 2 THEN GOTO final_label
PRINT "Should not reach here"

my_sub:
PRINT "Inside sub"
A = 2
RETURN

final_label:
PRINT "Done"
