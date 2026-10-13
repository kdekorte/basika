' ============================================================================
'  BURROW - a digging game in the style of Namco's Dig Dug (1982)
' ----------------------------------------------------------------------------
'  Dig tunnels through four layers of earth and clear each round of its
'  creatures with your air pump: hit one with the harpoon, then pump until
'  it pops. Deeper pops score more.
'    Blobs (purple)   chase you through the tunnels.
'    Drakes (green)   chase you too, and breathe fire along their row.
'  A creature that cannot reach you floats through the earth as a pair of
'  eyes. Dig out from under a rock and it falls, crushing whatever is below;
'  drop two and a mushroom appears for bonus points. The last creature of a
'  round runs for the top left corner of the field.
'
'  Keys    Arrows (or W A S D) dig   Space pump (tap to pump faster)
'          P pause   F fullscreen on/off   M music on/off   Esc title / quit
'  A joystick or gamepad works too: the stick digs, button 1 pumps.
'
'  The sprites and sounds are in assets/, made by tools/make_assets.py.
'  Run with: basika -w demo/burrow/burrow.bas
'  (basika --headless demo/burrow/burrow.bas --selftest [frames] plays the
'   demo for that many frames, 600 by default, saves burrow_selftest.png and
'   exits.)
' ============================================================================

DEFLNG A-Z

CONST TILE = 48 ' pixels; the sprites are drawn at this size
CONST HALF = 24
CONST BIG = 96 ' the pumped-up sprites
CONST COLS = 20
CONST ROWS = 13 ' row 0 is the surface
CONST FIELD_Y = 48 ' screen y of the top of the field
CONST SW = 960 ' COLS * TILE
CONST SH = 712 ' FIELD_Y + ROWS * TILE + 40
CONST DIG_SPEED = 3 ' pixels a frame; it divides TILE so turns line up
CONST MAX_FOES = 8
CONST MAX_ROCKS = 6
CONST MAX_POPS = 8
CONST HARPOON_REACH = 144 ' three tiles

' Cells of assets/sprites.png (TILE pixels, 16 to a row); tools/make_assets.py
' draws them in this order. The digger faces right; up is 6 cells on and
' down 12, each with 4 walking frames and then 2 pumping frames.
CONST S_DIGGER = 0
CONST S_DYING = 18 ' 4 frames
CONST S_LIFE = 22
CONST S_BLOB = 23 ' 4 walking frames
CONST S_BLINK = 27
CONST S_EYES = 28 ' 2 frames
CONST S_DRAKE = 30 ' 4 walking frames
CONST S_WINDUP = 34 ' 2 frames
CONST S_BREATHE = 36
CONST S_FLAME = 37 ' 3 frames
CONST S_FLAME_TIP = 40 ' 3 frames
CONST S_ROCK = 43
CONST S_CRACKED = 44
CONST S_CRUMBLE = 45 ' 3 frames
CONST S_POP = 48 ' 3 frames
CONST S_MUSHROOM = 51
CONST S_FLOWER = 52 ' 2 frames
' Cells of assets/sprites_big.png (BIG pixels): pumped-up creatures, 3 sizes each.
CONST B_BLOB = 0
CONST B_DRAKE = 3

CONST KIND_BLOB = 1
CONST KIND_DRAKE = 2

' Creature states
CONST F_GONE = 0
CONST F_WALK = 1
CONST F_GHOST = 2
CONST F_PUMPED = 3
CONST F_POP = 4
CONST F_CRUSHED = 5
CONST F_WINDUP = 6
CONST F_FIRE = 7

' Rock states
CONST R_GONE = 0
CONST R_REST = 1
CONST R_WOBBLE = 2
CONST R_FALL = 3
CONST R_BREAK = 4

' Game states
CONST G_READY = 1
CONST G_PLAY = 2
CONST G_DYING = 3
CONST G_CLEAR = 4
CONST G_OVER = 5

' Directions: 1 up, 2 right, 3 down, 4 left
DIM SHARED DirX(4), DirY(4)
DirX(2) = 1: DirX(4) = -1: DirY(1) = -1: DirY(3) = 1

' The field: Dug marks tiles dug out, OpenR the passage to the tile on the
' right and OpenD the passage to the tile below. Dist holds the steps from
' the digger along the tunnels (-1 out of reach).
DIM SHARED Dug(COLS - 1, ROWS - 1), OpenR(COLS - 1, ROWS - 1), OpenD(COLS - 1, ROWS - 1)
DIM SHARED Dist(COLS - 1, ROWS - 1), QueueC(COLS * ROWS), QueueR(COLS * ROWS)
DIM SHARED DistFromC, DistFromR, TunnelsChanged

' The digger
DIM SHARED PX, PY, PFace, PMoved, PAnim, PLastC, PLastR, StartC, StartR
DIM SHARED Harp, HarpLen, HarpDir, PumpFoe, PumpClock
' Harp: 0 put away, 1 shooting out, 2 stuck in a creature

' Creatures
DIM SHARED FoeKind(MAX_FOES), FoeState(MAX_FOES), FoeDir(MAX_FOES), FoeFace(MAX_FOES)
DIM SHARED FoeX(MAX_FOES) AS SINGLE, FoeY(MAX_FOES) AS SINGLE
DIM SHARED FoeStage(MAX_FOES), FoeClock(MAX_FOES), FoeRestless(MAX_FOES), FoeCool(MAX_FOES)
DIM SHARED FoeHomeC(MAX_FOES), FoeHomeR(MAX_FOES), FoeRock(MAX_FOES), FoeFlee(MAX_FOES)
DIM SHARED FoeTX(MAX_FOES), FoeTY(MAX_FOES), FoeLast(MAX_FOES) ' the tile it is heading for
DIM SHARED FoeCount

' Rocks
DIM SHARED RockState(MAX_ROCKS), RockC(MAX_ROCKS), RockY(MAX_ROCKS), RockClock(MAX_ROCKS)
DIM SHARED RockKills(MAX_ROCKS), RockCount, RocksDropped

' Points shown where they were scored
DIM SHARED PopX(MAX_POPS), PopY(MAX_POPS), PopClock(MAX_POPS), PopText(MAX_POPS) AS STRING

DIM SHARED Score, HiScore, Lives, RoundNo, NextLife, GameState, StateClock
DIM SHARED BonusClock, Frame AS LONG, Demo, Paused, SelfTest, QuitGame
DIM SHARED FullPref, FullOn, MusicOn, Speed AS SINGLE, GhostSpeed AS SINGLE
DIM SHARED InDir, InFire, InFirePressed, LastFire, InAnyKey
DIM SHARED Sheet, BigSheet, DirtImg, LogoImg, BaseDir AS STRING
DIM SHARED SndWalk, SndStart, SndClear, SndDie, SndOver, SndExtra, SndBonus
DIM SHARED SndHarpoon, SndPump, SndPop, SndFire, SndFall, SndCrush

DIM frames AS LONG

SCREEN _NEWIMAGE(SW, SH, 32)
_TITLE "Burrow"
_PRINTMODE _KEEPBACKGROUND
_AUTODISPLAY OFF

FindBaseDir
LoadAssets
LoadHiScore
FullPref = -1: MusicOn = -1
IF LEFT$(COMMAND$, 10) = "--selftest" THEN
    SelfTest = -1
    frames = VAL(MID$(COMMAND$, 11))
    IF frames <= 0 THEN frames = 600
    RANDOMIZE 7
ELSE
    RANDOMIZE TIMER
END IF

StartDemo
DO
    ReadInput
    IF NOT Paused THEN UpdateGame
    Render
    _DISPLAY
    Frame = Frame + 1
    IF SelfTest THEN
        IF Frame >= frames THEN SCREENSHOT "burrow_selftest.png": SYSTEM
    ELSE
        _LIMIT 60
    END IF
