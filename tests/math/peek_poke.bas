10 REM Peek, Poke, and Varptr Edge Cases Test
20 POKE 0, 1
30 POKE 65535, 255
40 PRINT PEEK(0); PEEK(65535)
50 A = 100
60 POKE 100, A
70 B = PEEK(100)
80 PRINT B
90 V = VARPTR(A)
100 IF V >= 61440 THEN PRINT 1 ELSE PRINT 0
110 PRINT PEEK(1000)
120 REM Edge Case: Poke negative address (should not crash/affect memory)
130 POKE -1, 42
140 PRINT PEEK(-1)
150 REM Edge Case: Poke address >= 65536 (should not crash/affect memory)
160 POKE 65536, 42
170 PRINT PEEK(65536)
180 REM Edge Case: Poke out-of-range value (negative value and > 255)
190 POKE 200, -5
200 POKE 201, 1000
210 PRINT PEEK(200); PEEK(201)
225 REM Edge Case: VARPTR on scalar variable types
230 A% = 42
240 A$ = "test"
250 PRINT VARPTR(A%) >= 61440; VARPTR(A$) >= 61440
260 REM Edge Case: VARPTR on array elements
270 DIM B(10)
280 B(5) = 99
290 PRINT VARPTR(B(5)) >= 61440
300 REM Edge Case: VARPTR on undefined variable
310 PRINT VARPTR(UNDEFINED_VAR)
320 END