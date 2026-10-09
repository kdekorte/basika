' ============================================================================
'  VORTEX - a tube shooter in the style of Atari's Tempest (1981)
' ----------------------------------------------------------------------------
'  You are the claw on the rim of a tube seen end-on. Enemies climb up the
'  lanes from the far end; shoot them before they reach the rim.
'    Flippers (red)    climb and flip between lanes; at the rim they flip
'                      toward you and grab you.
'    Tankers (purple)  climb slowly and split into two flippers when shot.
'    Spikers (green)   lay a spike in their lane, then turn back. Each shot
'                      shortens a spike.
'  Enemies fire up their lanes, too. The superzapper clears the tube once
'  per level; a second zap destroys one enemy. After a level you warp down
'  the tube to the next one; a spike in your lane on the way is fatal.
'  The tubes are a circle, square, plus, V, star and flat line, then they
'  repeat in a new color, faster.
'
'  Keys    Left/Right (or A/D) move   Space fire (hold, or tap faster)
'          Z superzapper
'          P pause   F fullscreen on/off   Esc title screen / quit
'  A joystick or gamepad works too, alongside the keys: the stick moves,
'  button 1 fires (and starts a game), button 2 is the superzapper.
'  Games play fullscreen (F switches that off); the title screen runs a
'  demo game in a window.
'
'  Run with: basika -w demo/vortex.bas
'  (basika --headless demo/vortex.bas --selftest [frames] plays the demo for
'   that many frames, 420 by default, saves vortex_selftest.png and exits.)
' ============================================================================

CONST SW = 1024
CONST SH = 768
CONST CX = 512
CONST CY = 410
CONST ZFAR = 5 ' the far end of the tube is 1/ZFAR the size of the rim
CONST MAX_POINTS = 24
CONST MAX_FOES = 40
CONST MAX_SHOTS = 8
CONST MAX_BULLETS = 12
CONST MAX_PARTS = 200
CONST MAX_STARS = 70
CONST FOE_FLIPPER = 1
CONST FOE_TANKER = 2
CONST FOE_SPIKER = 3
CONST ST_PLAY = 1
CONST ST_DYING = 2
CONST ST_WARP = 3
CONST ST_ZOOM = 4
CONST ST_OVER = 5
CONST SHAPE_COUNT = 6
CONST KEY_LEFT = 19200
CONST KEY_RIGHT = 19712
CONST FLIP_FRAMES = 14
CONST GRAB_FRAMES = 10
CONST SHOT_SPEED = .045
CONST AUTO_FIRE_FRAMES = 8 ' holding fire shoots every 8 frames, 7.5 a second
CONST TAP_FIRE_FRAMES = 2 ' each new press shoots at once, at most every 2 frames
CONST BULLET_SPEED = .011
CONST EXTRA_LIFE = 20000
CONST YELLOW = _RGB32(255, 240, 40)
CONST WHITE = _RGB32(255, 255, 255)
CONST RED = _RGB32(255, 40, 50)
CONST PURPLE = _RGB32(200, 60, 255)
CONST GREEN = _RGB32(40, 255, 90)
CONST PINK = _RGB32(255, 130, 200)

TYPE FoeType
    kind AS INTEGER ' 0 = free slot
    lane AS INTEGER
    flipTo AS INTEGER ' lane a flip ends in
    d AS SINGLE ' depth: 0 at the far end, 1 at the rim
    flipT AS SINGLE ' flip progress, 0 when not flipping
    clock AS SINGLE
    speed AS SINGLE
    goal AS SINGLE ' how high a spiker climbs
END TYPE

' The tube: rim points in screen pixels, and the point the far end shrinks to.
DIM SHARED RimX(MAX_POINTS) AS SINGLE, RimY(MAX_POINTS) AS SINGLE
DIM SHARED NP AS INTEGER, Lanes AS INTEGER, Closed AS INTEGER
DIM SHARED VX AS SINGLE, VY AS SINGLE, CamZ AS SINGLE
DIM SHARED CornerX(16) AS SINGLE, CornerY(16) AS SINGLE, CornerSub(16) AS INTEGER
DIM SHARED TubeColor AS _UNSIGNED LONG
' Project sets these.
DIM SHARED PrX AS SINGLE, PrY AS SINGLE, PrOK AS INTEGER
DIM SHARED OutX(MAX_POINTS) AS SINGLE, OutY(MAX_POINTS) AS SINGLE
DIM SHARED InX(MAX_POINTS) AS SINGLE, InY(MAX_POINTS) AS SINGLE

DIM SHARED Foes(MAX_FOES) AS FoeType
DIM SHARED Spike(MAX_POINTS) AS SINGLE
DIM SHARED ShotOn(MAX_SHOTS) AS INTEGER, ShotLane(MAX_SHOTS) AS INTEGER, ShotD(MAX_SHOTS) AS SINGLE
DIM SHARED BulOn(MAX_BULLETS) AS INTEGER, BulLane(MAX_BULLETS) AS INTEGER, BulD(MAX_BULLETS) AS SINGLE
DIM SHARED PtX(MAX_PARTS) AS SINGLE, PtY(MAX_PARTS) AS SINGLE
DIM SHARED PtVX(MAX_PARTS) AS SINGLE, PtVY(MAX_PARTS) AS SINGLE
DIM SHARED PtLife(MAX_PARTS) AS INTEGER, PtColor(MAX_PARTS) AS _UNSIGNED LONG
DIM SHARED StarX(MAX_STARS) AS SINGLE, StarY(MAX_STARS) AS SINGLE, StarZ(MAX_STARS) AS SINGLE

DIM SHARED Level AS INTEGER, Lives AS INTEGER, Score AS LONG, HiScore AS LONG, NextLife AS LONG
DIM SHARED State AS INTEGER, StateClock AS INTEGER, Demo AS INTEGER, Paused AS INTEGER
DIM SHARED PlayerLane AS INTEGER, PlayerD AS SINGLE, MoveCool AS INTEGER, FireCool AS INTEGER, FireHeld AS INTEGER
DIM SHARED ZapUses AS INTEGER, ZapFlash AS INTEGER, DiedInWarp AS INTEGER
DIM SHARED Pending AS INTEGER, SpawnClock AS INTEGER
DIM SHARED InLeft AS INTEGER, InRight AS INTEGER, InFire AS INTEGER, InZap AS INTEGER
DIM SHARED FullPref AS INTEGER, FullOn AS INTEGER, QuitGame AS INTEGER, SelfTest AS INTEGER
DIM SHARED Frame AS LONG

DIM i AS INTEGER, frames AS LONG