LOOP UNTIL QuitGame
IF FullOn THEN _FULLSCREEN _OFF
SYSTEM

' ------------------------------------------------------------ files and assets

' Assets live next to this program, wherever it is run from.
SUB FindBaseDir
    DIM i AS INTEGER, p$
    p$ = ARGV$(0)
    BaseDir = ""
    FOR i = LEN(p$) TO 1 STEP -1
        IF MID$(p$, i, 1) = "/" OR MID$(p$, i, 1) = "\" THEN BaseDir = LEFT$(p$, i): EXIT FOR
    NEXT
END SUB

FUNCTION LoadSound (file$)
    LoadSound = _SNDOPEN(BaseDir + "assets/" + file$)
END FUNCTION

SUB LoadAssets
    Sheet = _LOADIMAGE(BaseDir + "assets/sprites.png", 32)
    BigSheet = _LOADIMAGE(BaseDir + "assets/sprites_big.png", 32)
    IF Sheet = -1 OR BigSheet = -1 THEN
        PRINT "Cannot load "; BaseDir; "assets/sprites.png"
        PRINT "Run python3 demo/burrow/tools/make_assets.py to make it."
        _DISPLAY
        SLEEP 5
        SYSTEM
    END IF
    DirtImg = _NEWIMAGE(COLS * TILE, ROWS * TILE, 32)
    SndWalk = LoadSound("walk.wav"): SndStart = LoadSound("start.wav")
    SndClear = LoadSound("clear.wav"): SndDie = LoadSound("die.wav")
    SndOver = LoadSound("over.wav"): SndExtra = LoadSound("extra.wav")
    SndBonus = LoadSound("bonus.wav"): SndHarpoon = LoadSound("harpoon.wav")
    SndPump = LoadSound("pump.wav"): SndPop = LoadSound("pop.wav")
    SndFire = LoadSound("fire.wav"): SndFall = LoadSound("fall.wav")
    SndCrush = LoadSound("crush.wav")
    IF SndWalk THEN _SNDVOL SndWalk, .6

    ' The title: the font's letters, blown up.
    LogoImg = _NEWIMAGE(_PRINTWIDTH("BURROW") + 2, 20, 32)
    _DEST LogoImg
    _PRINTMODE _KEEPBACKGROUND
    COLOR _RGB32(255, 214, 70)
    _PRINTSTRING (1, 1), "BURROW"
    _DEST 0
END SUB

' The high score is kept in burrow.hi next to the program.
SUB LoadHiScore
    DIM f AS INTEGER
    f = FREEFILE
    OPEN BaseDir + "burrow.hi" FOR APPEND AS #f
    CLOSE #f
    OPEN BaseDir + "burrow.hi" FOR INPUT AS #f
    IF LOF(f) > 0 THEN INPUT #f, HiScore
    CLOSE #f
END SUB

SUB SaveHiScore
    DIM f AS INTEGER
    IF SelfTest OR Demo THEN EXIT SUB
    f = FREEFILE
    OPEN BaseDir + "burrow.hi" FOR OUTPUT AS #f
    PRINT #f, HiScore
    CLOSE #f
END SUB

' Sounds play in games, not in the demo.
SUB Sfx (h)
    IF h <> 0 AND NOT Demo THEN _SNDPLAYCOPY h
END SUB

SUB Jingle (h)
    IF h <> 0 AND NOT Demo THEN _SNDSTOP h: _SNDPLAY h
END SUB

' The digging tune plays while the digger moves and pauses when it stops.
SUB UpdateMusic
    DIM want AS INTEGER
    IF SndWalk = 0 THEN EXIT SUB
    want = MusicOn AND NOT Demo AND NOT Paused AND GameState = G_PLAY AND PMoved
    IF want AND NOT _SNDPLAYING(SndWalk) THEN _SNDLOOP SndWalk
    IF NOT want AND _SNDPLAYING(SndWalk) THEN _SNDPAUSE SndWalk
END SUB

' ------------------------------------------------------------------- game flow

SUB StartDemo
    Demo = -1
    NewGame
END SUB

SUB StartGame
    Demo = 0
    NewGame
END SUB

SUB NewGame
    Score = 0: Lives = 3: RoundNo = 1: NextLife = 10000
    Paused = 0
    StartRound
    ApplyFullscreen
END SUB

' Games play fullscreen unless F turned it off; the demo stays in a window.
SUB ApplyFullscreen
    DIM want AS INTEGER
    want = FullPref AND NOT Demo AND NOT SelfTest
    IF want <> FullOn THEN
        IF want THEN _FULLSCREEN _SQUAREPIXELS ELSE _FULLSCREEN _OFF
        FullOn = want
    END IF
END SUB

SUB AddScore (points)
    Score = Score + points
    IF Score >= NextLife THEN
        NextLife = NextLife + 20000
        IF Lives < 8 THEN Lives = Lives + 1: Sfx SndExtra
    END IF
    IF Score > HiScore AND NOT Demo AND NOT SelfTest THEN HiScore = Score
END SUB

SUB SetState (s)
    GameState = s
    StateClock = 0
END SUB

SUB UpdateGame
    StateClock = StateClock + 1
    SELECT CASE GameState
        CASE G_READY
            IF StateClock > 120 THEN SetState G_PLAY
        CASE G_PLAY
            MoveDigger
            MoveHarpoon
            MoveFoes
            MoveRocks
            CheckBonus
            CheckDigger
            IF LiveFoes = 0 THEN
                SetState G_CLEAR
                Jingle SndClear
            END IF
        CASE G_DYING
            MoveRocks
            IF StateClock > 150 THEN
                Lives = Lives - 1
                IF Lives <= 0 THEN
                    SetState G_OVER
                    Jingle SndOver
                    SaveHiScore
                ELSE
                    ResetPositions
                    SetState G_READY
                END IF
            END IF
        CASE G_CLEAR
            IF StateClock > 150 THEN
                RoundNo = RoundNo + 1
                StartRound
            END IF
        CASE G_OVER
            IF StateClock > 300 OR (Demo AND StateClock > 60) THEN StartDemo
    END SELECT
    UpdatePops
    UpdateMusic
END SUB

FUNCTION LiveFoes
    DIM i AS INTEGER, n AS INTEGER
    FOR i = 1 TO FoeCount
        IF FoeState(i) <> F_GONE THEN n = n + 1
    NEXT
    LiveFoes = n
END FUNCTION

' ------------------------------------------------------------------- the field

