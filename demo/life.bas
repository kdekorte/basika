' ============================================================================
'  LIFE - John Conway's Game of Life on a 75 x 75 grid
' ----------------------------------------------------------------------------
'  Each generation a live cell with two or three live neighbors lives on, a
'  dead cell with exactly three comes alive, and every other cell dies or
'  stays dead. Cells past the edges count as dead. The grid starts with each
'  cell alive by a coin toss and steps ten times a second, as in
'  RandomThings/html/life.html.
'
'  Keys    Space pause / resume   N one step (while paused)   R new random
'          grid   C clear   Esc quit
'  Mouse   left button draws live cells, right button erases them
'
'  Run with: basika -w demo/life.bas
'  (basika --headless demo/life.bas --selftest [generations] seeds the grid
'   the same way every time, runs that many generations, 50 by default,
'   saves life_selftest.png and exits.)
' ============================================================================

DEFLNG A-Z

CONST SW = 640
CONST SH = 480
CONST N = 75 ' cells across and down
CONST CELL = 6 ' pixels per cell, including its grid line
CONST GX = (SW - N * CELL - 1) \ 2 ' top left of the grid
CONST GY = 8
CONST STEP_TIME = .1 ' seconds per generation

' Two generations, with a ring of always-dead cells around each so that
' counting neighbors needs no edge checks. Cur is the one shown.
DIM SHARED Grid(1, 0 TO N + 1, 0 TO N + 1) AS INTEGER, Cur
DIM SHARED Generation, Alive, Paused, QuitApp, SelfTest
DIM SHARED cLive, cDead, cLine, cText
DIM frames, lastStep AS DOUBLE

SCREEN _NEWIMAGE(SW, SH, 32)
_TITLE "Life"
_AUTODISPLAY OFF
cLive = _RGB32(70, 130, 180) ' steel blue
cDead = _RGB32(255, 255, 255)
cLine = _RGB32(128, 128, 128)
cText = _RGB32(40, 40, 40)

IF LEFT$(COMMAND$, 10) = "--selftest" THEN
    SelfTest = -1
    frames = VAL(MID$(COMMAND$, 11))
    IF frames <= 0 THEN frames = 50
    RANDOMIZE -1 ' the same grid every run
ELSE
    RANDOMIZE TIMER
END IF

Seed -1
lastStep = TIMER
DO
    IF SelfTest THEN
        StepLife
        IF Generation >= frames THEN DrawStatus: _DISPLAY: SCREENSHOT "life_selftest.png": SYSTEM
    ELSE
        ReadInput
        IF NOT Paused AND ABS(TIMER - lastStep) >= STEP_TIME THEN
            lastStep = TIMER
            StepLife
        END IF
        DrawStatus
        _DISPLAY
        _LIMIT 60
    END IF
LOOP UNTIL QuitApp
SYSTEM

' ------------------------------------------------------------------- the grid

' Fills the grid at random (random = -1) or clears it, and redraws it all.
SUB Seed (random)
    DIM r, c, i
    LINE (0, 0)-(SW - 1, SH - 1), cDead, BF
    FOR i = 0 TO N
        LINE (GX, GY + i * CELL)-(GX + N * CELL, GY + i * CELL), cLine
        LINE (GX + i * CELL, GY)-(GX + i * CELL, GY + N * CELL), cLine
    NEXT
    Cur = 0: Generation = 0: Alive = 0
    FOR r = 0 TO N + 1
        FOR c = 0 TO N + 1
            Grid(0, r, c) = 0: Grid(1, r, c) = 0
        NEXT
    NEXT
    IF random THEN
        FOR r = 1 TO N
            FOR c = 1 TO N
                IF RND >= .5 THEN SetCell r, c, 1
            NEXT
        NEXT
    END IF
END SUB

' Sets one cell of the shown generation and draws it.
SUB SetCell (r, c, v)
    IF Grid(Cur, r, c) = v THEN EXIT SUB
    Grid(Cur, r, c) = v
    IF v THEN Alive = Alive + 1 ELSE Alive = Alive - 1
    DrawCell r, c, v
END SUB

SUB DrawCell (r, c, v)
    DIM x, y
    x = GX + (c - 1) * CELL + 1
    y = GY + (r - 1) * CELL + 1
    IF v THEN
        LINE (x, y)-(x + CELL - 2, y + CELL - 2), cLive, BF
    ELSE
        LINE (x, y)-(x + CELL - 2, y + CELL - 2), cDead, BF
    END IF
END SUB

' Works out the next generation and draws only the cells that changed.
SUB StepLife
    DIM r, c, count, v, nx
    nx = 1 - Cur
    FOR r = 1 TO N
        FOR c = 1 TO N
            count = Grid(Cur, r - 1, c - 1) + Grid(Cur, r - 1, c) + Grid(Cur, r - 1, c + 1)
            count = count + Grid(Cur, r, c - 1) + Grid(Cur, r, c + 1)
            count = count + Grid(Cur, r + 1, c - 1) + Grid(Cur, r + 1, c) + Grid(Cur, r + 1, c + 1)
            IF count = 3 THEN
                v = 1
            ELSEIF count = 2 THEN
                v = Grid(Cur, r, c)
            ELSE
                v = 0
            END IF
            Grid(nx, r, c) = v
            IF v <> Grid(Cur, r, c) THEN
                IF v THEN Alive = Alive + 1 ELSE Alive = Alive - 1
                DrawCell r, c, v
            END IF
        NEXT
    NEXT
    Cur = nx
    Generation = Generation + 1
END SUB

' ---------------------------------------------------------- input and status

SUB ReadInput
    DIM k, r, c
    WHILE _MOUSEINPUT: WEND
    IF _MOUSEBUTTON(1) OR _MOUSEBUTTON(2) THEN
        c = (_MOUSEX - GX) \ CELL + 1
        r = (_MOUSEY - GY) \ CELL + 1
        IF _MOUSEX >= GX AND _MOUSEY >= GY AND r <= N AND c <= N THEN
            IF _MOUSEBUTTON(1) THEN SetCell r, c, 1 ELSE SetCell r, c, 0
        END IF
    END IF
    DO
        k = _KEYHIT
        SELECT CASE k
            CASE 32: Paused = NOT Paused
            CASE ASC("n"), ASC("N"): IF Paused THEN StepLife
            CASE ASC("r"), ASC("R"): Seed -1
            CASE ASC("c"), ASC("C"): Seed 0
            CASE 27: QuitApp = -1
        END SELECT
    LOOP WHILE k
END SUB

SUB DrawStatus
    DIM s AS STRING
    s = "Gen" + STR$(Generation) + "  Alive" + STR$(Alive)
    IF Paused THEN s = s + "  PAUSED"
    s = LEFT$(s + SPACE$(30), 30) + "Space pause N step R random C clear"
    COLOR cText, cDead
    _PRINTSTRING (GX, SH - 18), s
END SUB
