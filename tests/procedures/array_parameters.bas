' Arrays passed as name() are shared with the procedure; LBOUND and UBOUND
' report their bounds.
DIM a(3), n$(2), g(1 TO 2, 1 TO 3) AS INTEGER
a(2) = 7: n$(1) = "hi": g(2, 3) = 9
Show a(), n$()
PRINT "after:"; a(1); Total(a())
Grid g()
CALL Show(a(), n$())
PRINT a(1)
ON ERROR GOTO Trap
PRINT UBOUND(g, 3)
PRINT UBOUND(never)
END
Trap:
PRINT "error"; ERR
RESUME NEXT
SUB Show (arr(), s$())
  PRINT arr(2); s$(1); UBOUND(arr); UBOUND(s$)
  arr(1) = 5
END SUB
FUNCTION Total (v())
  t = 0
  FOR i = LBOUND(v) TO UBOUND(v): t = t + v(i): NEXT
  Total = t
END FUNCTION
SUB Grid (m() AS INTEGER)
  PRINT LBOUND(m, 2); UBOUND(m, 2); m(2, 3)
END SUB