SUB StartRound
    DIM i AS INTEGER, c AS INTEGER, r AS INTEGER, tries AS INTEGER, horizontal AS INTEGER
    DIM ok AS INTEGER, k AS INTEGER
    ' Pixels a frame, for tiles of 32 pixels scaled to TILE.
    Speed = .9 + RoundNo * .08: IF Speed > 1.8 THEN Speed = 1.8
    GhostSpeed = .5 + RoundNo * .04: IF GhostSpeed > 1 THEN GhostSpeed = 1
    Speed = Speed * TILE / 32: GhostSpeed = GhostSpeed * TILE / 32
    FOR c = 0 TO COLS - 1
        FOR r = 0 TO ROWS - 1
            Dug(c, r) = (r = 0): OpenR(c, r) = (r = 0 AND c < COLS - 1): OpenD(c, r) = 0
        NEXT
    NEXT
    DrawEarth

    ' The digger's shaft from the surface down to the middle.
    StartC = COLS \ 2 - 1: StartR = 6
    FOR r = 1 TO StartR
        DigTile StartC, r
        OpenPassage StartC, r - 1, StartC, r
    NEXT

    ' Creatures sit in short tunnels of their own, away from the shaft.
    FoeCount = 4 + (RoundNo - 1) \ 2: IF FoeCount > MAX_FOES THEN FoeCount = MAX_FOES
    FOR i = 1 TO FoeCount
        tries = 0
        DO
            tries = tries + 1
            horizontal = RND < .6
            c = 1 + INT(RND * (COLS - 5)): r = 2 + INT(RND * (ROWS - 4))
            ok = ABS(c - StartC) > 3 OR r > StartR + 2
            FOR k = 0 TO 2
                IF horizontal THEN
                    IF Dug(c + k, r) OR Dug(c + k, r - 1) OR Dug(c + k, r + 1) THEN ok = 0
                ELSE
                    IF r + k > ROWS - 1 THEN ok = 0 ELSE IF Dug(c, r + k) OR Dug(c - 1, r + k) OR Dug(c + 1, r + k) THEN ok = 0
                END IF
            NEXT
        LOOP UNTIL ok OR tries > 200
        FOR k = 0 TO 2
            IF horizontal THEN
                DigTile c + k, r
                IF k > 0 THEN OpenPassage c + k - 1, r, c + k, r
            ELSE
                DigTile c, r + k
                IF k > 0 THEN OpenPassage c, r + k - 1, c, r + k
            END IF
        NEXT
        FoeKind(i) = KIND_BLOB
        IF i MOD 3 = 2 THEN FoeKind(i) = KIND_DRAKE
        FoeHomeC(i) = c + 1 * -horizontal: FoeHomeR(i) = r + 1 * (horizontal + 1)
    NEXT

    ' Rocks rest on solid earth with solid earth below them.
    RockCount = 3 + RoundNo \ 4: IF RockCount > MAX_ROCKS THEN RockCount = MAX_ROCKS
    FOR i = 1 TO RockCount
        tries = 0
        DO
            tries = tries + 1
            c = 1 + INT(RND * (COLS - 2)): r = 2 + INT(RND * (ROWS - 5))
            ok = NOT Dug(c, r) AND NOT Dug(c, r + 1) AND NOT Dug(c, r - 1) AND c <> StartC
            FOR k = 1 TO i - 1
                IF RockC(k) = c AND ABS(RockY(k) \ TILE - r) < 3 THEN ok = 0
            NEXT
        LOOP UNTIL ok OR tries > 200
        RockState(i) = R_REST: RockC(i) = c: RockY(i) = r * TILE: RockClock(i) = 0
    NEXT
    RocksDropped = 0: BonusClock = 0
    FOR i = 1 TO MAX_POPS: PopClock(i) = 0: NEXT
    ResetPositions
    TunnelsChanged = -1
    SetState G_READY
    Jingle SndStart
END SUB

SUB ResetPositions
    DIM i AS INTEGER
    PX = StartC * TILE: PY = StartR * TILE: PFace = 2: PMoved = 0
    PLastC = StartC: PLastR = StartR
    Harp = 0: PumpFoe = 0
    ' After a death the creatures left go back home; a new round brings all.
    FOR i = 1 TO FoeCount
        IF GameState <> G_DYING OR FoeState(i) <> F_GONE THEN
            FoeState(i) = F_WALK
            FoeX(i) = FoeHomeC(i) * TILE: FoeY(i) = FoeHomeR(i) * TILE
            FoeDir(i) = 0: FoeLast(i) = 0: FoeFace(i) = 1: FoeStage(i) = 0: FoeClock(i) = 0
            FoeRestless(i) = INT(RND * 200): FoeCool(i) = 120: FoeFlee(i) = 0
        END IF
    NEXT
END SUB

' The layers of earth, with flecks, under a strip of sky.
SUB DrawEarth
    DIM r AS INTEGER, i AS INTEGER, x AS INTEGER, y AS INTEGER, layer AS INTEGER
    DIM c AS _UNSIGNED LONG, fleck AS _UNSIGNED LONG
    _DEST DirtImg
    FOR y = 0 TO TILE - 1
        LINE (0, y)-(COLS * TILE - 1, y), _RGB32(40 + y * 2, 90 + y * 3, 200 - y)
    NEXT
    FOR r = 1 TO ROWS - 1
        layer = (r - 1) \ 3
        SELECT CASE layer
            CASE 0: c = _RGB32(226, 176, 72): fleck = _RGB32(190, 136, 48)
            CASE 1: c = _RGB32(208, 124, 46): fleck = _RGB32(168, 90, 30)
            CASE 2: c = _RGB32(176, 80, 40): fleck = _RGB32(130, 52, 28)
            CASE ELSE: c = _RGB32(122, 48, 46): fleck = _RGB32(86, 28, 32)
        END SELECT
        LINE (0, r * TILE)-(COLS * TILE - 1, r * TILE + TILE - 1), c, BF
        FOR i = 1 TO 160
            x = INT(RND * COLS * TILE): y = r * TILE + INT(RND * TILE)
            LINE (x, y)-(x + 1, y + 1), fleck, BF
        NEXT
    NEXT
    _DEST 0
END SUB

SUB DigTile (c, r)
    IF c < 0 OR c >= COLS OR r < 1 OR r >= ROWS THEN EXIT SUB
    IF NOT Dug(c, r) THEN
        Dug(c, r) = -1
        TunnelsChanged = -1
        _DEST DirtImg
        LINE (c * TILE, r * TILE)-(c * TILE + TILE - 1, r * TILE + TILE - 1), _RGB32(16, 10, 12), BF
        _DEST 0
    END IF
END SUB

SUB OpenPassage (c1, r1, c2, r2)
    IF c2 = c1 + 1 THEN OpenR(c1, r1) = -1
    IF c2 = c1 - 1 THEN OpenR(c2, r1) = -1
    IF r2 = r1 + 1 THEN OpenD(c1, r1) = -1
    IF r2 = r1 - 1 THEN OpenD(c1, r2) = -1
    TunnelsChanged = -1
END SUB

' Whether the passage from tile (c, r) in direction d is open.
FUNCTION CanPass (c, r, d)
    CanPass = 0
    SELECT CASE d
        CASE 1: IF r > 0 THEN CanPass = OpenD(c, r - 1)
        CASE 2: IF c < COLS - 1 THEN CanPass = OpenR(c, r)
        CASE 3: IF r < ROWS - 1 THEN CanPass = OpenD(c, r)
        CASE 4: IF c > 0 THEN CanPass = OpenR(c - 1, r)
    END SELECT
END FUNCTION

FUNCTION RockAt (c, r)
    DIM i AS INTEGER
    RockAt = 0
    FOR i = 1 TO RockCount
        IF RockState(i) = R_REST OR RockState(i) = R_WOBBLE THEN
            IF RockC(i) = c AND RockY(i) \ TILE = r THEN RockAt = i
        END IF
    NEXT
END FUNCTION

' Steps along the tunnels from (fc, fr) to every tile, for the creatures.
SUB MapTunnels (fc, fr)
    DIM head AS INTEGER, tail AS INTEGER, c AS INTEGER, r AS INTEGER, d AS INTEGER
    DIM nc AS INTEGER, nr AS INTEGER
    FOR c = 0 TO COLS - 1
        FOR r = 0 TO ROWS - 1
            Dist(c, r) = -1
        NEXT
    NEXT
    Dist(fc, fr) = 0
    QueueC(0) = fc: QueueR(0) = fr: tail = 1
    DO WHILE head < tail
        c = QueueC(head): r = QueueR(head): head = head + 1
        FOR d = 1 TO 4
            IF CanPass(c, r, d) THEN
                nc = c + DirX(d): nr = r + DirY(d)
                IF Dist(nc, nr) < 0 THEN
                    Dist(nc, nr) = Dist(c, r) + 1
                    QueueC(tail) = nc: QueueR(tail) = nr: tail = tail + 1
                END IF
            END IF
        NEXT
    LOOP
    DistFromC = fc: DistFromR = fr: TunnelsChanged = 0