SCREEN _NEWIMAGE(SW, SH, 32)
_TITLE "Vortex"
_PRINTMODE _KEEPBACKGROUND
_AUTODISPLAY OFF

FullPref = -1
IF LEFT$(COMMAND$, 10) = "--selftest" THEN
    SelfTest = -1
    frames = VAL(MID$(COMMAND$, 11))
    IF frames <= 0 THEN frames = 420
    RANDOMIZE 7
ELSE
    RANDOMIZE TIMER
END IF
FOR i = 0 TO MAX_STARS
    ResetStar i
    StarZ(i) = RND * ZFAR
NEXT

StartDemo
DO
    ReadInput
    IF NOT Paused THEN Update
    Render
    _DISPLAY
    Frame = Frame + 1
    IF SelfTest THEN
        IF Frame >= frames THEN SCREENSHOT "vortex_selftest.png": SYSTEM
    ELSE
        _LIMIT 60
    END IF
LOOP UNTIL QuitGame
IF FullOn THEN _FULLSCREEN _OFF
SYSTEM

' ---------------------------------------------------------------- game flow

SUB StartDemo
    Demo = -1
    NewGame
END SUB

SUB StartGame
    Demo = 0
    NewGame
END SUB

SUB NewGame
    Score = 0: Lives = 3: Level = 1: NextLife = EXTRA_LIFE
    Paused = 0: DiedInWarp = 0
    StartLevel
    ApplyFullscreen
END SUB

' Games play fullscreen unless F turned it off; the demo stays in a window.
SUB ApplyFullscreen
    DIM want AS INTEGER
    want = FullPref AND NOT Demo AND NOT SelfTest
    IF want <> FullOn THEN
        IF want THEN
            _FULLSCREEN _SQUAREPIXELS
        ELSE
            _FULLSCREEN _OFF
        END IF
        FullOn = want
    END IF
END SUB

SUB StartLevel
    DIM i AS INTEGER
    BuildShape Level
    FOR i = 0 TO MAX_FOES: Foes(i).kind = 0: NEXT
    FOR i = 0 TO MAX_POINTS: Spike(i) = 0: NEXT
    ClearShots
    Pending = 8 + 2 * Level
    IF Pending > 30 THEN Pending = 30
    SpawnClock = 40
    ZapUses = 0
    PlayerD = 1
    CamZ = -6 ' the new tube flies in from the distance
    State = ST_ZOOM: StateClock = 0
END SUB

' After a death the enemies still in the tube go back to the far end.
SUB RestartLevel
    DIM f AS INTEGER
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind THEN Foes(f).kind = 0: Pending = Pending + 1
    NEXT
    ClearShots
    PlayerD = 1
    SpawnClock = 40
    CamZ = -3
    State = ST_ZOOM: StateClock = 0
END SUB

SUB ClearShots
    DIM i AS INTEGER
    FOR i = 0 TO MAX_SHOTS: ShotOn(i) = 0: NEXT
    FOR i = 0 TO MAX_BULLETS: BulOn(i) = 0: NEXT
END SUB

SUB ReadInput
    DIM k AS LONG, jx AS INTEGER, jPress AS INTEGER
    DO
        k = _KEYHIT
        SELECT CASE k
            CASE 27
                IF Demo THEN
                    QuitGame = -1
                ELSE
                    StartDemo
                END IF
            CASE 32
                IF Demo OR State = ST_OVER THEN StartGame
            CASE 80, 112 ' P
                IF NOT Demo THEN Paused = NOT Paused
            CASE 70, 102 ' F
                FullPref = NOT FullPref
                ApplyFullscreen
            CASE 90, 122 ' Z
                IF NOT Demo THEN InZap = -1
        END SELECT
    LOOP UNTIL k = 0
    ' The joystick: STICK(0) is 0 with none attached, else 1-200 with 100 at
    ' rest. STRIG(0) and STRIG(4) latch presses of buttons 1 and 2, so they
    ' are read every frame to drop presses left over from the last game.
    jx = STICK(0)
    jPress = STRIG(0)
    IF STRIG(4) AND NOT Demo THEN InZap = -1
    IF jPress AND (Demo OR State = ST_OVER) THEN StartGame
    IF Demo THEN
        Autopilot
    ELSE
        InLeft = _KEYDOWN(KEY_LEFT) OR _KEYDOWN(97) OR _KEYDOWN(65) OR (jx > 0 AND jx < 70)
        InRight = _KEYDOWN(KEY_RIGHT) OR _KEYDOWN(100) OR _KEYDOWN(68) OR jx > 130
        InFire = _KEYDOWN(32) OR STRIG(1)
    END IF
END SUB

SUB Update
    SELECT CASE State
        CASE ST_ZOOM
            MovePlayer
            UpdateStars .1
            CamZ = CamZ + .12
            IF CamZ >= 0 THEN CamZ = 0: State = ST_PLAY: StateClock = 0
        CASE ST_PLAY
            MovePlayer
            FireShots
            IF InZap THEN Superzap
            SpawnFoes
            MoveShots
            MoveBullets
            MoveFoes
            IF State = ST_PLAY AND Pending = 0 AND LiveFoes% = 0 THEN BeginWarp
        CASE ST_WARP
            UpdateWarp
        CASE ST_DYING
            IF StateClock >= 110 THEN EndDeath
        CASE ST_OVER
            IF StateClock >= 300 THEN StartDemo
    END SELECT
    InZap = 0
    UpdateParticles
    IF ZapFlash > 0 THEN ZapFlash = ZapFlash - 1
    StateClock = StateClock + 1
END SUB

SUB BeginWarp
    ClearShots
    AddScore 500 * Level
    State = ST_WARP: StateClock = 0
    Sfx 5
END SUB

' The claw rides down the tube while the camera follows it, so the claw
' keeps its size and the tube rushes past. Spikes in its lane are fatal.
SUB UpdateWarp
    MovePlayer
    FireShots
    MoveShots
    UpdateStars .15
    PlayerD = PlayerD - .009
    CamZ = ZFAR - (ZFAR - 1) * PlayerD - 1
    IF Spike(PlayerLane) > 0 AND Spike(PlayerLane) >= PlayerD THEN
        DiedInWarp = -1
        KillPlayer
        EXIT SUB
    END IF
    IF PlayerD <= 0 THEN
        Level = Level + 1
        StartLevel
    END IF
END SUB

SUB KillPlayer
    Project PlayerLane + .5, PlayerD
    Burst PrX, PrY, 60, YELLOW, 7
    Burst PrX, PrY, 30, WHITE, 4
    State = ST_DYING: StateClock = 0
    Sfx 3
END SUB

