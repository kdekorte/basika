' ============================================================================
'  CHROMATIC FLUX - an animated tour of BASIKA's graphics primitives
' ----------------------------------------------------------------------------
'  Everything is drawn with PSET, LINE, CIRCLE and PAINT into a 32-bit image,
'  where each _RGBA32 color carries its own alpha, so overlapping shapes blend.
'
'    * aurora curtains and RGB orbs      translucent LINE ... BF spans
'    * ringed planet                     CIRCLE arcs with start/end + aspect
'    * comet and sparks                  alpha trails, PSET particles
'    * radar card                        pie wedge (negative angles) + PAINT
'    * line card                         LINE style masks, marching ants
'    * paint card                        PAINT tiles, LINE -STEP, alpha swatches
'
'  Space pauses, any other key exits.   Run with: basika -w <this file>
' ============================================================================

CONST SCREEN_W = 800
CONST SCREEN_H = 600
CONST STAR_COUNT = 80
CONST SPARK_COUNT = 64
CONST TRAIL_LENGTH = 16

CONST PLANET_X = 575
CONST PLANET_Y = 265
CONST PLANET_R = 70
CONST ORBS_X = 190
CONST ORBS_Y = 255

TYPE Star
    x AS SINGLE
    y AS SINGLE
    speed AS SINGLE
    phase AS SINGLE
END TYPE

TYPE Spark
    x AS SINGLE
    y AS SINGLE
    vx AS SINGLE
    vy AS SINGLE
    life AS SINGLE
    tint AS _UNSIGNED LONG
END TYPE

DIM SHARED Stars(1 TO STAR_COUNT) AS Star
DIM SHARED Sparks(1 TO SPARK_COUNT) AS Spark
DIM SHARED TrailX(1 TO TRAIL_LENGTH) AS SINGLE, TrailY(1 TO TRAIL_LENGTH) AS SINGLE
DIM SHARED NextSpark AS INTEGER, AntStyle AS LONG

DIM t AS DOUBLE, dt AS DOUBLE, lastTime AS DOUBLE, now AS DOUBLE
DIM fps AS SINGLE, fpsTime AS DOUBLE, fpsFrames AS INTEGER
DIM paused AS INTEGER, k AS STRING

SCREEN _NEWIMAGE(SCREEN_W, SCREEN_H, 32)
_AUTODISPLAY OFF
RANDOMIZE TIMER
InitScene

lastTime = TIMER
fpsTime = lastTime
DO
    k = INKEY$
    IF k = " " THEN paused = NOT paused

    ' Frame-rate independent time step (TIMER wraps at midnight).
    now = TIMER
    dt = now - lastTime
    IF dt < 0 THEN dt = dt + 86400
    IF dt > .1 THEN dt = .1
    lastTime = now
    IF NOT paused THEN t = t + dt

    fpsFrames = fpsFrames + 1
    IF now - fpsTime >= .5 OR now < fpsTime THEN
        fps = fpsFrames / (now - fpsTime + .0001)
        fpsTime = now
        fpsFrames = 0
    END IF

    DrawSky
    DrawStars t
    DrawAurora t
    DrawRings t, 0
    DrawComet t, 0
    DrawPlanet t
    DrawRings t, 1
    DrawComet t, 1
    IF paused THEN
        UpdateSparks 0
    ELSE
        UpdateSparks dt
    END IF
    DrawOrbs t
    DrawRadarCard t
    DrawLineCard t, paused
    DrawPaintCard t
    DrawHud fps, paused

    _DISPLAY
    _LIMIT 60
LOOP UNTIL (k <> "") AND (k <> " ")
END

SUB InitScene
    FOR i = 1 TO STAR_COUNT
        Stars(i).x = RND * SCREEN_W
        Stars(i).y = 46 + RND * 380
        Stars(i).speed = .2 + RND * RND * 2.5
        Stars(i).phase = RND * _PI(2)
    NEXT
    FOR i = 1 TO TRAIL_LENGTH
        TrailX(i) = PLANET_X
        TrailY(i) = PLANET_Y
    NEXT
    NextSpark = 1
    AntStyle = &HF0F0