END SUB

' ------------------------------------------------------------------- the digger

SUB MoveDigger
    DIM want AS INTEGER, d AS INTEGER, nx AS INTEGER, ny AS INTEGER, c AS INTEGER, r AS INTEGER
    PMoved = 0
    IF Harp = 1 THEN EXIT SUB ' the harpoon is on its way
    want = InDir
    IF want = 0 THEN EXIT SUB
    IF Harp = 2 THEN Harp = 0: PumpFoe = 0 ' moving lets go of the pump

    ' Turns happen on the grid: finish the current tile first.
    d = want
    IF (want = 1 OR want = 3) AND PX MOD TILE <> 0 THEN
        IF PFace = 2 OR PFace = 4 THEN d = PFace ELSE d = 2
    ELSEIF (want = 2 OR want = 4) AND PY MOD TILE <> 0 THEN
        IF PFace = 1 OR PFace = 3 THEN d = PFace ELSE d = 3
    END IF
    nx = PX + DirX(d) * DIG_SPEED: ny = PY + DirY(d) * DIG_SPEED
    IF nx < 0 OR nx > (COLS - 1) * TILE OR ny < 0 OR ny > (ROWS - 1) * TILE THEN
        PFace = d
        EXIT SUB
    END IF
    ' Rocks are in the way.
    IF d = 2 THEN c = (nx + TILE - 1) \ TILE ELSE c = nx \ TILE
    IF d = 3 THEN r = (ny + TILE - 1) \ TILE ELSE r = ny \ TILE
    IF RockAt(c, r) THEN PFace = d: EXIT SUB
    PX = nx: PY = ny: PFace = d: PMoved = -1
    PAnim = PAnim + 1
    IF PY >= TILE THEN
        _DEST DirtImg
        LINE (PX, PY)-(PX + TILE - 1, PY + TILE - 1), _RGB32(16, 10, 12), BF
        _DEST 0
    END IF
    IF PX MOD TILE = 0 AND PY MOD TILE = 0 THEN
        c = PX \ TILE: r = PY \ TILE
        IF c <> PLastC OR r <> PLastR THEN
            IF NOT Dug(c, r) THEN AddScore 10
            Dug(c, r) = -1
            OpenPassage PLastC, PLastR, c, r
            PLastC = c: PLastR = r
        END IF
    END IF
END SUB

SUB MoveHarpoon
    DIM tx AS INTEGER, ty AS INTEGER, c AS INTEGER, r AS INTEGER, i AS INTEGER
    IF Harp = 0 THEN
        IF InFirePressed THEN
            Harp = 1: HarpLen = 12: HarpDir = PFace
            Sfx SndHarpoon
        END IF
        EXIT SUB
    END IF
    IF Harp = 1 THEN
        HarpLen = HarpLen + 12
        tx = PX + HALF + DirX(HarpDir) * (18 + HarpLen): ty = PY + HALF + DirY(HarpDir) * (18 + HarpLen)
        ' It goes through tunnels only.
        c = tx \ TILE: r = ty \ TILE
        IF tx < 0 OR ty < 0 OR c >= COLS OR r >= ROWS THEN Harp = 0: EXIT SUB
        IF NOT Dug(c, r) AND NOT (c = (PX + HALF) \ TILE AND r = (PY + HALF) \ TILE) THEN Harp = 0: EXIT SUB
        FOR i = 1 TO FoeCount
            IF Pumpable(i) THEN
                IF ABS(FoeX(i) + HALF - tx) < HALF AND ABS(FoeY(i) + HALF - ty) < HALF THEN
                    Harp = 2: PumpFoe = i: PumpClock = 0
                    FoeState(i) = F_PUMPED: FoeStage(i) = 1: FoeClock(i) = 0
                    Sfx SndPump
                    EXIT SUB
                END IF
            END IF
        NEXT
        IF HarpLen >= HARPOON_REACH THEN Harp = 0
        EXIT SUB
    END IF
    ' Pumping: each press, or a steady hold, blows the creature up more.
    i = PumpFoe
    IF FoeState(i) <> F_PUMPED THEN Harp = 0: PumpFoe = 0: EXIT SUB
    PumpClock = PumpClock + 1
    IF InFirePressed OR (InFire AND PumpClock MOD 20 = 0) THEN
        FoeStage(i) = FoeStage(i) + 1: FoeClock(i) = 0
        Sfx SndPump
        IF FoeStage(i) >= 4 THEN PopFoe i
    END IF
END SUB

FUNCTION Pumpable (i)
    SELECT CASE FoeState(i)
        CASE F_WALK, F_WINDUP, F_FIRE: Pumpable = -1
        CASE F_GHOST: Pumpable = FoeFlee(i)
        CASE F_PUMPED: Pumpable = -1
        CASE ELSE: Pumpable = 0
    END SELECT
END FUNCTION

SUB PopFoe (i)
    DIM points AS LONG, r AS INTEGER
    r = (FoeY(i) + HALF) \ TILE
    points = 200 + 100 * ((r - 1) \ 3)
    IF r <= 0 THEN points = 200
    IF FoeKind(i) = KIND_DRAKE AND (HarpDir = 2 OR HarpDir = 4) THEN points = points * 2
    AddScore points
    ShowPoints FoeX(i), FoeY(i), points
    FoeState(i) = F_POP: FoeClock(i) = 0
    Harp = 0: PumpFoe = 0
    Sfx SndPop
END SUB

SUB CheckDigger
    DIM i AS INTEGER, fx AS INTEGER, reach AS INTEGER
    FOR i = 1 TO FoeCount
        SELECT CASE FoeState(i)
            CASE F_WALK, F_WINDUP, F_FIRE
                IF ABS(FoeX(i) - PX) < TILE * 2 \ 3 AND ABS(FoeY(i) - PY) < TILE * 2 \ 3 THEN KillDigger: EXIT SUB
        END SELECT
        IF FoeState(i) = F_FIRE THEN
            ' The flame reaches up to three tiles, through the earth too.
            reach = FlameReach(i)
            IF ABS(FoeY(i) - PY) < TILE * 5 \ 8 THEN
                IF FoeFace(i) > 0 THEN fx = FoeX(i) + TILE ELSE fx = FoeX(i) - reach
                IF PX + TILE - 9 > fx AND PX + 9 < fx + reach THEN KillDigger: EXIT SUB
            END IF
        END IF
    NEXT
    IF BonusClock > 0 THEN
        IF ABS(PX - StartC * TILE) < TILE * 5 \ 8 AND ABS(PY - StartR * TILE) < TILE * 5 \ 8 THEN
            AddScore 400 + 200 * RoundNo
            ShowPoints StartC * TILE, StartR * TILE, 400 + 200 * RoundNo
            BonusClock = 0
            Sfx SndBonus
        END IF
    END IF
END SUB

SUB KillDigger
    IF GameState <> G_PLAY THEN EXIT SUB
    SetState G_DYING
    Harp = 0: PumpFoe = 0
    Jingle SndDie
END SUB

' --------------------------------------------------------------- the creatures