SUB EndDeath
    Lives = Lives - 1
    IF Lives <= 0 THEN
        State = ST_OVER: StateClock = 0
    ELSEIF DiedInWarp THEN
        DiedInWarp = 0
        Level = Level + 1
        StartLevel
    ELSE
        RestartLevel
    END IF
END SUB

SUB AddScore (n AS LONG)
    Score = Score + n
    IF NOT Demo AND Score > HiScore THEN HiScore = Score
    IF Score >= NextLife THEN
        NextLife = NextLife + EXTRA_LIFE
        Lives = Lives + 1
        Sfx 6
    END IF
END SUB

' Sound effects play in the background, and not in the demo.
SUB Sfx (n AS INTEGER)
    IF Demo OR SelfTest THEN EXIT SUB
    SELECT CASE n
        CASE 1: PLAY "MBT255L64O5CO4G"
        CASE 2: PLAY "MBT255L64O2CO1GEC"
        CASE 3: PLAY "MBT150L16O3CO2AFDO1A"
        CASE 4: PLAY "MBT255L64O6CO4CO6CO4CO6CO4C"
        CASE 5: PLAY "MBT255L64O2CEGO3CEGO4CEGO5C"
        CASE 6: PLAY "MBT200L16O5CEG"
    END SELECT
END SUB

' ---------------------------------------------------------------- the tube

SUB SetCorner (i AS INTEGER, x AS SINGLE, y AS SINGLE, subdivisions AS INTEGER)
    CornerX(i) = x: CornerY(i) = y: CornerSub(i) = subdivisions
END SUB

' Places the rim points along the corners' edges, CornerSub(e) to an edge.
SUB SampleCorners (nc AS INTEGER)
    DIM e AS INTEGER, j AS INTEGER, k AS INTEGER, edges AS INTEGER
    NP = 0
    IF Closed THEN edges = nc ELSE edges = nc - 1
    FOR e = 0 TO edges - 1
        j = (e + 1) MOD nc
        FOR k = 0 TO CornerSub(e) - 1
            RimX(NP) = CornerX(e) + (CornerX(j) - CornerX(e)) * k / CornerSub(e)
            RimY(NP) = CornerY(e) + (CornerY(j) - CornerY(e)) * k / CornerSub(e)
            NP = NP + 1
        NEXT
    NEXT
    IF NOT Closed THEN RimX(NP) = CornerX(nc - 1): RimY(NP) = CornerY(nc - 1): NP = NP + 1
END SUB

' Rim points run so that lane + 1 is to the right along the bottom.
SUB BuildShape (lv AS INTEGER)
    DIM i AS INTEGER, a AS SINGLE, w AS SINGLE, l AS SINGLE, best AS SINGLE, y AS SINGLE
    VX = CX: VY = CY
    SELECT CASE (lv - 1) MOD SHAPE_COUNT
        CASE 0 ' circle
            Closed = -1: NP = 16
            FOR i = 0 TO NP - 1
                a = -_PI / 2 - _PI / 16 + i * _PI / 8
                RimX(i) = CX + 320 * COS(a): RimY(i) = CY - 320 * SIN(a)
            NEXT
        CASE 1 ' square
            Closed = -1
            SetCorner 0, CX - 300, CY + 300, 4
            SetCorner 1, CX + 300, CY + 300, 4
            SetCorner 2, CX + 300, CY - 300, 4
            SetCorner 3, CX - 300, CY - 300, 4
            SampleCorners 4
        CASE 2 ' plus
            Closed = -1: w = 106: l = 318
            SetCorner 0, CX - w, CY + l, 2
            SetCorner 1, CX + w, CY + l, 2
            SetCorner 2, CX + w, CY + w, 2
            SetCorner 3, CX + l, CY + w, 2
            SetCorner 4, CX + l, CY - w, 2
            SetCorner 5, CX + w, CY - w, 2
            SetCorner 6, CX + w, CY - l, 2
            SetCorner 7, CX - w, CY - l, 2
            SetCorner 8, CX - w, CY - w, 2
            SetCorner 9, CX - l, CY - w, 2
            SetCorner 10, CX - l, CY + w, 2
            SetCorner 11, CX - w, CY + w, 2
            SampleCorners 12
        CASE 3 ' V, open at the top
            Closed = 0: VY = CY - 200
            SetCorner 0, CX - 440, CY - 170, 8
            SetCorner 1, CX, CY + 310, 8
            SetCorner 2, CX + 440, CY - 170, 0
            SampleCorners 3
        CASE 4 ' star
            Closed = -1: NP = 16
            FOR i = 0 TO NP - 1
                a = -_PI / 2 - _PI / 16 + i * _PI / 8
                IF i MOD 2 = 0 THEN w = 320 ELSE w = 190
                RimX(i) = CX + w * COS(a): RimY(i) = CY - w * SIN(a)
            NEXT
        CASE 5 ' flat line, seen from above
            Closed = 0: VY = CY - 300
            SetCorner 0, CX - 470, CY + 270, 12
            SetCorner 1, CX + 470, CY + 270, 0
            SampleCorners 2
    END SELECT
    IF Closed THEN Lanes = NP ELSE Lanes = NP - 1
    ' Start in the lowest lane nearest the middle.
    best = -1E+09
    FOR i = 0 TO Lanes - 1
        y = (RimY(i) + RimY(NextPoint%(i))) / 2 - ABS((RimX(i) + RimX(NextPoint%(i))) / 2 - CX) * .01
        IF y > best THEN best = y: PlayerLane = i
    NEXT
    SELECT CASE ((lv - 1) \ SHAPE_COUNT) MOD 4
        CASE 0: TubeColor = _RGB32(50, 90, 255)
        CASE 1: TubeColor = _RGB32(255, 60, 60)
        CASE 2: TubeColor = _RGB32(240, 220, 40)
        CASE 3: TubeColor = _RGB32(40, 230, 255)
    END SELECT
END SUB

FUNCTION NextPoint% (i AS INTEGER)
    IF i + 1 >= NP THEN NextPoint% = 0 ELSE NextPoint% = i + 1
END FUNCTION

' A lane one step from l in direction dir; an open tube stops at its ends.
FUNCTION StepLane% (l AS INTEGER, dir AS INTEGER)
    DIM n AS INTEGER
    n = l + dir
    IF Closed THEN
        IF n < 0 THEN n = n + Lanes
        IF n >= Lanes THEN n = n - Lanes
    ELSE
        IF n < 0 OR n >= Lanes THEN n = l
    END IF
    StepLane% = n
END FUNCTION

