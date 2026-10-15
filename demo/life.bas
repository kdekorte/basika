' ============================================================================
'  LIFE - John Conway's Game of Life on a 160 x 120 grid
' ----------------------------------------------------------------------------
'  Each generation a live cell with two or three live neighbors lives on, a
'  dead cell with exactly three comes alive, and every other cell dies or
'  stays dead. Cells past the edges count as dead. The grid starts with each
'  cell alive by a coin toss and steps ten times a second, as in
'  RandomThings/html/life.html (which uses 75 x 75).
'
'  A new cell starts faint and grows solid over its first generations; a
'  cell that dies leaves a violet glow that fades out.
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

CONST SW = 816
CONST SH = 640
CONST COLS = 160 ' cells across
CONST ROWS = 120 ' cells down
CONST CELL = 5 ' pixels per cell, including its grid line
CONST GX = (SW - COLS * CELL - 1) \ 2 ' top left of the grid
CONST GY = 8
CONST STEP_TIME = .1 ' seconds per generation
CONST FADE_IN = 6 ' generations a new cell takes to become solid
CONST GLOW = 5 ' generations a dead cell's glow lasts

' Two generations, with a ring of always-dead cells around each so that
' counting neighbors needs no edge checks. Cur is the one shown. Age is how
' many generations a live cell has lived (up to FADE_IN), Ember how many
' generations of glow a dead cell has left.
DIM SHARED Grid(1, 0 TO ROWS + 1, 0 TO COLS + 1) AS INTEGER, Cur
DIM SHARED Age(ROWS, COLS) AS INTEGER, Ember(ROWS, COLS) AS INTEGER
DIM SHARED Generation, Alive, Paused, QuitApp, SelfTest
DIM SHARED LiveCol(1 TO FADE_IN), GlowCol(1 TO GLOW)
DIM SHARED cBack, cLine, cText
DIM frames, lastStep AS DOUBLE

SCREEN _NEWIMAGE(SW, SH, 32)
_TITLE "Life"
_AUTODISPLAY OFF
cBack = _RGB32(255, 255, 255)
cLine = _RGB32(232, 234, 238) ' barely darker than the background
cText = _RGB32(70, 76, 90)
MakeColors

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

' ------------------------------------------------------------------- colors

' Over a white background a color drawn at opacity a is that much of the way
' from white to the color, so the fades are worked out once here.
SUB MakeColors
    DIM i, a AS SINGLE
    FOR i = 1 TO FADE_IN ' from 30% opaque at birth to solid
        a = .3 + .7 * (i - 1) / (FADE_IN - 1)
        LiveCol(i) = OverWhite(30, 110, 220, a)
    NEXT
    FOR i = 1 TO GLOW ' Ember = GLOW just after death, 1 just before it is gone
        a = .45 * i / GLOW
        GlowCol(i) = OverWhite(150, 60, 220, a)
    NEXT
END SUB

FUNCTION OverWhite (r, g, b, a AS SINGLE)
    OverWhite = _RGB32(255 + (r - 255) * a, 255 + (g - 255) * a, 255 + (b - 255) * a)
END FUNCTION

' ------------------------------------------------------------------- the grid

' Fills the grid at random (random = -1) or clears it, and redraws it all.
SUB Seed (random)
    DIM r, c, i
    LINE (0, 0)-(SW - 1, SH - 1), cBack, BF
    FOR i = 0 TO ROWS
        LINE (GX, GY + i * CELL)-(GX + COLS * CELL, GY + i * CELL), cLine
    NEXT
    FOR i = 0 TO COLS
        LINE (GX + i * CELL, GY)-(GX + i * CELL, GY + ROWS * CELL), cLine
    NEXT
    Cur = 0: Generation = 0: Alive = 0
    FOR r = 0 TO ROWS + 1
        FOR c = 0 TO COLS + 1
            Grid(0, r, c) = 0: Grid(1, r, c) = 0
            IF r <= ROWS AND c <= COLS THEN Age(r, c) = 0: Ember(r, c) = 0
        NEXT
    NEXT
    IF random THEN
        FOR r = 1 TO ROWS
            FOR c = 1 TO COLS
                IF RND >= .5 THEN SetCell r, c, 1
            NEXT
        NEXT
    END IF
END SUB

' Sets one cell of the shown generation and draws it: a drawn cell is born,
' an erased one starts to glow.
SUB SetCell (r, c, v)
    IF Grid(Cur, r, c) = v THEN EXIT SUB
    Grid(Cur, r, c) = v
    IF v THEN
        Alive = Alive + 1
        Age(r, c) = 1: Ember(r, c) = 0
    ELSE
        Alive = Alive - 1
        Age(r, c) = 0: Ember(r, c) = GLOW
    END IF
    DrawCell r, c
END SUB

SUB DrawCell (r, c)
    DIM x, y, col
    x = GX + (c - 1) * CELL + 1
    y = GY + (r - 1) * CELL + 1
    IF Age(r, c) THEN
        col = LiveCol(Age(r, c))
    ELSEIF Ember(r, c) THEN
        col = GlowCol(Ember(r, c))
    ELSE
        col = cBack
    END IF
    LINE (x, y)-(x + CELL - 2, y + CELL - 2), col, BF
END SUB

' Works out the next generation, ages the live cells, fades the glow of the
' dead ones, and draws only the cells whose look changed.
SUB StepLife
    DIM r, c, count, v, nx
    nx = 1 - Cur
    FOR r = 1 TO ROWS
        FOR c = 1 TO COLS
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
            IF v THEN
                IF Grid(Cur, r, c) = 0 THEN
                    Alive = Alive + 1
                    Age(r, c) = 1: Ember(r, c) = 0
                    DrawCell r, c
                ELSEIF Age(r, c) < FADE_IN THEN
                    Age(r, c) = Age(r, c) + 1
                    DrawCell r, c
                END IF
            ELSEIF Grid(Cur, r, c) THEN
                Alive = Alive - 1
                Age(r, c) = 0: Ember(r, c) = GLOW
                DrawCell r, c
            ELSEIF Ember(r, c) THEN
                Ember(r, c) = Ember(r, c) - 1
                DrawCell r, c
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
        IF _MOUSEX >= GX AND _MOUSEY >= GY AND r <= ROWS AND c <= COLS THEN
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
    COLOR cText, cBack
    _PRINTSTRING (GX, SH - 18), s
END SUB