SUB MoveFoes
    DIM i AS INTEGER, pc AS INTEGER, pr AS INTEGER, alive AS INTEGER
    pc = (PX + HALF) \ TILE: pr = (PY + HALF) \ TILE
    IF TunnelsChanged OR pc <> DistFromC OR pr <> DistFromR THEN MapTunnels pc, pr
    alive = LiveFoes
    FOR i = 1 TO FoeCount
        ' The last creature of a round runs for the top left corner.
        IF alive = 1 AND NOT FoeFlee(i) AND (FoeState(i) = F_WALK OR FoeState(i) = F_GHOST) THEN
            FoeFlee(i) = -1: FoeState(i) = F_GHOST: FoeClock(i) = 0
        END IF
        SELECT CASE FoeState(i)
            CASE F_WALK: WalkFoe i
            CASE F_GHOST: GhostFoe i
            CASE F_PUMPED
                FoeClock(i) = FoeClock(i) + 1
                ' Left alone, it shrinks back and carries on.
                IF PumpFoe <> i AND FoeClock(i) > 45 THEN
                    FoeStage(i) = FoeStage(i) - 1: FoeClock(i) = 0
                    IF FoeStage(i) <= 0 THEN FoeState(i) = F_WALK: SnapFoe i
                END IF
            CASE F_POP
                FoeClock(i) = FoeClock(i) + 1
                IF FoeClock(i) > 24 THEN FoeState(i) = F_GONE
            CASE F_WINDUP
                FoeClock(i) = FoeClock(i) + 1
                IF FoeClock(i) > 40 THEN FoeState(i) = F_FIRE: FoeClock(i) = 0: Sfx SndFire
            CASE F_FIRE
                FoeClock(i) = FoeClock(i) + 1
                IF FoeClock(i) > 36 THEN FoeState(i) = F_WALK: FoeCool(i) = 200 + INT(RND * 200)
        END SELECT
    NEXT
END SUB

FUNCTION FlameReach (i)
    DIM n AS INTEGER
    n = FoeClock(i) * 12
    IF n > 3 * TILE THEN n = 3 * TILE
    FlameReach = n
END FUNCTION

' Puts a creature back on the nearest tile after pumping or floating.
SUB SnapFoe (i)
    FoeX(i) = INT((FoeX(i) + HALF) / TILE) * TILE
    FoeY(i) = INT((FoeY(i) + HALF) / TILE) * TILE
    FoeDir(i) = 0
END SUB

SUB WalkFoe (i)
    DIM c AS INTEGER, r AS INTEGER, d AS INTEGER, best AS INTEGER, bestDist AS INTEGER
    DIM nd AS INTEGER, tries AS INTEGER, dx AS SINGLE, dy AS SINGLE
    IF FoeCool(i) > 0 THEN FoeCool(i) = FoeCool(i) - 1
    FoeRestless(i) = FoeRestless(i) + 1
    IF FoeDir(i) = 0 THEN
        ' On a tile: choose the next one.
        c = FoeX(i) \ TILE: r = FoeY(i) \ TILE
        ' Drakes stop to breathe fire along their row when the digger is near.
        IF FoeKind(i) = KIND_DRAKE AND FoeCool(i) = 0 THEN
            IF r = (PY + HALF) \ TILE AND ABS(c - (PX + HALF) \ TILE) <= 5 AND RND < .4 THEN
                IF PX > FoeX(i) THEN FoeFace(i) = 1 ELSE FoeFace(i) = -1
                FoeState(i) = F_WINDUP: FoeClock(i) = 0
                EXIT SUB
            END IF
        END IF
        ' A creature that wanders too long floats off after the digger.
        IF FoeRestless(i) > 520 - RoundNo * 30 THEN
            FoeRestless(i) = INT(RND * 120): FoeState(i) = F_GHOST: FoeClock(i) = 0
            EXIT SUB
        END IF
        ' Head along the tunnels toward the digger, or wander.
        best = 0
        IF Dist(c, r) > 0 THEN
            bestDist = Dist(c, r)
            FOR d = 1 TO 4
                IF CanPass(c, r, d) THEN
                    nd = Dist(c + DirX(d), r + DirY(d))
                    IF nd >= 0 AND nd < bestDist AND RockAt(c + DirX(d), r + DirY(d)) = 0 THEN best = d: bestDist = nd
                END IF
            NEXT
        ELSE
            ' Out of reach: wander, and grow restless faster.
            FoeRestless(i) = FoeRestless(i) + 2
            d = FoeLast(i)
            IF d > 0 AND CanPass(c, r, d) AND RND < .85 THEN
                best = d
            ELSE
                FOR tries = 1 TO 12
                    nd = 1 + INT(RND * 4)
                    IF CanPass(c, r, nd) AND RockAt(c + DirX(nd), r + DirY(nd)) = 0 THEN
                        ' Turning back is the last resort.
                        IF nd <> ((FoeLast(i) + 1) MOD 4) + 1 OR tries > 8 THEN best = nd: EXIT FOR
                    END IF
                NEXT
            END IF
        END IF
        IF best = 0 THEN EXIT SUB
        FoeDir(i) = best: FoeLast(i) = best
        FoeTX(i) = (c + DirX(best)) * TILE: FoeTY(i) = (r + DirY(best)) * TILE
    END IF
    ' Move toward the chosen tile, landing exactly on it.
    d = FoeDir(i)
    IF d = 2 THEN FoeFace(i) = 1
    IF d = 4 THEN FoeFace(i) = -1
    dx = FoeTX(i) - FoeX(i): dy = FoeTY(i) - FoeY(i)
    IF ABS(dx) + ABS(dy) <= Speed THEN
        FoeX(i) = FoeTX(i): FoeY(i) = FoeTY(i): FoeDir(i) = 0
    ELSE
        FoeX(i) = FoeX(i) + DirX(d) * Speed: FoeY(i) = FoeY(i) + DirY(d) * Speed
    END IF
END SUB

SUB GhostFoe (i)
    DIM tx AS SINGLE, ty AS SINGLE, dx AS SINGLE, dy AS SINGLE, length AS SINGLE, s AS SINGLE
    DIM c AS INTEGER, r AS INTEGER
    FoeClock(i) = FoeClock(i) + 1
    IF FoeFlee(i) THEN
        tx = 0: ty = 0: s = Speed * 1.2
    ELSE
        tx = PX: ty = PY: s = GhostSpeed
    END IF
    dx = tx - FoeX(i): dy = ty - FoeY(i)
    length = SQR(dx * dx + dy * dy)
    IF FoeFlee(i) AND length < 2 THEN FoeState(i) = F_GONE: EXIT SUB ' it got away
    IF length > s THEN
        FoeX(i) = FoeX(i) + dx / length * s: FoeY(i) = FoeY(i) + dy / length * s
    END IF
    IF dx > 0 THEN FoeFace(i) = 1 ELSE FoeFace(i) = -1
    ' After a while it takes shape again in the tunnel it is over, when that
    ' is not right on top of the digger.
    IF NOT FoeFlee(i) AND FoeClock(i) > 90 THEN
        c = INT((FoeX(i) + HALF) / TILE): r = INT((FoeY(i) + HALF) / TILE)
        IF c >= 0 AND c < COLS AND r >= 1 AND r < ROWS THEN
            IF Dug(c, r) AND RockAt(c, r) = 0 AND ABS(c * TILE - PX) + ABS(r * TILE - PY) >= 2 * TILE THEN
                FoeX(i) = c * TILE: FoeY(i) = r * TILE
                FoeState(i) = F_WALK: FoeDir(i) = 0: FoeRestless(i) = INT(RND * 120)
            END IF
        END IF
    END IF
END SUB

' ------------------------------------------------------------------- the rocks