END SUB

' A filled circle built from one horizontal span per row, so a translucent
' color covers each pixel exactly once and blends evenly.
SUB FillCircle (cx AS SINGLE, cy AS SINGLE, r AS SINGLE, c AS _UNSIGNED LONG)
    DIM row AS INTEGER, half AS SINGLE
    FOR row = -INT(r) TO INT(r)
        half = SQR(r * r - row * row)
        LINE (cx - half, cy + row)-(cx + half, cy + row), c
    NEXT
END SUB

SUB DrawSky
    DIM y AS INTEGER, f AS SINGLE
    FOR y = 0 TO SCREEN_H - 1 STEP 6
        f = y / SCREEN_H
        LINE (0, y)-(SCREEN_W - 1, y + 5), _RGB32(6 + 26 * f, 4 + 12 * f, 22 + 44 * f), BF
    NEXT
END SUB

SUB DrawStars (t AS DOUBLE)
    DIM i AS INTEGER, x AS SINGLE, a AS INTEGER
    FOR i = 1 TO STAR_COUNT
        ' Parallax drift: faster stars are "closer" and brighter.
        x = Stars(i).x - t * Stars(i).speed * 12
        x = x - SCREEN_W * INT(x / SCREEN_W)
        a = 90 + 80 * Stars(i).speed / 2.7 + 70 * SIN(t * 2.5 + Stars(i).phase)
        PSET (x, Stars(i).y), _RGBA32(220, 230, 255, a)
        IF Stars(i).speed > 1.6 THEN
            PSET (x - 1, Stars(i).y), _RGBA32(160, 190, 255, a \ 3)
            PSET (x + 1, Stars(i).y), _RGBA32(160, 190, 255, a \ 3)
            PSET (x, Stars(i).y - 1), _RGBA32(160, 190, 255, a \ 3)
            PSET (x, Stars(i).y + 1), _RGBA32(160, 190, 255, a \ 3)
        END IF
    NEXT
END SUB

' Three overlapping curtains of 4-pixel translucent columns.
SUB DrawAurora (t AS DOUBLE)
    DIM layer AS INTEGER, x AS INTEGER, top AS SINGLE, h AS SINGLE, a AS SINGLE
    DIM r AS INTEGER, g AS INTEGER, b AS INTEGER
    FOR layer = 0 TO 2
        SELECT CASE layer
            CASE 0: r = 40: g = 255: b = 160
            CASE 1: r = 60: g = 180: b = 255
            CASE ELSE: r = 200: g = 90: b = 255
        END SELECT
        FOR x = 0 TO SCREEN_W - 1 STEP 4
            top = 70 + 35 * SIN(x * .007 + t * .6 + layer * 2.1) + 18 * SIN(x * .023 - t * 1.4 + layer)
            h = 90 + 45 * SIN(x * .011 + t * .8 + layer * 1.3)
            a = 34 + 26 * SIN(x * .031 + t * 1.9 + layer * 1.7)
            LINE (x, top)-(x + 3, top + h * .45), _RGBA32(r, g, b, a), BF
            LINE (x, top + h * .45)-(x + 3, top + h), _RGBA32(r, g, b, a * .45), BF
        NEXT
    NEXT
END SUB

' The ring is a stack of thin ellipses (aspect .28). The far half (angles 0
' to PI) is drawn before the planet and the near half (PI to 2*PI) after it.
SUB DrawRings (t AS DOUBLE, frontHalf AS INTEGER)
    DIM i AS INTEGER, a AS INTEGER, startAngle AS SINGLE, endAngle AS SINGLE
    IF frontHalf THEN
        startAngle = _PI: endAngle = _PI(2)
    ELSE
        startAngle = 0: endAngle = _PI
    END IF
    FOR i = 0 TO 24
        a = 70 + 60 * SIN(i * .8 + t * .3)
        IF i MOD 7 = 3 THEN a = 18
        CIRCLE (PLANET_X, PLANET_Y), 98 + i * 2, _RGBA32(235, 205, 160, a), startAngle, endAngle, .28
    NEXT
END SUB