' Signed lane steps from a to b, the short way round a closed tube.
FUNCTION LaneDist% (a AS INTEGER, b AS INTEGER)
    DIM n AS INTEGER
    n = b - a
    IF Closed THEN
        IF n > Lanes \ 2 THEN n = n - Lanes
        IF n < -(Lanes \ 2) THEN n = n + Lanes
    END IF
    LaneDist% = n
END FUNCTION

' Screen position of a point in the tube: u runs across the lanes (lane L
' spans L to L + 1), d from the far end (0) to the rim (1). PrOK is 0 when
' the point is behind the camera.
SUB Project (u AS SINGLE, d AS SINGLE)
    DIM i AS INTEGER, j AS INTEGER, f AS SINGLE, w AS SINGLE, z AS SINGLE, s AS SINGLE
    IF Closed THEN
        w = u - NP * INT(u / NP)
        i = INT(w)
        IF i >= NP THEN i = 0
        f = w - i
        j = i + 1
        IF j >= NP THEN j = 0
    ELSE
        i = INT(u)
        IF i < 0 THEN i = 0
        IF i > NP - 2 THEN i = NP - 2
        f = u - i
        j = i + 1
    END IF
    z = ZFAR - (ZFAR - 1) * d - CamZ
    PrOK = -1
    IF z < .1 THEN z = .1: PrOK = 0
    s = 1 / z
    PrX = VX + (RimX(i) + (RimX(j) - RimX(i)) * f - VX) * s
    PrY = VY + (RimY(i) + (RimY(j) - RimY(i)) * f - VY) * s
END SUB

' A line with a faint glow beside it, like a vector monitor.
SUB GlowLine (x1 AS SINGLE, y1 AS SINGLE, x2 AS SINGLE, y2 AS SINGLE, c AS _UNSIGNED LONG)
    DIM g AS _UNSIGNED LONG
    g = (c AND &HFFFFFF) OR &H50000000
    LINE (x1 + 1, y1)-(x2 + 1, y2), g
    LINE (x1, y1 + 1)-(x2, y2 + 1), g
    LINE (x1, y1)-(x2, y2), c
END SUB

SUB DrawTube
    DIM i AS INTEGER, j AS INTEGER, c AS _UNSIGNED LONG, od AS SINGLE, near AS SINGLE
    c = TubeColor
    IF ZapFlash > 0 AND (ZapFlash \ 2) MOD 2 = 0 THEN c = WHITE
    ' While warping the rim passes the camera; draw the walls up to just
    ' in front of it.
    od = 1
    near = (ZFAR - CamZ - .2) / (ZFAR - 1)
    IF near < od THEN od = near
    IF od <= 0 THEN EXIT SUB
    FOR i = 0 TO NP - 1
        Project i, od: OutX(i) = PrX: OutY(i) = PrY
        Project i, 0: InX(i) = PrX: InY(i) = PrY
    NEXT
    FOR i = 0 TO Lanes - 1
        j = NextPoint%(i)
        IF od = 1 THEN GlowLine OutX(i), OutY(i), OutX(j), OutY(j), c
        GlowLine InX(i), InY(i), InX(j), InY(j), c
    NEXT
    FOR i = 0 TO NP - 1
        GlowLine OutX(i), OutY(i), InX(i), InY(i), c
    NEXT
    IF State <> ST_DYING AND State <> ST_OVER THEN
        i = PlayerLane: j = NextPoint%(i)
        LINE (OutX(i), OutY(i))-(InX(i), InY(i)), YELLOW
        LINE (OutX(j), OutY(j))-(InX(j), InY(j)), YELLOW
    END IF
END SUB

' ---------------------------------------------------------------- the player

SUB MovePlayer
    DIM dir AS INTEGER
    IF InLeft AND NOT InRight THEN dir = -1
    IF InRight AND NOT InLeft THEN dir = 1
    IF dir = 0 THEN MoveCool = 0: EXIT SUB
    IF MoveCool > 0 THEN MoveCool = MoveCool - 1: EXIT SUB
    PlayerLane = StepLane%(PlayerLane, dir)
    MoveCool = 3
END SUB

' Holding fire repeats slowly; a new press fires as soon as the last shot is
' TAP_FIRE_FRAMES old, so tapping out-shoots holding. At most MAX_SHOTS are
' in the tube at once.
SUB FireShots
    DIM i AS INTEGER
    IF FireCool > 0 THEN FireCool = FireCool - 1
    IF NOT InFire THEN FireHeld = 0: EXIT SUB
    IF FireHeld THEN
        IF FireCool > 0 THEN EXIT SUB
    ELSE
        IF FireCool > AUTO_FIRE_FRAMES - TAP_FIRE_FRAMES THEN EXIT SUB
    END IF
    FOR i = 0 TO MAX_SHOTS - 1
        IF NOT ShotOn(i) THEN
            ShotOn(i) = -1: ShotLane(i) = PlayerLane: ShotD(i) = PlayerD
            FireCool = AUTO_FIRE_FRAMES
            FireHeld = -1
            Sfx 1
            EXIT FOR
        END IF
    NEXT
END SUB

SUB MoveShots
    DIM i AS INTEGER
    FOR i = 0 TO MAX_SHOTS - 1
        IF ShotOn(i) THEN
            ShotD(i) = ShotD(i) - SHOT_SPEED
            IF ShotD(i) <= 0 THEN ShotOn(i) = 0 ELSE ShotHits i
        END IF
    NEXT
END SUB

' A shot stops at the first bullet, enemy or spike it meets in its lane.
SUB ShotHits (i AS INTEGER)
    DIM l AS INTEGER, d AS SINGLE, b AS INTEGER, f AS INTEGER
    l = ShotLane(i): d = ShotD(i)
    FOR b = 0 TO MAX_BULLETS - 1
        IF BulOn(b) AND BulLane(b) = l AND ABS(BulD(b) - d) < .05 THEN
            BulOn(b) = 0: ShotOn(i) = 0
            Project l + .5, d
            Burst PrX, PrY, 6, PINK, 2
            EXIT SUB
        END IF
    NEXT
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind THEN
            IF FoeLane%(f) = l AND ABS(Foes(f).d - d) < .05 THEN
                ShotOn(i) = 0
                KillFoe f, -1
                EXIT SUB
            END IF
        END IF
    NEXT
    IF Spike(l) > 0 AND d <= Spike(l) THEN
        Spike(l) = Spike(l) - .07
        IF Spike(l) < .03 THEN Spike(l) = 0
        ShotOn(i) = 0
        AddScore 3
    END IF
END SUB