SUB MoveRocks
    DIM i AS INTEGER, j AS INTEGER, c AS INTEGER, r AS INTEGER, points AS LONG
    FOR i = 1 TO RockCount
        c = RockC(i): r = RockY(i) \ TILE
        SELECT CASE RockState(i)
            CASE R_REST
                ' It falls once the tile below is dug and the digger is out from under it.
                IF r < ROWS - 1 THEN
                    IF Dug(c, r + 1) AND NOT ((PX + HALF) \ TILE = c AND (PY + HALF) \ TILE = r + 1) THEN
                        RockState(i) = R_WOBBLE: RockClock(i) = 0
                    END IF
                END IF
            CASE R_WOBBLE
                RockClock(i) = RockClock(i) + 1
                IF RockClock(i) > 45 THEN
                    RockState(i) = R_FALL: RockKills(i) = 0
                    Dug(c, r) = -1: OpenPassage c, r, c, r + 1
                    _DEST DirtImg
                    LINE (c * TILE, r * TILE)-(c * TILE + TILE - 1, r * TILE + TILE - 1), _RGB32(16, 10, 12), BF
                    _DEST 0
                    Sfx SndFall
                END IF
            CASE R_FALL
                RockY(i) = RockY(i) + 6 ' it divides TILE
                ' It takes along whatever it lands on.
                FOR j = 1 TO FoeCount
                    SELECT CASE FoeState(j)
                        CASE F_WALK, F_PUMPED, F_WINDUP, F_FIRE
                            IF ABS(FoeX(j) - c * TILE) < TILE * 3 \ 4 AND FoeY(j) > RockY(i) AND FoeY(j) - RockY(i) < TILE * 7 \ 8 THEN
                                FoeState(j) = F_CRUSHED: FoeRock(j) = i
                                RockKills(i) = RockKills(i) + 1
                                IF PumpFoe = j THEN Harp = 0: PumpFoe = 0
                            END IF
                    END SELECT
                    IF FoeState(j) = F_CRUSHED AND FoeRock(j) = i THEN FoeX(j) = c * TILE: FoeY(j) = RockY(i) + TILE \ 3
                NEXT
                IF GameState = G_PLAY AND ABS(PX - c * TILE) < TILE * 3 \ 4 AND PY > RockY(i) AND PY - RockY(i) < TILE * 7 \ 8 THEN KillDigger
                IF RockY(i) MOD TILE = 0 THEN
                    r = RockY(i) \ TILE
                    IF r >= ROWS - 1 THEN
                        LandRock i
                    ELSEIF NOT Dug(c, r + 1) THEN
                        LandRock i
                    END IF
                END IF
            CASE R_BREAK
                RockClock(i) = RockClock(i) + 1
                IF RockClock(i) > 40 THEN RockState(i) = R_GONE
        END SELECT
    NEXT
END SUB

SUB LandRock (i)
    DIM j AS INTEGER, points AS LONG
    RockState(i) = R_BREAK: RockClock(i) = 0
    Sfx SndCrush
    FOR j = 1 TO FoeCount
        IF FoeState(j) = F_CRUSHED AND FoeRock(j) = i THEN FoeState(j) = F_GONE
    NEXT
    SELECT CASE RockKills(i)
        CASE 0: points = 0
        CASE 1: points = 1000
        CASE 2: points = 2500
        CASE 3: points = 4000
        CASE ELSE: points = 4000 + 2000 * (RockKills(i) - 3)
    END SELECT
    IF points > 0 THEN AddScore points: ShowPoints RockC(i) * TILE, RockY(i), points
    RocksDropped = RocksDropped + 1
    ' Two dropped rocks bring out the mushroom where the digger started.
    IF RocksDropped = 2 THEN BonusClock = 600
END SUB

SUB CheckBonus
    IF BonusClock > 0 THEN BonusClock = BonusClock - 1
END SUB

' ---------------------------------------------------------------- score popups

SUB ShowPoints (x AS SINGLE, y AS SINGLE, points AS LONG)
    DIM i AS INTEGER, slot AS INTEGER
    slot = 1
    FOR i = 1 TO MAX_POPS
        IF PopClock(i) = 0 THEN slot = i: EXIT FOR
    NEXT
    PopX(slot) = x: PopY(slot) = y: PopClock(slot) = 70
    PopText(slot) = LTRIM$(STR$(points))
END SUB

SUB UpdatePops
    DIM i AS INTEGER
    FOR i = 1 TO MAX_POPS
        IF PopClock(i) > 0 THEN PopClock(i) = PopClock(i) - 1: PopY(i) = PopY(i) - .45
    NEXT
END SUB

' ----------------------------------------------------------------------- input

SUB ReadInput
    DIM k AS LONG, jx AS INTEGER, jy AS INTEGER, jPress AS INTEGER
    DIM up AS INTEGER, down AS INTEGER, lft AS INTEGER, rgt AS INTEGER
    InAnyKey = 0
    DO
        k = _KEYHIT
        IF k > 0 THEN InAnyKey = -1
        SELECT CASE k
            CASE 27
                IF Demo THEN QuitGame = -1 ELSE StartDemo
                InAnyKey = 0
            CASE 80, 112 ' P
                IF NOT Demo THEN Paused = NOT Paused
                InAnyKey = 0
            CASE 70, 102 ' F
                FullPref = NOT FullPref
                ApplyFullscreen
                InAnyKey = 0
            CASE 77, 109 ' M
                MusicOn = NOT MusicOn
                InAnyKey = 0
        END SELECT
    LOOP UNTIL k = 0
    ' STICK(0) and STICK(1) are 0 with no joystick, else 1-200 with 100 at rest.
    ' STRIG(0) latches presses of button 1, so it is read every frame.
    jx = STICK(0): jy = STICK(1)
    jPress = STRIG(0)
    IF Demo THEN
        IF InAnyKey OR jPress THEN StartGame: EXIT SUB
        Autopilot
        EXIT SUB
    END IF
    IF GameState = G_OVER AND (InAnyKey OR jPress) AND StateClock > 60 THEN StartDemo: EXIT SUB
    up = _KEYDOWN(18432) OR _KEYDOWN(119) OR _KEYDOWN(87) OR (jy > 0 AND jy < 70)
    down = _KEYDOWN(20480) OR _KEYDOWN(115) OR _KEYDOWN(83) OR jy > 130
    lft = _KEYDOWN(19200) OR _KEYDOWN(97) OR _KEYDOWN(65) OR (jx > 0 AND jx < 70)
    rgt = _KEYDOWN(19712) OR _KEYDOWN(100) OR _KEYDOWN(68) OR jx > 130
    ' With two directions held, the one across the way the digger faces wins,
    ' so it turns at the next corner.
    InDir = 0
    IF up THEN InDir = 1
    IF down THEN InDir = 3
    IF (lft OR rgt) AND (InDir = 0 OR PFace = 1 OR PFace = 3) THEN
        IF lft THEN InDir = 4 ELSE InDir = 2
    END IF
    InFire = _KEYDOWN(32) OR STRIG(1)
    InFirePressed = (InFire AND NOT LastFire) OR jPress
    LastFire = InFire
END SUB

