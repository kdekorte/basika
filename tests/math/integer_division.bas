' \ and MOD round their operands to whole numbers first, as in QBasic, and
' division by zero is an error rather than a silent result.
PRINT 7 \ 2; -7 \ 2; 5.9 \ 2; 7 MOD 3; -7 MOD 3; 7.6 MOD 2; 10 / 4
ON ERROR GOTO Trap
PRINT 1 / 0
X = 5 MOD 0
PRINT "X still"; X
Y = 9 \ 0.4
END
Trap:
PRINT "error"; ERR
RESUME NEXT
