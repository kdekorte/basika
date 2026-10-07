' Second half of chain_common.bas: receives the COMMON values by position.
COMMON points, who$, stages()
PRINT "second program:"; points; who$; stages(2); notcommon
PRINT #1, "written after CHAIN"
CLOSE #1
OPEN "tests/chain_common.tmp" FOR INPUT AS #1
LINE INPUT #1, a$: LINE INPUT #1, b$
PRINT a$; " / "; b$
CLOSE #1
KILL "tests/chain_common.tmp"