' The demo digger heads for the nearest creature and pumps it: it gets onto
' the creature's row, digs toward it until the way is open, then shoots.
SUB Autopilot
    DIM i AS INTEGER, best AS INTEGER, bestD AS LONG, dist2 AS LONG, dx AS INTEGER, dy AS INTEGER
    DIM fc AS INTEGER, fr AS INTEGER, pc AS INTEGER, pr AS INTEGER, k AS INTEGER, toward AS INTEGER
    DIM inReach AS INTEGER, gap AS INTEGER
    InDir = 0: InFirePressed = 0: InFire = 0
    IF GameState <> G_PLAY THEN EXIT SUB
    IF Harp = 2 THEN
        InFire = -1: InFirePressed = (Frame MOD 8 = 0)
        EXIT SUB
    END IF
    IF Harp = 1 THEN EXIT SUB
    pc = (PX + HALF) \ TILE: pr = (PY + HALF) \ TILE
    ' Step off the row of a drake about to breathe fire.
    FOR i = 1 TO FoeCount
        IF FoeState(i) = F_WINDUP OR FoeState(i) = F_FIRE THEN
            IF (FoeY(i) + HALF) \ TILE = pr AND ABS(FoeX(i) - PX) < 5 * TILE THEN
                IF pr > 1 AND RockAt(pc, pr - 1) = 0 THEN InDir = 1 ELSE InDir = 3
                EXIT SUB
            END IF
        END IF
    NEXT
    bestD = 999999
    FOR i = 1 TO FoeCount
        IF FoeState(i) = F_WALK OR FoeState(i) = F_WINDUP OR FoeState(i) = F_FIRE OR (FoeState(i) = F_GHOST AND FoeFlee(i)) THEN
            dist2 = (FoeX(i) - PX) ^ 2 + (FoeY(i) - PY) ^ 2
            IF dist2 < bestD THEN bestD = dist2: best = i
        END IF
    NEXT
    IF best = 0 THEN EXIT SUB
    fc = (FoeX(best) + HALF) \ TILE: fr = (FoeY(best) + HALF) \ TILE
    dx = fc - pc: dy = fr - pr
    IF (dx = 0 OR dy = 0) AND ABS(dx) + ABS(dy) <= 4 AND ABS(dx) + ABS(dy) > 0 THEN
        IF dx > 0 THEN toward = 2 ELSE IF dx < 0 THEN toward = 4 ELSE IF dy > 0 THEN toward = 3 ELSE toward = 1
        gap = ABS(dx) + ABS(dy)
        inReach = gap <= 3
        FOR k = 1 TO gap - 1
            IF NOT Dug(pc + SGN(dx) * k, pr + SGN(dy) * k) THEN inReach = 0
        NEXT
        IF inReach THEN
            IF PFace = toward THEN InFirePressed = -1 ELSE InDir = toward
            EXIT SUB
        END IF
        ' Dig closer, but not into it.
        IF gap > 1 THEN InDir = toward
        EXIT SUB
    END IF
    ' Get onto its row first, from at least two tiles to the side.
    IF dy <> 0 THEN
        IF ABS(dx) < 2 THEN
            IF (dx >= 0 AND pc > 1) OR pc >= COLS - 2 THEN InDir = 4 ELSE InDir = 2
        ELSEIF dy > 0 THEN
            InDir = 3
        ELSE
            InDir = 1
        END IF
    ELSEIF dx > 0 THEN
        InDir = 2
    ELSE
        InDir = 4
    END IF
END SUB

' ---------------------------------------------------------------------- drawing

' Sheet cell n at (x, y), size pixels square, facing left if flip.
SUB Spr (n, x AS SINGLE, y AS SINGLE, size AS SINGLE, flip)
    DIM sx AS INTEGER, sy AS INTEGER, x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER
    sx = (n MOD 16) * TILE: sy = (n \ 16) * TILE
    x1 = INT(x): y1 = INT(y): x2 = INT(x + size - 1): y2 = INT(y + size - 1)
    IF flip THEN
        _PUTIMAGE (x2, y1)-(x1, y2), Sheet, 0, (sx, sy)-(sx + TILE - 1, sy + TILE - 1)
    ELSE
        _PUTIMAGE (x1, y1)-(x2, y2), Sheet, 0, (sx, sy)-(sx + TILE - 1, sy + TILE - 1)
    END IF
END SUB

' A big cell centered on (cx, cy).
SUB SprBig (n, cx AS SINGLE, cy AS SINGLE, flip)
    DIM sx AS INTEGER, x1 AS INTEGER, y1 AS INTEGER
    sx = n * BIG: x1 = INT(cx) - BIG \ 2: y1 = INT(cy) - BIG \ 2
    IF flip THEN
        _PUTIMAGE (x1 + BIG - 1, y1)-(x1, y1 + BIG - 1), BigSheet, 0, (sx, 0)-(sx + BIG - 1, BIG - 1)
    ELSE
        _PUTIMAGE (x1, y1)-(x1 + BIG - 1, y1 + BIG - 1), BigSheet, 0, (sx, 0)-(sx + BIG - 1, BIG - 1)
    END IF
END SUB

SUB Render
    DIM i AS INTEGER, n AS INTEGER, size AS SINGLE, x AS SINGLE, y AS SINGLE, flip AS INTEGER
    DIM tx AS INTEGER, ty AS INTEGER, reach AS INTEGER
    LINE (0, 0)-(SW - 1, SH - 1), _RGB32(0, 0, 0), BF
    _PUTIMAGE (0, FIELD_Y), DirtImg
    ' Flowers on the surface count the rounds.
    FOR i = 1 TO RoundNo
        IF i > 18 THEN EXIT FOR
        Spr S_FLOWER + ((Frame \ 40 + i) MOD 2), COLS * TILE - i * 36 - 12, FIELD_Y, TILE, 0
    NEXT
    IF BonusClock > 0 AND (BonusClock > 120 OR Frame MOD 10 < 6) THEN
        Spr S_MUSHROOM, StartC * TILE, StartR * TILE + FIELD_Y, TILE, 0
    END IF
    FOR i = 1 TO RockCount
        x = RockC(i) * TILE: y = RockY(i) + FIELD_Y
        SELECT CASE RockState(i)
            CASE R_REST, R_FALL: Spr S_ROCK, x, y, TILE, 0
            CASE R_WOBBLE: Spr S_ROCK, x + ((Frame \ 3) MOD 2) * 4 - 2, y, TILE, 0
            CASE R_BREAK
                IF RockClock(i) < 10 THEN Spr S_CRACKED, x, y, TILE, 0 ELSE Spr S_CRUMBLE + (RockClock(i) - 10) \ 11, x, y, TILE, 0
        END SELECT
    NEXT
    ' The harpoon line.
    IF Harp > 0 AND GameState = G_PLAY THEN
        IF Harp = 2 THEN
            tx = FoeX(PumpFoe) + HALF: ty = FoeY(PumpFoe) + HALF
        ELSE
            tx = PX + HALF + DirX(HarpDir) * (18 + HarpLen): ty = PY + HALF + DirY(HarpDir) * (18 + HarpLen)
        END IF
        LINE (PX + HALF, PY + HALF + FIELD_Y)-(tx, ty + FIELD_Y), _RGB32(230, 230, 240)
        LINE (PX + HALF + DirY(HarpDir), PY + HALF + FIELD_Y + DirX(HarpDir))-(tx + DirY(HarpDir), ty + FIELD_Y + DirX(HarpDir)), _RGB32(150, 150, 170)
        LINE (tx - 3, ty - 3 + FIELD_Y)-(tx + 3, ty + 3 + FIELD_Y), _RGB32(250, 250, 255), BF
    END IF
    ' Floating eyes go over the digger, everything else under it.
    FOR i = 1 TO FoeCount
        IF FoeState(i) <> F_GHOST THEN DrawFoe i
    NEXT
    DrawDigger
    FOR i = 1 TO FoeCount
        IF FoeState(i) = F_GHOST THEN DrawFoe i
    NEXT
    COLOR _RGB32(255, 255, 255)
    FOR i = 1 TO MAX_POPS
        IF PopClock(i) > 0 THEN _PRINTSTRING (PopX(i), PopY(i) + FIELD_Y), PopText(i)
    NEXT
    DrawHud
    IF Demo THEN DrawTitle
    IF Paused THEN Banner "PAUSED", "P to carry on"
    IF NOT Demo THEN
        SELECT CASE GameState
            CASE G_READY: Banner "ROUND" + STR$(RoundNo), "READY!"
            CASE G_CLEAR: Banner "ROUND" + STR$(RoundNo) + " CLEAR", ""
            CASE G_OVER: Banner "GAME OVER", "any key for the title screen"
        END SELECT
    END IF
