' Modern Structured BASIC Version
DIM N AS LONG
DIM Flags(1000000) AS _BYTE
DIM Count AS LONG
DIM StartTime AS DOUBLE
DIM EndTime AS DOUBLE
DIM I AS LONG, J AS LONG

N = 1000000
Count = 0
StartTime = TIMER

PRINT "Sieving up to "; N; "..."

' Core Sieve Algorithm
FOR I = 2 TO SQR(N)
    IF Flags(I) = 0 THEN
        FOR J = I * I TO N STEP I
            Flags(J) = 1
        NEXT J
    END IF
NEXT I

' Count Primes
FOR I = 2 TO N
    IF Flags(I) = 0 THEN Count = Count + 1
NEXT I

EndTime = TIMER
PRINT "Execution Time: "; EndTime - StartTime; " seconds"
PRINT "Primes found: "; Count