' The first zap of a level destroys every enemy and bullet; the second
' destroys the enemy closest to the rim.
SUB Superzap
    DIM f AS INTEGER, best AS INTEGER
    IF ZapUses = 0 THEN
        FOR f = 0 TO MAX_FOES
            IF Foes(f).kind THEN KillFoe f, 0
        NEXT
        ClearBullets
        ZapFlash = 16
        Sfx 4
    ELSEIF ZapUses = 1 THEN
        best = -1
        FOR f = 0 TO MAX_FOES
            IF Foes(f).kind THEN
                IF best < 0 THEN
                    best = f
                ELSEIF Foes(f).d > Foes(best).d THEN
                    best = f
                END IF
            END IF
        NEXT
        IF best >= 0 THEN KillFoe best, 0
        ZapFlash = 6
        Sfx 4
    END IF
    ZapUses = ZapUses + 1
END SUB

SUB ClearBullets
    DIM b AS INTEGER
    FOR b = 0 TO MAX_BULLETS: BulOn(b) = 0: NEXT
END SUB

' The claw sits across a lane at depth d: two prongs rising outward from the
' rim, with a notch between them.
SUB DrawClaw (lane AS INTEGER, d AS SINGLE, c AS _UNSIGNED LONG)
    DIM ax AS SINGLE, ay AS SINGLE, bx AS SINGLE, by AS SINGLE, mx AS SINGLE, my AS SINGLE
    DIM nx AS SINGLE, ny AS SINGLE, size AS SINGLE, h AS SINGLE, tx AS SINGLE, ty AS SINGLE
    DIM x2 AS SINGLE, y2 AS SINGLE, x3 AS SINGLE, y3 AS SINGLE, x4 AS SINGLE, y4 AS SINGLE
    Project lane, d: ax = PrX: ay = PrY
    IF NOT PrOK THEN EXIT SUB
    Project lane + 1, d: bx = PrX: by = PrY
    size = SQR((bx - ax) ^ 2 + (by - ay) ^ 2)
    IF size < 1 THEN EXIT SUB
    nx = (by - ay) / size: ny = -(bx - ax) / size
    mx = (ax + bx) / 2: my = (ay + by) / 2
    IF (mx - VX) * nx + (my - VY) * ny < 0 THEN nx = -nx: ny = -ny
    h = size * .6
    IF h > 40 THEN h = 40
    IF h < 12 THEN h = 12
    tx = (bx - ax) / size: ty = (by - ay) / size ' along the rim, from a to b
    x2 = ax + nx * h + tx * size * .12: y2 = ay + ny * h + ty * size * .12
    x3 = mx + nx * h * .35: y3 = my + ny * h * .35
    x4 = bx + nx * h - tx * size * .12: y4 = by + ny * h - ty * size * .12
    GlowLine ax, ay, x2, y2, c
    GlowLine x2, y2, x3, y3, c
    GlowLine x3, y3, x4, y4, c
    GlowLine x4, y4, bx, by, c
    GlowLine bx, by, ax, ay, c
END SUB

' ---------------------------------------------------------------- enemies

FUNCTION LiveFoes%
    DIM f AS INTEGER, n AS INTEGER
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind THEN n = n + 1
    NEXT
    LiveFoes% = n
END FUNCTION

' The lane a foe counts as being in (the far one past halfway through a flip).
FUNCTION FoeLane% (f AS INTEGER)
    IF Foes(f).flipT >= .5 THEN FoeLane% = Foes(f).flipTo ELSE FoeLane% = Foes(f).lane
END FUNCTION

' Where a foe is across the lanes, moving over during a flip.
FUNCTION FoeU! (f AS INTEGER)
    FoeU! = Foes(f).lane + .5 + LaneDist%(Foes(f).lane, Foes(f).flipTo) * Foes(f).flipT
END FUNCTION

FUNCTION SpawnFoe% (kind AS INTEGER, lane AS INTEGER, d AS SINGLE)
    DIM f AS INTEGER
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind = 0 THEN
            Foes(f).kind = kind: Foes(f).lane = lane: Foes(f).flipTo = lane
            Foes(f).d = d: Foes(f).flipT = 0: Foes(f).clock = 0
            SELECT CASE kind
                CASE FOE_FLIPPER: Foes(f).speed = .0026 + .0003 * Level
                CASE FOE_TANKER: Foes(f).speed = .0018 + .0002 * Level
                CASE FOE_SPIKER: Foes(f).speed = .004 + .0002 * Level
            END SELECT
            IF Foes(f).speed > .009 THEN Foes(f).speed = .009
            Foes(f).goal = .35 + RND * .45
            SpawnFoe% = -1
            EXIT FUNCTION
        END IF
    NEXT
    SpawnFoe% = 0
END FUNCTION

SUB SpawnFoes
    DIM kind AS INTEGER, r AS SINGLE
    IF Pending <= 0 THEN EXIT SUB
    SpawnClock = SpawnClock - 1
    IF SpawnClock > 0 THEN EXIT SUB
    IF LiveFoes% >= 5 + Level THEN SpawnClock = 20: EXIT SUB
    kind = FOE_FLIPPER: r = RND
    IF Level >= 2 AND r < .25 THEN kind = FOE_TANKER
    IF Level >= 3 AND r > .8 THEN kind = FOE_SPIKER
    IF SpawnFoe%(kind, INT(RND * Lanes), 0) THEN Pending = Pending - 1
    SpawnClock = 50 - 3 * Level
    IF SpawnClock < 15 THEN SpawnClock = 15
    SpawnClock = SpawnClock + INT(RND * 30)
END SUB

' Destroys a foe; a shot tanker splits into two flippers.
SUB KillFoe (f AS INTEGER, canSplit AS INTEGER)
    DIM c AS _UNSIGNED LONG, lane AS INTEGER, d AS SINGLE, ok AS INTEGER
    Project FoeU!(f), Foes(f).d
    SELECT CASE Foes(f).kind
        CASE FOE_FLIPPER: c = RED: AddScore 150
        CASE FOE_TANKER: c = PURPLE: AddScore 100
        CASE FOE_SPIKER: c = GREEN: AddScore 50
    END SELECT
    Burst PrX, PrY, 14, c, 4
    Sfx 2
    IF canSplit AND Foes(f).kind = FOE_TANKER THEN
        lane = Foes(f).lane: d = Foes(f).d
        Foes(f).kind = 0
        Split lane, d
    ELSE
        Foes(f).kind = 0
    END IF
END SUB

SUB Split (lane AS INTEGER, d AS SINGLE)
    DIM ok AS INTEGER
    ok = SpawnFoe%(FOE_FLIPPER, StepLane%(lane, -1), d)
    ok = SpawnFoe%(FOE_FLIPPER, StepLane%(lane, 1), d)
END SUB