END SUB

SUB DrawDigger
    DIM n AS INTEGER, first AS INTEGER
    IF GameState = G_DYING THEN
        ' Dazed, then squashed flat, then gone.
        IF StateClock < 120 THEN Spr S_DYING + StateClock \ 30, PX, PY + FIELD_Y, TILE, PFace = 4
        EXIT SUB
    END IF
    IF GameState = G_READY AND (StateClock \ 8) MOD 2 = 1 THEN EXIT SUB
    SELECT CASE PFace
        CASE 1: first = S_DIGGER + 6
        CASE 3: first = S_DIGGER + 12
        CASE ELSE: first = S_DIGGER
    END SELECT
    ' Four walking frames a tile; pumping works the plunger.
    n = first + (PAnim \ 4) MOD 4
    IF Harp = 1 THEN n = first + 4
    IF Harp = 2 THEN n = first + 4 + (PumpClock \ 5) MOD 2
    Spr n, PX, PY + FIELD_Y, TILE, PFace = 4
END SUB

SUB DrawFoe (i)
    DIM n AS INTEGER, x AS SINGLE, y AS SINGLE, flip AS INTEGER, reach AS INTEGER, k AS INTEGER
    DIM steps AS INTEGER, pace AS INTEGER
    x = FoeX(i): y = FoeY(i) + FIELD_Y: flip = (FoeFace(i) < 0)
    pace = (Frame \ 7 + i) MOD 4 ' creatures are out of step with each other
    SELECT CASE FoeState(i)
        CASE F_GONE: EXIT SUB
        CASE F_WALK, F_CRUSHED
            IF FoeKind(i) = KIND_DRAKE THEN
                Spr S_DRAKE + pace, x, y, TILE, flip
            ELSEIF (Frame + i * 53) MOD 170 < 7 THEN
                Spr S_BLINK, x, y, TILE, flip
            ELSE
                Spr S_BLOB + pace, x, y, TILE, flip
            END IF
        CASE F_GHOST
            Spr S_EYES + (Frame \ 15) MOD 2, x, y, TILE, flip
        CASE F_PUMPED
            IF FoeKind(i) = KIND_DRAKE THEN n = B_DRAKE ELSE n = B_BLOB
            IF FoeStage(i) = 1 THEN
                ' Barely pumped, it still fits in its tile.
                IF FoeKind(i) = KIND_DRAKE THEN Spr S_WINDUP + 1, x, y, TILE, flip ELSE Spr S_BLOB, x, y, TILE, flip
            ELSE
                SprBig n + FoeStage(i) - 2, x + HALF, y + HALF, flip
            END IF
        CASE F_POP
            Spr S_POP + FoeClock(i) \ 9, x - HALF \ 2, y - HALF \ 2, TILE + HALF, 0
        CASE F_WINDUP
            ' A drake draws in breath before it breathes.
            Spr S_WINDUP + (FoeClock(i) \ 8) MOD 2, x, y, TILE, flip
        CASE F_FIRE
            Spr S_BREATHE, x, y, TILE, flip
            reach = FlameReach(i)
            steps = (reach + TILE - 1) \ TILE
            FOR k = 0 TO steps - 1
                IF k = steps - 1 THEN n = S_FLAME_TIP ELSE n = S_FLAME
                n = n + (Frame \ 3 + k) MOD 3
                IF FoeFace(i) > 0 THEN
                    Spr n, x + TILE + k * TILE, y, TILE, 0
                ELSE
                    Spr n, x - TILE - k * TILE, y, TILE, -1
                END IF
            NEXT
    END SELECT
END SUB

SUB DrawHud
    DIM i AS INTEGER
    COLOR _RGB32(255, 90, 80)
    _PRINTSTRING (16, 4), "SCORE"
    _PRINTSTRING (SW \ 2 - 50, 4), "HIGH SCORE"
    _PRINTSTRING (SW - 120, 4), "ROUND"
    COLOR _RGB32(255, 255, 255)
    _PRINTSTRING (16, 24), RIGHT$("      " + LTRIM$(STR$(Score)), 7)
    _PRINTSTRING (SW \ 2 - 40, 24), RIGHT$("      " + LTRIM$(STR$(HiScore)), 7)
    _PRINTSTRING (SW - 110, 24), LTRIM$(STR$(RoundNo))
    ' Spare diggers along the bottom.
    FOR i = 1 TO Lives - 1
        IF i > 10 THEN EXIT FOR
        Spr S_LIFE, 12 + (i - 1) * 38, FIELD_Y + ROWS * TILE + 4, 32, 0
    NEXT
    IF NOT MusicOn AND NOT Demo THEN
        COLOR _RGB32(150, 150, 160)
        _PRINTSTRING (SW - 120, FIELD_Y + ROWS * TILE + 10), "music off"
    END IF
END SUB

SUB Banner (title$, subtitle$)
    DIM w AS INTEGER, y AS INTEGER
    y = SH \ 2 - 40
    LINE (SW \ 2 - 200, y)-(SW \ 2 + 200, y + 76), _RGBA32(0, 0, 0, 190), BF
    LINE (SW \ 2 - 200, y)-(SW \ 2 + 200, y + 76), _RGB32(255, 214, 70), B
    COLOR _RGB32(255, 214, 70)
    w = _PRINTWIDTH(title$)
    _PRINTSTRING (SW \ 2 - w \ 2, y + 14), title$
    COLOR _RGB32(230, 230, 240)
    w = _PRINTWIDTH(subtitle$)
    IF subtitle$ <> "" THEN _PRINTSTRING (SW \ 2 - w \ 2, y + 44), subtitle$
END SUB

SUB DrawTitle
    DIM w AS INTEGER, lw AS INTEGER, y AS INTEGER, x AS INTEGER
    LINE (90, 96)-(SW - 90, SH - 80), _RGBA32(0, 0, 0, 170), BF
    lw = _WIDTH(LogoImg) * 9
    _PUTIMAGE (SW \ 2 - lw \ 2 + 8, 128)-(SW \ 2 + lw \ 2 + 7, 128 + 9 * 20 - 1), LogoImg
    _PUTIMAGE (SW \ 2 - lw \ 2, 120)-(SW \ 2 + lw \ 2 - 1, 120 + 9 * 20 - 1), LogoImg
    y = 320: x = SW \ 2 - 230
    Spr S_BLOB + (Frame \ 7) MOD 4, x, y, TILE, 0
    Spr S_DRAKE + (Frame \ 7) MOD 4, x, y + 60, TILE, 0
    Spr S_ROCK, x, y + 120, TILE, 0
    COLOR _RGB32(230, 230, 240)
    _PRINTSTRING (x + 70, y + 14), "Blob: chases you through the tunnels"
    _PRINTSTRING (x + 70, y + 74), "Drake: breathes fire along its row"
    _PRINTSTRING (x + 70, y + 134), "Rock: dig out from under it to drop it"
    COLOR _RGB32(255, 214, 70)
    w = _PRINTWIDTH("Arrows dig   Space pumps   F fullscreen   M music")
    _PRINTSTRING (SW \ 2 - w \ 2, y + 196), "Arrows dig   Space pumps   F fullscreen   M music"
    IF (Frame \ 30) MOD 2 = 0 THEN
        w = _PRINTWIDTH("Press any key or button to start")
        _PRINTSTRING (SW \ 2 - w \ 2, y + 236), "Press any key or button to start"
    END IF
END SUB