SUB DrawPlanet (t AS DOUBLE)
    DIM row AS INTEGER, half AS SINGLE, a AS SINGLE, y AS INTEGER, shade AS INTEGER
    FillCircle PLANET_X, PLANET_Y, PLANET_R + 10, _RGBA32(255, 160, 90, 22)
    FillCircle PLANET_X, PLANET_Y, PLANET_R + 4, _RGBA32(255, 170, 100, 40)
    FillCircle PLANET_X, PLANET_Y, PLANET_R, _RGB32(150, 82, 54)
    ' Drifting cloud bands, then a two-step shadow on the night side.
    FOR row = -PLANET_R TO PLANET_R STEP 3
        half = SQR(PLANET_R * PLANET_R - row * row)
        y = PLANET_Y + row
        a = 50 + 45 * SIN(row * .19 + t * .45) * COS(row * .05)
        LINE (PLANET_X - half, y)-(PLANET_X + half, y + 2), _RGBA32(255, 205, 150, a), BF
        ' Stacked translucent spans darken gradually toward the night side.
        FOR shade = 0 TO 4
            LINE (PLANET_X + half * (shade * .2 - .05) - row * .2, y)-(PLANET_X + half, y + 2), _RGBA32(10, 0, 30, 48), BF
        NEXT
    NEXT
    FillCircle PLANET_X - 26, PLANET_Y - 28, 22, _RGBA32(255, 240, 210, 26)
    FillCircle PLANET_X - 30, PLANET_Y - 32, 10, _RGBA32(255, 250, 230, 40)
END SUB

' The comet circles the planet; frontHalf selects the part of the orbit drawn
' in front of the planet so it passes behind and in front of it.
SUB DrawComet (t AS DOUBLE, frontHalf AS INTEGER)
    DIM angle AS SINGLE, cx AS SINGLE, cy AS SINGLE, i AS INTEGER, f AS SINGLE
    angle = t * .7
    cx = PLANET_X + 235 * COS(angle)
    cy = PLANET_Y + 62 * SIN(angle)
    IF (SIN(angle) > 0) <> (frontHalf <> 0) THEN EXIT SUB

    ' Only one of the two passes gets here each frame; the trail and sparks
    ' advance only while the comet is moving.
    IF cx <> TrailX(1) OR cy <> TrailY(1) THEN
        FOR i = TRAIL_LENGTH TO 2 STEP -1
            TrailX(i) = TrailX(i - 1)
            TrailY(i) = TrailY(i - 1)
        NEXT
        TrailX(1) = cx
        TrailY(1) = cy
        SpawnSpark cx, cy
        SpawnSpark cx, cy
    END IF
    FOR i = TRAIL_LENGTH TO 2 STEP -1
        f = 1 - i / TRAIL_LENGTH
        FillCircle TrailX(i), TrailY(i), 2 + 9 * f, _RGBA32(120, 200, 255, 10 + 60 * f)
    NEXT
    FillCircle cx, cy, 14, _RGBA32(140, 210, 255, 50)
    FillCircle cx, cy, 8, _RGBA32(200, 240, 255, 150)
    FillCircle cx, cy, 4, _RGB32(255, 255, 255)
END SUB

SUB SpawnSpark (x AS SINGLE, y AS SINGLE)
    Sparks(NextSpark).x = x
    Sparks(NextSpark).y = y
    Sparks(NextSpark).vx = (RND - .5) * 70
    Sparks(NextSpark).vy = (RND - .8) * 50
    Sparks(NextSpark).life = .6 + RND * .8
    Sparks(NextSpark).tint = _RGB32(180 + RND * 75, 160 + RND * 95, 255)
    NextSpark = NextSpark MOD SPARK_COUNT + 1
END SUB

SUB UpdateSparks (dt AS DOUBLE)
    DIM i AS INTEGER, c AS _UNSIGNED LONG
    FOR i = 1 TO SPARK_COUNT
        IF Sparks(i).life > 0 THEN
            Sparks(i).x = Sparks(i).x + Sparks(i).vx * dt
            Sparks(i).y = Sparks(i).y + Sparks(i).vy * dt
            Sparks(i).vy = Sparks(i).vy + 60 * dt
            Sparks(i).life = Sparks(i).life - dt
            c = Sparks(i).tint
            PSET (Sparks(i).x, Sparks(i).y), _RGBA32(_RED32(c), _GREEN32(c), _BLUE32(c), 255 * Sparks(i).life / 1.4)
        END IF
    NEXT