SUB StartFlip (f AS INTEGER, target AS INTEGER)
    IF target = Foes(f).lane THEN EXIT SUB
    Foes(f).flipTo = target
    Foes(f).flipT = .0001
END SUB

SUB MoveFoes
    DIM f AS INTEGER
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind THEN
            SELECT CASE Foes(f).kind
                CASE FOE_FLIPPER: MoveFlipper f
                CASE FOE_TANKER: MoveTanker f
                CASE FOE_SPIKER: MoveSpiker f
            END SELECT
            IF State <> ST_PLAY THEN EXIT SUB
        END IF
    NEXT
END SUB

SUB MoveFlipper (f AS INTEGER)
    DIM dir AS INTEGER
    IF Foes(f).flipT > 0 THEN
        Foes(f).flipT = Foes(f).flipT + 1 / FLIP_FRAMES
        IF Foes(f).flipT >= 1 THEN
            Foes(f).lane = Foes(f).flipTo: Foes(f).flipT = 0: Foes(f).clock = 0
        END IF
    END IF
    IF Foes(f).d < 1 THEN
        Foes(f).d = Foes(f).d + Foes(f).speed
        IF Foes(f).d >= 1 THEN Foes(f).d = 1: Foes(f).clock = 0
        IF Foes(f).flipT = 0 AND RND < .008 THEN
            IF RND < .5 THEN dir = -1 ELSE dir = 1
            StartFlip f, StepLane%(Foes(f).lane, dir)
        END IF
        EnemyFire f
    ELSEIF Foes(f).flipT = 0 THEN
        ' At the rim: flip toward the player, or grab the player.
        Foes(f).clock = Foes(f).clock + 1
        IF Foes(f).lane = PlayerLane THEN
            IF Foes(f).clock >= GRAB_FRAMES THEN KillPlayer
        ELSEIF Foes(f).clock >= 12 THEN
            StartFlip f, StepLane%(Foes(f).lane, SGN(LaneDist%(Foes(f).lane, PlayerLane)))
        END IF
    END IF
END SUB

SUB MoveTanker (f AS INTEGER)
    DIM lane AS INTEGER
    Foes(f).d = Foes(f).d + Foes(f).speed
    IF Foes(f).d >= 1 THEN
        lane = Foes(f).lane
        Foes(f).kind = 0
        Split lane, 1
        EXIT SUB
    END IF
    EnemyFire f
END SUB

' A spiker climbs to its goal, leaving a spike behind, and then backs down;
' at the far end it turns into a flipper.
SUB MoveSpiker (f AS INTEGER)
    DIM l AS INTEGER
    l = Foes(f).lane
    Foes(f).d = Foes(f).d + Foes(f).speed
    IF Foes(f).speed > 0 THEN
        IF Foes(f).d > Spike(l) THEN Spike(l) = Foes(f).d
        IF Foes(f).d >= Foes(f).goal THEN Foes(f).speed = -Foes(f).speed
    ELSEIF Foes(f).d <= 0 THEN
        Foes(f).kind = 0
        l = SpawnFoe%(FOE_FLIPPER, INT(RND * Lanes), 0)
    END IF
END SUB

SUB EnemyFire (f AS INTEGER)
    DIM b AS INTEGER
    IF Foes(f).d > .8 OR RND > .0015 + .0003 * Level THEN EXIT SUB
    FOR b = 0 TO MAX_BULLETS - 1
        IF NOT BulOn(b) THEN
            BulOn(b) = -1: BulLane(b) = FoeLane%(f): BulD(b) = Foes(f).d + .02
            EXIT SUB
        END IF
    NEXT
END SUB

SUB MoveBullets
    DIM b AS INTEGER
    FOR b = 0 TO MAX_BULLETS - 1
        IF BulOn(b) THEN
            BulD(b) = BulD(b) + BULLET_SPEED
            IF BulD(b) >= 1 THEN
                BulOn(b) = 0
                IF BulLane(b) = PlayerLane AND State = ST_PLAY THEN KillPlayer
            END IF
        END IF
    NEXT
END SUB

SUB DrawFoes
    DIM f AS INTEGER, u AS SINGLE, d AS SINGLE
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind THEN
            u = FoeU!(f): d = Foes(f).d
            SELECT CASE Foes(f).kind
                CASE FOE_FLIPPER: DrawFlipper u, d, Foes(f).flipT
                CASE FOE_TANKER: DrawTanker u, d
                CASE FOE_SPIKER: DrawSpiker u, d
            END SELECT
        END IF
    NEXT
END SUB

' A bowtie across the lane; it narrows and lifts as it flips over.
SUB DrawFlipper (u AS SINGLE, d AS SINGLE, t AS SINGLE)
    DIM w AS SINGLE, lift AS SINGLE, x1 AS SINGLE, y1 AS SINGLE, x2 AS SINGLE, y2 AS SINGLE
    DIM x3 AS SINGLE, y3 AS SINGLE, x4 AS SINGLE, y4 AS SINGLE
    w = .45 * (.35 + .65 * ABS(COS(_PI * t)))
    lift = .05 * SIN(_PI * t)
    Project u - w, d + lift: x1 = PrX: y1 = PrY
    Project u + w, d + lift: x2 = PrX: y2 = PrY
    Project u - w, d + lift + .035: x3 = PrX: y3 = PrY
    Project u + w, d + lift + .035: x4 = PrX: y4 = PrY
    GlowLine x1, y1, x4, y4, RED
    GlowLine x4, y4, x2, y2, RED
    GlowLine x2, y2, x3, y3, RED
    GlowLine x3, y3, x1, y1, RED
END SUB

SUB DrawTanker (u AS SINGLE, d AS SINGLE)
    DIM k AS INTEGER, s AS SINGLE, x(3) AS SINGLE, y(3) AS SINGLE
    FOR k = 1 TO 2
        s = k / 2
        Project u - .4 * s, d + .025: x(0) = PrX: y(0) = PrY
        Project u, d + .025 + .03 * s: x(1) = PrX: y(1) = PrY
        Project u + .4 * s, d + .025: x(2) = PrX: y(2) = PrY
        Project u, d + .025 - .03 * s: x(3) = PrX: y(3) = PrY
        GlowLine x(0), y(0), x(1), y(1), PURPLE
        GlowLine x(1), y(1), x(2), y(2), PURPLE
        GlowLine x(2), y(2), x(3), y(3), PURPLE
        GlowLine x(3), y(3), x(0), y(0), PURPLE
    NEXT
END SUB

