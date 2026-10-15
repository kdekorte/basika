' Ten nested FOR loops: every combination runs once, in order, with the
' innermost loop changing fastest, and each variable ends one step past
' its limit. Then ten loops closed by one NEXT list inside a SUB, ten
' loops with mixed bounds and STEPs, an innermost loop that runs zero
' times, and EXIT FOR from the tenth level.
DEFINT A-Z
DECLARE SUB NextList (count AS INTEGER)
DECLARE SUB Show (label AS STRING, values AS STRING)

count = 0: ordered = -1: prev = -1: total& = 0
FOR a = 0 TO 1
  FOR b = 0 TO 1
    FOR c = 0 TO 1
      FOR d = 0 TO 1
        FOR e = 0 TO 1
          FOR f = 0 TO 1
            FOR g = 0 TO 1
              FOR h = 0 TO 1
                FOR i = 0 TO 1
                  FOR j = 0 TO 1
                    v = a * 512 + b * 256 + c * 128 + d * 64 + e * 32 + f * 16 + g * 8 + h * 4 + i * 2 + j
                    IF v <> prev + 1 THEN ordered = 0
                    prev = v
                    total& = total& + v
                    count = count + 1
                  NEXT j
                NEXT i
              NEXT h
            NEXT g
          NEXT f
        NEXT e
      NEXT d
    NEXT c
  NEXT b
NEXT a
Show "COUNT", STR$(count)
Show "ORDERED", STR$(ordered)
Show "TOTAL", STR$(total&)
Show "LAST", STR$(prev)
Show "AFTER", STR$(a) + "," + STR$(b) + "," + STR$(c) + "," + STR$(d) + "," + STR$(e) + "," + STR$(f) + "," + STR$(g) + "," + STR$(h) + "," + STR$(i) + "," + STR$(j)

n = 0
NextList n
Show "NEXT LIST", STR$(n)

' Trip counts 3, 2, 1, 2, 1, 3, 1, 2, 1, 2: 144 runs in all.
count = 0: total& = 0
FOR a = 3 TO 1 STEP -1
  FOR b = 0 TO 2 STEP 2
    FOR c = 5 TO 5
      FOR d = -1 TO 0
        FOR e = 10 TO 1 STEP -10
          FOR f = 1 TO 5 STEP 2
            FOR g = 7 TO 7 STEP -1
              FOR h = 2 TO 1 STEP -1
                FOR i = 0 TO 0
                  FOR j = 4 TO 6 STEP 2
                    total& = total& + a + b + c + d + e + f + g + h + i + j
                    count = count + 1
                  NEXT j
                NEXT i
              NEXT h
            NEXT g
          NEXT f
        NEXT e
      NEXT d
    NEXT c
  NEXT b
NEXT a
Show "STEPS", STR$(count) + "," + STR$(total&)
Show "AFTER", STR$(a) + "," + STR$(b) + "," + STR$(c) + "," + STR$(d) + "," + STR$(e) + "," + STR$(f) + "," + STR$(g) + "," + STR$(h) + "," + STR$(i) + "," + STR$(j)

' The innermost loop runs zero times; the nine around it still run.
inner = 0: outer = 0
FOR a = 1 TO 2: FOR b = 1 TO 2: FOR c = 1 TO 2: FOR d = 1 TO 2: FOR e = 1 TO 2
FOR f = 1 TO 2: FOR g = 1 TO 2: FOR h = 1 TO 2: FOR i = 1 TO 2
  outer = outer + 1
  FOR j = 1 TO 0
    inner = inner + 1
  NEXT j
NEXT i: NEXT h: NEXT g: NEXT f
NEXT e: NEXT d: NEXT c: NEXT b: NEXT a
Show "ZERO TRIP", STR$(outer) + "," + STR$(inner) + "," + STR$(j)

' EXIT FOR from the tenth level leaves only that loop.
inner = 0: tail = 0
FOR a = 1 TO 2
  FOR b = 1 TO 2
    FOR c = 1 TO 2
      FOR d = 1 TO 2
        FOR e = 1 TO 2
          FOR f = 1 TO 2
            FOR g = 1 TO 2
              FOR h = 1 TO 2
                FOR i = 1 TO 2
                  FOR j = 1 TO 5
                    IF j = 3 THEN EXIT FOR
                    inner = inner + 1
                  NEXT j
                  tail = tail + j
                NEXT i
              NEXT h
            NEXT g
          NEXT f
        NEXT e
      NEXT d
    NEXT c
  NEXT b
NEXT a
Show "EXIT FOR", STR$(inner) + "," + STR$(tail) + "," + STR$(a) + "," + STR$(j)
END

SUB NextList (count AS INTEGER)
  DIM a, b, c, d, e, f, g, h, i, j
  FOR a = 1 TO 2: FOR b = 1 TO 2: FOR c = 1 TO 2: FOR d = 1 TO 2: FOR e = 1 TO 2
  FOR f = 1 TO 2: FOR g = 1 TO 2: FOR h = 1 TO 2: FOR i = 1 TO 2: FOR j = 1 TO 2
    count = count + 1
  NEXT j, i, h, g, f, e, d, c, b, a
END SUB

SUB Show (label AS STRING, values AS STRING)
  PRINT label + ":" + values
END SUB
