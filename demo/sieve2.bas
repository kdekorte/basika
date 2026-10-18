' Modern Structured BASIC Version
Dim N As Long = 1000000
Dim Flags(N) As _Byte
Dim Count As Long = 0
Dim StartTime As Double = Timer

Print "Sieving up to "; N; "..."

' Core Sieve Algorithm
For I As Long = 2 To Sqr(N)
    If Flags(I) = 0 Then
        For J As Long = I * I To N Step I
            Flags(J) = 1
        Next J
    End If
Next I

' Count Primes
For I As Long = 2 To N
    If Flags(I) = 0 Then Count = Count + 1
Next I

Dim EndTime As Double = Timer
Print "Execution Time: "; EndTime - StartTime; " seconds"
Print "Primes found: "; Count