' A spinning square.
SUB DrawSpiker (u AS SINGLE, d AS SINGLE)
    DIM k AS INTEGER, a AS SINGLE, x(4) AS SINGLE, y(4) AS SINGLE
    FOR k = 0 TO 4
        a = Frame * .25 + k * _PI / 2
        Project u + .3 * COS(a), d + .025 + .025 * SIN(a)
        x(k) = PrX: y(k) = PrY
    NEXT
    FOR k = 0 TO 3
        GlowLine x(k), y(k), x(k + 1), y(k + 1), GREEN
    NEXT
END SUB

SUB DrawSpikes
    DIM l AS INTEGER, x1 AS SINGLE, y1 AS SINGLE
    FOR l = 0 TO Lanes - 1
        IF Spike(l) > 0 THEN
            Project l + .5, 0: x1 = PrX: y1 = PrY
            Project l + .5, Spike(l)
            IF PrOK THEN
                GlowLine x1, y1, PrX, PrY, GREEN
                LINE (PrX - 3, PrY)-(PrX + 3, PrY), WHITE
                LINE (PrX, PrY - 3)-(PrX, PrY + 3), WHITE
            END IF
        END IF
    NEXT
END SUB

SUB DrawShots
    DIM i AS INTEGER, u AS SINGLE, x1 AS SINGLE, y1 AS SINGLE, x2 AS SINGLE, y2 AS SINGLE
    FOR i = 0 TO MAX_SHOTS - 1
        IF ShotOn(i) THEN
            u = ShotLane(i) + .5
            Project u - .14, ShotD(i): x1 = PrX: y1 = PrY
            Project u + .14, ShotD(i): x2 = PrX: y2 = PrY
            Project u, ShotD(i) + .025
            IF PrOK THEN
                LINE (x1, y1)-(PrX, PrY), YELLOW
                LINE (PrX, PrY)-(x2, y2), YELLOW
                Project u, ShotD(i) - .025
                LINE (x2, y2)-(PrX, PrY), WHITE
                LINE (PrX, PrY)-(x1, y1), WHITE
            END IF
        END IF
    NEXT
END SUB

SUB DrawBullets
    DIM b AS INTEGER, u AS SINGLE, x1 AS SINGLE, y1 AS SINGLE
    FOR b = 0 TO MAX_BULLETS - 1
        IF BulOn(b) THEN
            u = BulLane(b) + .5
            Project u - .12, BulD(b): x1 = PrX: y1 = PrY
            Project u + .12, BulD(b)
            LINE (x1, y1)-(PrX, PrY), PINK
            Project u, BulD(b) - .02: x1 = PrX: y1 = PrY
            Project u, BulD(b) + .02
            LINE (x1, y1)-(PrX, PrY), PINK
        END IF
    NEXT
END SUB

' ---------------------------------------------------------------- demo player

' Plays the title screen's demo game: chase the enemy nearest the rim, dodge
' bullets and spikes, fire at anything in the lane and zap a flipper that is
' about to grab.
SUB Autopilot
    DIM f AS INTEGER, b AS INTEGER, target AS INTEGER, best AS SINGLE, dist AS INTEGER, busy AS INTEGER
    InLeft = 0: InRight = 0: InFire = 0
    IF State = ST_OVER OR State = ST_DYING THEN EXIT SUB
    target = PlayerLane: best = -1
    FOR f = 0 TO MAX_FOES
        IF Foes(f).kind THEN
            IF Foes(f).d > best THEN best = Foes(f).d: target = FoeLane%(f)
            IF Foes(f).lane = PlayerLane OR FoeLane%(f) = PlayerLane THEN busy = -1
            IF Foes(f).kind = FOE_FLIPPER AND Foes(f).d >= 1 AND ZapUses = 0 THEN
                IF ABS(LaneDist%(FoeLane%(f), PlayerLane)) <= 1 AND RND < .3 THEN InZap = -1
            END IF
        END IF
    NEXT
    IF State = ST_WARP THEN
        ' Steer to the lane with the lowest spike nearby.
        IF Spike(PlayerLane) > PlayerD - .3 THEN
            IF Spike(StepLane%(PlayerLane, 1)) < Spike(StepLane%(PlayerLane, -1)) THEN
                target = StepLane%(PlayerLane, 1)
            ELSE
                target = StepLane%(PlayerLane, -1)
            END IF
        END IF
    END IF
    FOR b = 0 TO MAX_BULLETS - 1
        IF BulOn(b) AND BulLane(b) = PlayerLane THEN
            busy = -1
            IF BulD(b) > .82 THEN
                target = StepLane%(PlayerLane, 1)
                IF target = PlayerLane THEN target = StepLane%(PlayerLane, -1)
            END IF
        END IF
    NEXT
    IF Spike(PlayerLane) > 0 THEN busy = -1
    dist = LaneDist%(PlayerLane, target)
    IF dist < 0 THEN InLeft = -1
    IF dist > 0 THEN InRight = -1
    ' The demo taps fire rather than holding it.
    IF (busy OR RND < .05) AND Frame MOD 4 < 2 THEN InFire = -1
END SUB

' ---------------------------------------------------------------- effects

SUB Burst (x AS SINGLE, y AS SINGLE, n AS INTEGER, c AS _UNSIGNED LONG, speed AS SINGLE)
    DIM i AS INTEGER, k AS INTEGER, a AS SINGLE, v AS SINGLE
    FOR i = 0 TO MAX_PARTS
        IF PtLife(i) <= 0 THEN
            a = RND * _PI(2): v = speed * (.3 + RND)
            PtX(i) = x: PtY(i) = y
            PtVX(i) = v * COS(a): PtVY(i) = v * SIN(a)
            PtLife(i) = 25 + INT(RND * 25): PtColor(i) = c
            k = k + 1
            IF k >= n THEN EXIT SUB
        END IF
    NEXT
END SUB

SUB UpdateParticles
    DIM i AS INTEGER
    FOR i = 0 TO MAX_PARTS
        IF PtLife(i) > 0 THEN
            PtX(i) = PtX(i) + PtVX(i): PtY(i) = PtY(i) + PtVY(i)
            PtVX(i) = PtVX(i) * .97: PtVY(i) = PtVY(i) * .97
            PtLife(i) = PtLife(i) - 1
        END IF
    NEXT
END SUB

SUB DrawParticles
    DIM i AS INTEGER
    FOR i = 0 TO MAX_PARTS
        IF PtLife(i) > 0 THEN
            LINE (PtX(i), PtY(i))-(PtX(i) - PtVX(i) * 2, PtY(i) - PtVY(i) * 2), PtColor(i)
        END IF
    NEXT
END SUB

SUB ResetStar (i AS INTEGER)
    StarX(i) = (RND - .5) * 1800
    StarY(i) = (RND - .5) * 1400
    StarZ(i) = ZFAR
