' CHAIN runs another program; COMMON variables pass to it by position and
' open files stay open.
COMMON score, player$, levels()
DIM levels(3)
score = 1200: player$ = "Ada": levels(2) = 7
notcommon = 5
OPEN "tests/chain_common.tmp" FOR OUTPUT AS #1
PRINT #1, "written before CHAIN"
PRINT "first program: chaining"
CHAIN "tests/procedures/chain_common_next"
PRINT "not reached"