END SUB

' Red, green and blue discs: where they overlap the alpha blends mix.
SUB DrawOrbs (t AS DOUBLE)
    DIM i AS INTEGER, angle AS SINGLE, x AS SINGLE, y AS SINGLE, pulse AS SINGLE
    DIM c AS _UNSIGNED LONG
    pulse = 46 + 14 * SIN(t * 1.3)
    FOR i = 0 TO 2
        angle = t * .8 + i * _PI(2) / 3
        x = ORBS_X + pulse * COS(angle)
        y = ORBS_Y + pulse * SIN(angle) * .8
        SELECT CASE i
            CASE 0: c = _RGBA32(255, 40, 70, 115)
            CASE 1: c = _RGBA32(40, 255, 110, 115)
            CASE ELSE: c = _RGBA32(60, 110, 255, 115)
        END SELECT
        FillCircle x, y, 72, c
        CIRCLE (x, y), 72, _RGBA32(255, 255, 255, 70)
    NEXT
END SUB

SUB DrawCard (x1 AS INTEGER, title AS STRING)
    LINE (x1, 438)-(x1 + 240, 586), _RGBA32(10, 12, 30, 180), BF
    LINE (x1, 438)-(x1 + 240, 586), _RGBA32(150, 170, 255, 120), B
    COLOR _RGB32(200, 215, 255)
    _PRINTSTRING (x1 + 10, 444), title
END SUB

' A sweeping radar: negative CIRCLE angles draw the wedge's radius lines and
' PAINT floods the wedge with a translucent green.
SUB DrawRadarCard (t AS DOUBLE)
    DIM cx AS INTEGER, cy AS INTEGER, sweep AS SINGLE, i AS INTEGER
    DIM wedgeStart AS SINGLE, mid AS SINGLE, behind AS SINGLE
    DIM edge AS _UNSIGNED LONG
    DrawCard 20, "CIRCLE ARCS + PAINT"
    cx = 140: cy = 524
    edge = _RGB32(90, 255, 140)
    FOR i = 1 TO 3
        CIRCLE (cx, cy), i * 18, _RGBA32(90, 255, 140, 60)
    NEXT
    LINE (cx - 58, cy)-(cx + 58, cy), _RGBA32(90, 255, 140, 60)
    LINE (cx, cy - 58)-(cx, cy + 58), _RGBA32(90, 255, 140, 60)
    sweep = t * 1.6
    sweep = sweep - _PI(2) * INT(sweep / _PI(2))
    wedgeStart = sweep - .7
    ' CIRCLE angles must stay within 2*PI, and negative zero is not possible.
    IF wedgeStart < .01 THEN wedgeStart = wedgeStart + _PI(2)
    IF wedgeStart > _PI(2) THEN wedgeStart = _PI(2)
    IF sweep < .01 THEN sweep = .01
    CIRCLE (cx, cy), 56, edge, -wedgeStart, -sweep
    mid = sweep - .35
    ' VIEW confines PAINT (and its screen read-back) to this card.
    VIEW SCREEN (21, 439)-(259, 585)
    PAINT (cx + 30 * COS(mid), cy - 30 * SIN(mid)), _RGBA32(90, 255, 140, 70), edge
    VIEW
    ' Blips glow when the beam passes and fade as it moves on.
    FOR i = 1 TO 4
        mid = i * 1.7
        behind = sweep - mid
        behind = behind - _PI(2) * INT(behind / _PI(2))
        FillCircle cx + (14 + i * 9) * COS(mid), cy - (14 + i * 9) * SIN(mid), 2, _RGBA32(200, 255, 210, 255 * (1 - behind / _PI(2)))
    NEXT
END SUB