END SUB

SUB UpdateStars (speed AS SINGLE)
    DIM i AS INTEGER
    FOR i = 0 TO MAX_STARS
        StarZ(i) = StarZ(i) - speed
        IF StarZ(i) < .3 THEN ResetStar i
    NEXT
END SUB

SUB DrawStars
    DIM i AS INTEGER, z2 AS SINGLE
    FOR i = 0 TO MAX_STARS
        z2 = StarZ(i) + .25
        LINE (CX + StarX(i) / StarZ(i), CY + StarY(i) / StarZ(i))-(CX + StarX(i) / z2, CY + StarY(i) / z2), _RGB32(150, 150, 220)
    NEXT
END SUB

' ---------------------------------------------------------------- screen

SUB Render
    CLS
    IF State = ST_WARP OR State = ST_ZOOM THEN DrawStars
    DrawTube
    DrawSpikes
    DrawFoes
    DrawBullets
    DrawShots
    IF State <> ST_DYING AND State <> ST_OVER THEN DrawClaw PlayerLane, PlayerD, YELLOW
    DrawParticles
    DrawHud
    IF Demo THEN
        LINE (0, 180)-(SW - 1, 425), _RGBA32(0, 0, 0, 170), BF
        VTextC "VORTEX", 200, 14, TubeColor
        COLOR WHITE
        IF (Frame \ 30) MOD 2 = 0 THEN CenterText "PRESS SPACE TO PLAY", 330
        IF STICK(0) THEN CenterText "JOYSTICK: STICK MOVES   BUTTON 1 FIRE   BUTTON 2 SUPERZAPPER", 356
        CenterText "LEFT/RIGHT MOVE   SPACE FIRE (TAP TO FIRE FASTER)   Z SUPERZAPPER   P PAUSE", 380
        IF FullPref THEN
            CenterText "F FULLSCREEN: ON    ESC QUIT", 404
        ELSE
            CenterText "F FULLSCREEN: OFF    ESC QUIT", 404
        END IF
    ELSEIF State = ST_OVER THEN
        LINE (0, 310)-(SW - 1, 445), _RGBA32(0, 0, 0, 170), BF
        VTextC "GAME OVER", 330, 9, RED
        COLOR WHITE
        CenterText "PRESS SPACE TO PLAY AGAIN", 420
    ELSEIF Paused THEN
        VTextC "PAUSED", 340, 8, WHITE
    ELSEIF State = ST_ZOOM AND CamZ < -1 THEN
        VTextC "LEVEL" + STR$(Level), 360, 6, YELLOW
    END IF
END SUB

SUB DrawHud
    DIM i AS INTEGER, s$
    COLOR YELLOW
    _PRINTSTRING (16, 8), LTRIM$(STR$(Score))
    COLOR WHITE
    s$ = "HI " + LTRIM$(STR$(HiScore))
    _PRINTSTRING ((SW - _PRINTWIDTH(s$)) \ 2, 8), s$
    s$ = "LEVEL" + STR$(Level)
    _PRINTSTRING (SW - 16 - _PRINTWIDTH(s$), 8), s$
    IF ZapUses = 0 AND NOT Demo THEN
        COLOR _RGB32(120, 200, 255)
        s$ = "SUPERZAPPER"
        _PRINTSTRING (SW - 16 - _PRINTWIDTH(s$), 28), s$
    END IF
    ' One small claw for each life after this one.
    FOR i = 1 TO Lives - 1
        LINE (12 + i * 22, 44)-(20 + i * 22, 34), YELLOW
        LINE (20 + i * 22, 34)-(28 + i * 22, 44), YELLOW
        LINE (12 + i * 22, 44)-(28 + i * 22, 44), YELLOW
    NEXT
END SUB

SUB CenterText (t$, y AS INTEGER)
    _PRINTSTRING ((SW - _PRINTWIDTH(t$)) \ 2, y), t$
END SUB

' Large vector letters on a 4 x 6 grid: each stroke is "x1y1x2y2".
FUNCTION Glyph$ (ch$)
    SELECT CASE ch$
        CASE "A": Glyph$ = "060202202042424603430000"
        CASE "C": Glyph$ = "400000060646"
        CASE "E": Glyph$ = "000600400333064600000000"
        CASE "G": Glyph$ = "40000006064646434323"
        CASE "L": Glyph$ = "00060646"
        CASE "M": Glyph$ = "0600002222404046"
        CASE "O": Glyph$ = "0040404646060600"
        CASE "P": Glyph$ = "0600004040434303"
        CASE "R": Glyph$ = "060000404043430313460000"
        CASE "S": Glyph$ = "40000003034343464606"
        CASE "T": Glyph$ = "00402026"
        CASE "U": Glyph$ = "000606464640"
        CASE "V": Glyph$ = "00262640"
        CASE "X": Glyph$ = "00464006"
        CASE "D": Glyph$ = "000600303041414545363606"
        CASE "0": Glyph$ = "00404046460606000640"
        CASE "1": Glyph$ = "202611200626"
        CASE "2": Glyph$ = "004040434303030606460000"
        CASE "3": Glyph$ = "00404046460603330000"
        CASE "4": Glyph$ = "0003034340460000"
        CASE "5": Glyph$ = "40000003034343464606"
        CASE "6": Glyph$ = "4000000606464643430300"
        CASE "7": Glyph$ = "00404026"
        CASE "8": Glyph$ = "0040404646060600034300"
        CASE "9": Glyph$ = "00404046460600030343"
        CASE ELSE: Glyph$ = ""
    END SELECT
END FUNCTION

SUB VText (t$, x AS SINGLE, y AS SINGLE, sc AS SINGLE, c AS _UNSIGNED LONG)
    DIM i AS INTEGER, k AS INTEGER, g$, ox AS SINGLE
    FOR i = 1 TO LEN(t$)
        g$ = Glyph$(MID$(t$, i, 1))
        ox = x + (i - 1) * 6 * sc
        FOR k = 1 TO LEN(g$) - 3 STEP 4
            IF MID$(g$, k, 4) <> "0000" THEN
                GlowLine ox + VAL(MID$(g$, k, 1)) * sc, y + VAL(MID$(g$, k + 1, 1)) * sc, ox + VAL(MID$(g$, k + 2, 1)) * sc, y + VAL(MID$(g$, k + 3, 1)) * sc, c
            END IF
        NEXT
    NEXT
END SUB

SUB VTextC (t$, y AS SINGLE, sc AS SINGLE, c AS _UNSIGNED LONG)
    VText t$, (SW - (LEN(t$) * 6 - 2) * sc) / 2, y, sc, c
END SUB
