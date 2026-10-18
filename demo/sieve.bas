10 REM === Sieve of Eratosthenes Benchmark ===
20 CLS
30 N = 1000000          :REM Upper limit for prime search
40 DIM FLAGS(N)   :REM Array to hold composite flags (0=Prime, 1=Composite)
50 COUNT = 0
60 PRINT "Starting benchmark for N ="; N
70 T1 = TIMER        :REM Record start time

80 FOR I = 2 TO N
90   FLAGS(I) = 0     :REM Initialize all numbers as prime
100 NEXT I

110 FOR I = 2 TO SQR(N)
120   IF FLAGS(I) = 1 THEN GOTO 160  :REM Skip if already marked
130   FOR J = I * I TO N STEP I
140     FLAGS(J) = 1  :REM Mark multiples as composite
150   NEXT J
160 NEXT I

170 REM Count the primes found
180 FOR I = 2 TO N
190   IF FLAGS(I) = 0 THEN COUNT = COUNT + 1
200 NEXT I

210 T2 = TIMER        :REM Record end time
220 PRINT "Benchmark Finished!"
230 PRINT "Primes found:"; COUNT
240 PRINT "Run Time: "; T2 - T1