' LINE style masks: each bit of the 16-bit pattern turns a pixel on or off.
' Rotating the pattern every frame makes the box border march.
SUB DrawLineCard (t AS DOUBLE, paused AS INTEGER)
    DIM i AS INTEGER
    DrawCard 280, "LINE STYLE MASKS"
    LINE (300, 484)-(500, 484), _RGB32(255, 170, 130), , &HFF00
    LINE (300, 498)-(500, 498), _RGB32(255, 140, 170), , &HF0F0
    LINE (300, 512)-(500, 512), _RGB32(255, 110, 210), , &HCCCC
    LINE (300, 526)-(500, 526), _RGB32(255, 80, 250), , &HAAAA
    IF NOT paused THEN AntStyle = ((AntStyle * 2) OR (AntStyle \ 32768)) AND &HFFFF&
    LINE (296, 466)-(504, 546), _RGB32(255, 255, 255), B, AntStyle
    ' A sine wave drawn with LINE -STEP from the last point referenced.
    PSET (300, 566), _RGB32(255, 220, 120)
    FOR i = 1 TO 50
        LINE -STEP(4, 7 * SIN(t * 3 + i * .45) - 7 * SIN(t * 3 + (i - 1) * .45)), _RGB32(255, 220, 120)
    NEXT
END SUB

' PAINT with a tile string paints a checkerboard (set bits use the foreground
' color, clear bits the background); translucent swatches show it through.
SUB DrawPaintCard (t AS DOUBLE)
    DIM i AS INTEGER, checker AS STRING, hx AS SINGLE, a AS INTEGER
    DrawCard 540, "PAINT TILES + ALPHA"
    LINE (560, 470)-(760, 510), _RGB32(255, 255, 255), B
    checker = STRING$(4, &HF0) + STRING$(4, &H0F)
    VIEW SCREEN (541, 439)-(779, 585)
    COLOR _RGB32(70, 70, 90), _RGB32(130, 130, 150)
    PAINT (600, 490), checker, _RGB32(255, 255, 255)
    COLOR _RGB32(255, 255, 255), _RGB32(0, 0, 0)
    FOR i = 0 TO 7
        a = 32 + i * 32
        IF a > 255 THEN a = 255
        LINE (563 + i * 25, 473)-(582 + i * 25, 507), _RGBA32(255, 120 + i * 12, 40, a), BF
    NEXT
    ' A hexagon traced with LINE -STEP and flooded with a pulsing PAINT.
    hx = 600
    PSET (hx, 524), _RGB32(120, 220, 255)
    LINE -STEP(20, 0), _RGB32(120, 220, 255)
    LINE -STEP(12, 20), _RGB32(120, 220, 255)
    LINE -STEP(-12, 20), _RGB32(120, 220, 255)
    LINE -STEP(-20, 0), _RGB32(120, 220, 255)
    LINE -STEP(-12, -20), _RGB32(120, 220, 255)
    LINE -STEP(12, -20), _RGB32(120, 220, 255)
    PAINT (hx + 10, 544), _RGBA32(120, 220, 255, 60 + 50 * SIN(t * 3)), _RGB32(120, 220, 255)
    VIEW
    COLOR _RGB32(200, 215, 255)
    _PRINTSTRING (650, 528), "PAINT +"
    _PRINTSTRING (650, 546), "_RGBA32"
END SUB

SUB DrawHud (fps AS SINGLE, paused AS INTEGER)
    LINE (0, 0)-(SCREEN_W - 1, 40), _RGBA32(0, 0, 0, 150), BF
    LINE (0, 40)-(SCREEN_W - 1, 40), _RGBA32(150, 170, 255, 140)
    COLOR _RGB32(255, 255, 255)
    _PRINTSTRING (20, 12), "CHROMATIC FLUX"
    COLOR _RGB32(150, 170, 230)
    _PRINTSTRING (170, 12), "32-bit _RGBA32 alpha  |  PSET  LINE  CIRCLE  PAINT"
    IF paused THEN
        _PRINTSTRING (600, 12), "PAUSED"
    ELSE
        _PRINTSTRING (600, 12), LTRIM$(STR$(INT(fps + .5))) + " FPS"
    END IF
    _PRINTSTRING (20, 414), "SPACE pause   any other key exits"
END SUB
