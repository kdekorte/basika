' ============================================================================
'  BASIKA PAINT - a small MacPaint-style program built on the QB64 extensions
' ----------------------------------------------------------------------------
'  The picture lives in an off-screen image (_NEWIMAGE). Tools draw into it
'  with _DEST, and every frame the window is rebuilt: chrome, the picture
'  (_PUTIMAGE), a live preview of the shape being dragged, and a scaled
'  thumbnail. Undo keeps a _COPYIMAGE snapshot; mirror and flip copy the
'  picture with a reversed _PUTIMAGE rectangle.
'
'  Mouse   left button draws, right button picks up a color, wheel = size
'  Keys    P pencil  B brush  S spray  E eraser  L line  R rectangle
'          F filled rectangle  O ellipse  K fill bucket
'          [ ] brush size   A opacity   U undo   C clear
'          M mirror   V flip   W save screenshot   Q quit
'          hold Shift for straight lines, squares and circles
'
'  Run with: basika -w demo/qb64_paint.bas
'  (basika --headless demo/qb64_paint.bas --selftest draws a sample picture,
'   saves basika_paint_selftest.png and exits.)
' ============================================================================

CONST SCREEN_W = 800
CONST SCREEN_H = 600
CONST CANVAS_X = 72
CONST CANVAS_Y = 32
CONST CANVAS_W = 712
CONST CANVAS_H = 496
CONST TOOL_COUNT = 9
CONST TOOL_PENCIL = 0
CONST TOOL_BRUSH = 1
CONST TOOL_SPRAY = 2
CONST TOOL_ERASER = 3
CONST TOOL_LINE = 4
CONST TOOL_RECT = 5
CONST TOOL_FILLED = 6
CONST TOOL_ELLIPSE = 7
CONST TOOL_BUCKET = 8
CONST SHIFT_KEY = 100304
CONST RIGHT_SHIFT_KEY = 100303

DIM SHARED Swatches(15) AS _UNSIGNED LONG
DIM SHARED ToolName(TOOL_COUNT - 1) AS STRING
DIM SHARED ToolKey(TOOL_COUNT - 1) AS STRING
DIM SHARED Picture AS LONG, UndoImage AS LONG
DIM SHARED Tool AS INTEGER, BrushSize AS INTEGER, Opacity AS INTEGER
DIM SHARED Ink AS _UNSIGNED LONG
DIM SHARED Dragging AS INTEGER, StartX AS INTEGER, StartY AS INTEGER
DIM SHARED LastX AS INTEGER, LastY AS INTEGER
DIM SHARED AntPhase AS LONG, Message AS STRING, MessageTime AS DOUBLE

SCREEN _NEWIMAGE(SCREEN_W, SCREEN_H, 32)
_TITLE "BASIKA Paint"
_AUTODISPLAY OFF
RANDOMIZE TIMER
InitPaint

IF COMMAND$ = "--selftest" THEN
    SelfTest
    SYSTEM
END IF

DIM mx AS INTEGER, my AS INTEGER, leftDown AS INTEGER, rightDown AS INTEGER
DIM wasDown AS INTEGER, done AS INTEGER, k AS LONG
DO
    WHILE _MOUSEINPUT: WEND
    mx = _MOUSEX: my = _MOUSEY
    leftDown = _MOUSEBUTTON(1): rightDown = _MOUSEBUTTON(2)
    ChangeSize -_MOUSEWHEEL

    k = _KEYHIT
    WHILE k <> 0
        IF k > 0 THEN done = HandleKey(k) OR done
        k = _KEYHIT
    WEND

    IF leftDown AND NOT wasDown THEN MousePressed mx, my
    IF leftDown AND wasDown THEN MouseDragged mx, my
    IF wasDown AND NOT leftDown THEN MouseReleased mx, my
    IF rightDown AND InCanvas(mx, my) THEN PickColor mx - CANVAS_X, my - CANVAS_Y
    wasDown = leftDown

    DrawWindow mx, my
    _DISPLAY
    _LIMIT 60
LOOP UNTIL done
SYSTEM

SUB InitPaint
    DIM i AS INTEGER, r AS INTEGER, g AS INTEGER, b AS INTEGER
    FOR i = 0 TO 15
        READ r, g, b
        Swatches(i) = _RGB32(r, g, b)
    NEXT
    FOR i = 0 TO TOOL_COUNT - 1
        READ ToolName(i), ToolKey(i)
    NEXT
    Picture = _NEWIMAGE(CANVAS_W, CANVAS_H, 32)
    _DEST Picture
    LINE (0, 0)-(CANVAS_W - 1, CANVAS_H - 1), _RGB32(255, 255, 255), BF
    _DEST 0
    UndoImage = _COPYIMAGE(Picture)
    Tool = TOOL_BRUSH
    BrushSize = 8
    Opacity = 255
    Ink = Swatches(0)
    AntPhase = &HF0F0&
END SUB

' Classic 16-color palette.
DATA 0,0,0, 255,255,255, 128,128,128, 192,192,192
DATA 200,30,40, 255,140,0, 250,220,40, 60,170,70
DATA 30,110,60, 40,160,220, 30,60,170, 120,60,180
DATA 230,110,170, 140,90,50, 255,210,170, 20,40,60
' Tool names and shortcut keys.
DATA "Pencil", "P", "Brush", "B", "Spray", "S", "Erase", "E", "Line", "L"
DATA "Rect", "R", "Box", "F", "Oval", "O", "Fill", "K"
' Key reminders beside the thumbnail.
HelpText:
DATA "[ ] size   A opacity", "U undo  M mirror  V flip", "W save  C clear  Q quit"

FUNCTION InCanvas (x AS INTEGER, y AS INTEGER)
    InCanvas = x >= CANVAS_X AND x < CANVAS_X + CANVAS_W AND y >= CANVAS_Y AND y < CANVAS_Y + CANVAS_H
END FUNCTION

FUNCTION ShiftHeld
    ShiftHeld = _KEYDOWN(SHIFT_KEY) OR _KEYDOWN(RIGHT_SHIFT_KEY)
END FUNCTION

' The ink color with the current opacity in its alpha byte.
FUNCTION InkColor~& ()
    InkColor~& = _RGBA32(_RED32(Ink), _GREEN32(Ink), _BLUE32(Ink), Opacity)
END FUNCTION

SUB Notify (text AS STRING)
    Message = text
    MessageTime = TIMER
END SUB

SUB ChangeSize (delta AS INTEGER)
    IF delta = 0 THEN EXIT SUB
    BrushSize = BrushSize + delta
    IF BrushSize < 1 THEN BrushSize = 1
    IF BrushSize > 64 THEN BrushSize = 64
END SUB

FUNCTION HandleKey (code AS LONG)
    DIM c AS STRING, i AS INTEGER
    HandleKey = 0
    c = UCASE$(CHR$(code MOD 256))
    IF code > 255 THEN EXIT FUNCTION
    FOR i = 0 TO TOOL_COUNT - 1
        IF c = ToolKey(i) THEN Tool = i: EXIT FUNCTION
    NEXT
    SELECT CASE c
        CASE "[": ChangeSize -2
        CASE "]": ChangeSize 2
        CASE "A"
            IF Opacity = 255 THEN
                Opacity = 128
            ELSEIF Opacity = 128 THEN
                Opacity = 48
            ELSE
                Opacity = 255
            END IF
        CASE "U": Undo
        CASE "C"
            SaveUndo
            _DEST Picture
            LINE (0, 0)-(CANVAS_W - 1, CANVAS_H - 1), _RGB32(255, 255, 255), BF
            _DEST 0
            Notify "Cleared (U to undo)"
        CASE "M": Reflect 1
        CASE "V": Reflect 0
        CASE "W"
            SCREENSHOT "basika_paint.png"
            Notify "Saved basika_paint.png"
        CASE "Q": HandleKey = -1
    END SELECT
END FUNCTION

SUB SaveUndo
    _PUTIMAGE (0, 0), Picture, UndoImage
END SUB

' Swap the picture and the undo copy, so U toggles undo/redo.
SUB Undo
    DIM spare AS LONG
    spare = _COPYIMAGE(Picture)
    _PUTIMAGE (0, 0), UndoImage, Picture
    _PUTIMAGE (0, 0), spare, UndoImage
    _FREEIMAGE spare
    Notify "Undo"
END SUB

' Mirror (horizontal) or flip (vertical) with a reversed destination rectangle.
SUB Reflect (horizontal AS INTEGER)
    DIM spare AS LONG
    SaveUndo
    spare = _COPYIMAGE(Picture)
    IF horizontal THEN
        _PUTIMAGE (CANVAS_W - 1, 0)-(0, CANVAS_H - 1), spare, Picture
    ELSE
        _PUTIMAGE (0, CANVAS_H - 1)-(CANVAS_W - 1, 0), spare, Picture
    END IF
    _FREEIMAGE spare
END SUB

SUB PickColor (x AS INTEGER, y AS INTEGER)
    _SOURCE Picture
    Ink = POINT(x, y) OR &HFF000000 ' Ink is _UNSIGNED LONG, so the value wraps to unsigned
    _SOURCE 0
END SUB

SUB MousePressed (mx AS INTEGER, my AS INTEGER)
    DIM i AS INTEGER, cx AS INTEGER, cy AS INTEGER
    ' Toolbar and palette clicks.
    IF mx < CANVAS_X - 4 THEN
        i = (my - 40) \ 50
        IF i >= 0 AND i < TOOL_COUNT THEN Tool = i
        EXIT SUB
    END IF
    IF my >= 540 AND my < 590 AND mx >= 120 AND mx < 120 + 8 * 34 THEN
        i = (mx - 120) \ 34 + 8 * ((my - 540) \ 25)
        IF i >= 0 AND i < 16 THEN Ink = Swatches(i)
        EXIT SUB
    END IF
    IF NOT InCanvas(mx, my) THEN EXIT SUB
    cx = mx - CANVAS_X: cy = my - CANVAS_Y
    SaveUndo
    Dragging = -1
    StartX = cx: StartY = cy: LastX = cx: LastY = cy
    SELECT CASE Tool
        CASE TOOL_BUCKET: BucketFill cx, cy: Dragging = 0
        CASE TOOL_PENCIL, TOOL_BRUSH, TOOL_ERASER, TOOL_SPRAY: Stroke cx, cy
    END SELECT
END SUB

SUB MouseDragged (mx AS INTEGER, my AS INTEGER)
    IF NOT Dragging THEN EXIT SUB
    DIM cx AS INTEGER, cy AS INTEGER
    cx = mx - CANVAS_X: cy = my - CANVAS_Y
    SELECT CASE Tool
        CASE TOOL_PENCIL, TOOL_BRUSH, TOOL_ERASER, TOOL_SPRAY: Stroke cx, cy
    END SELECT
END SUB

SUB MouseReleased (mx AS INTEGER, my AS INTEGER)
    IF NOT Dragging THEN EXIT SUB
    Dragging = 0
    IF Tool >= TOOL_LINE AND Tool <= TOOL_ELLIPSE THEN
        _DEST Picture
        DrawShape StartX, StartY, mx - CANVAS_X, my - CANVAS_Y, InkColor~&(), 0
        _DEST 0
    END IF
END SUB

' Freehand tools draw from the last mouse position to the new one.
SUB Stroke (x AS INTEGER, y AS INTEGER)
    DIM steps AS INTEGER, i AS INTEGER, px AS SINGLE, py AS SINGLE, r AS INTEGER, angle AS SINGLE
    _DEST Picture
    SELECT CASE Tool
        CASE TOOL_PENCIL
            LINE (LastX, LastY)-(x, y), InkColor~&()
        CASE TOOL_SPRAY
            FOR i = 1 TO BrushSize * 3
                angle = RND * _PI(2)
                r = RND * BrushSize
                PSET (x + r * COS(angle), y + r * SIN(angle)), InkColor~&()
            NEXT
        CASE ELSE
            steps = (ABS(x - LastX) + ABS(y - LastY)) \ (BrushSize \ 3 + 1) + 1
            FOR i = 1 TO steps
                px = LastX + (x - LastX) * i / steps
                py = LastY + (y - LastY) * i / steps
                IF Tool = TOOL_ERASER THEN
                    FillCircle px, py, BrushSize / 2, _RGB32(255, 255, 255)
                ELSE
                    FillCircle px, py, BrushSize / 2, InkColor~&()
                END IF
            NEXT
    END SELECT
    _DEST 0
    LastX = x: LastY = y
END SUB

SUB FillCircle (cx AS SINGLE, cy AS SINGLE, r AS SINGLE, c AS _UNSIGNED LONG)
    DIM row AS INTEGER, half AS SINGLE
    IF r < 1 THEN PSET (cx, cy), c: EXIT SUB
    FOR row = -INT(r) TO INT(r)
        half = SQR(r * r - row * row)
        LINE (cx - half, cy + row)-(cx + half, cy + row), c
    NEXT
END SUB

' Lines, rectangles and ellipses, in picture coordinates; preview draws
' marching ants for boxes. Shift keeps lines straight and shapes square.
SUB DrawShape (x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER, c AS _UNSIGNED LONG, preview AS INTEGER)
    DIM w AS INTEGER, h AS INTEGER, side AS INTEGER, rx AS SINGLE, ry AS SINGLE
    w = x2 - x1: h = y2 - y1
    IF ShiftHeld THEN
        IF Tool = TOOL_LINE THEN
            IF ABS(w) > ABS(h) THEN y2 = y1 ELSE x2 = x1
        ELSE
            side = ABS(w)
            IF ABS(h) < side THEN side = ABS(h)
            x2 = x1 + SGN(w) * side: y2 = y1 + SGN(h) * side
        END IF
    END IF
    SELECT CASE Tool
        CASE TOOL_LINE
            IF BrushSize <= 2 THEN
                LINE (x1, y1)-(x2, y2), c
            ELSE
                ThickLine x1, y1, x2, y2, c
            END IF
        CASE TOOL_RECT
            IF preview THEN
                LINE (x1, y1)-(x2, y2), c, B, AntPhase
            ELSE
                LINE (x1, y1)-(x2, y2), c, B
            END IF
        CASE TOOL_FILLED
            LINE (x1, y1)-(x2, y2), c, BF
            IF preview THEN LINE (x1, y1)-(x2, y2), _RGB32(0, 0, 0), B, AntPhase
        CASE TOOL_ELLIPSE
            rx = ABS(x2 - x1) / 2: ry = ABS(y2 - y1) / 2
            IF rx < 1 THEN rx = 1
            IF ry < 1 THEN ry = 1
            IF rx >= ry THEN
                CIRCLE ((x1 + x2) / 2, (y1 + y2) / 2), rx, c, , , ry / rx
            ELSE
                CIRCLE ((x1 + x2) / 2, (y1 + y2) / 2), ry, c, , , ry / rx
            END IF
    END SELECT
END SUB

SUB ThickLine (x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER, c AS _UNSIGNED LONG)
    DIM steps AS INTEGER, i AS INTEGER
    steps = (ABS(x2 - x1) + ABS(y2 - y1)) \ (BrushSize \ 3 + 1) + 1
    FOR i = 0 TO steps
        FillCircle x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps, BrushSize / 2, c
    NEXT
END SUB

' PAINT fills up to a border color, so the fill bucket uses the first color
' to the right of the click as the border of the region.
SUB BucketFill (x AS INTEGER, y AS INTEGER)
    DIM inside AS _UNSIGNED LONG, border AS _UNSIGNED LONG, opaque AS _UNSIGNED LONG, scan AS INTEGER
    opaque = InkColor~&() OR &HFF000000 ' assigning wraps the LONG back to unsigned
    _SOURCE Picture
    inside = POINT(x, y)
    border = opaque
    FOR scan = x + 1 TO CANVAS_W - 1
        IF POINT(scan, y) <> inside THEN border = POINT(scan, y): EXIT FOR
    NEXT
    _SOURCE 0
    IF inside = opaque THEN EXIT SUB
    _DEST Picture
    PAINT (x, y), InkColor~&(), border
    _DEST 0
END SUB

' ---- Drawing the window ----

SUB DrawWindow (mx AS INTEGER, my AS INTEGER)
    DIM i AS INTEGER, y AS INTEGER, label AS STRING
    LINE (0, 0)-(SCREEN_W - 1, SCREEN_H - 1), _RGB32(205, 210, 220), BF
    ' The picture, then the shape being dragged drawn over it as a preview.
    _PUTIMAGE (CANVAS_X, CANVAS_Y), Picture
    LINE (CANVAS_X - 1, CANVAS_Y - 1)-(CANVAS_X + CANVAS_W, CANVAS_Y + CANVAS_H), _RGB32(40, 40, 50), B
    AntPhase = ((AntPhase * 2) OR (AntPhase \ 32768)) AND &HFFFF&
    IF AntPhase = 0 THEN AntPhase = &HF0F0&
    IF Dragging AND Tool >= TOOL_LINE AND Tool <= TOOL_ELLIPSE THEN
        VIEW SCREEN (CANVAS_X, CANVAS_Y)-(CANVAS_X + CANVAS_W - 1, CANVAS_Y + CANVAS_H - 1)
        WINDOW SCREEN (0, 0)-(CANVAS_W - 1, CANVAS_H - 1) ' picture coordinates
        DrawShape StartX, StartY, mx - CANVAS_X, my - CANVAS_Y, InkColor~&(), -1
        WINDOW
        VIEW
    END IF

    ' Title bar and status line.
    LINE (0, 0)-(SCREEN_W - 1, 24), _RGB32(40, 44, 60), BF
    COLOR _RGB32(255, 255, 255)
    _PRINTSTRING (10, 4), "BASIKA Paint"
    COLOR _RGB32(170, 190, 230)
    label = ToolName(Tool) + "   size" + STR$(BrushSize) + "   opacity" + STR$(INT(Opacity * 100 / 255 + .5)) + "%"
    IF InCanvas(mx, my) THEN label = label + "   (" + LTRIM$(STR$(mx - CANVAS_X)) + "," + LTRIM$(STR$(my - CANVAS_Y)) + ")"
    _PRINTSTRING (150, 4), label
    IF Message <> "" AND TIMER - MessageTime < 2 AND TIMER >= MessageTime THEN
        COLOR _RGB32(255, 220, 120)
        _PRINTSTRING (SCREEN_W - 10 - _PRINTWIDTH(Message), 4), Message
    END IF

    ' Toolbar.
    FOR i = 0 TO TOOL_COUNT - 1
        y = 40 + i * 50
        IF i = Tool THEN
            LINE (3, y)-(66, y + 44), _RGB32(60, 90, 160), BF
            COLOR _RGB32(255, 255, 255)
        ELSE
            LINE (3, y)-(66, y + 44), _RGB32(235, 238, 245), BF
            COLOR _RGB32(40, 40, 50)
        END IF
        LINE (3, y)-(66, y + 44), _RGB32(120, 125, 140), B
        DrawToolIcon i, 34, y + 16
        _PRINTSTRING (34 - _PRINTWIDTH(ToolName(i)) \ 2, y + 28), ToolName(i)
    NEXT

    ' Palette, current color and the brush.
    COLOR _RGB32(40, 40, 50)
    _PRINTSTRING (12, 544), "Ink"
    LINE (12, 562)-(100, 590), Ink, BF
    LINE (12, 562)-(100, 590), _RGB32(40, 40, 50), B
    FOR i = 0 TO 15
        LINE (120 + (i MOD 8) * 34, 540 + (i \ 8) * 25)-(150 + (i MOD 8) * 34, 562 + (i \ 8) * 25), Swatches(i), BF
        LINE (120 + (i MOD 8) * 34, 540 + (i \ 8) * 25)-(150 + (i MOD 8) * 34, 562 + (i \ 8) * 25), _RGB32(40, 40, 50), B
    NEXT
    FillCircle 420, 565, BrushSize / 2, InkColor~&()
    RESTORE HelpText
    FOR i = 0 TO 2
        READ label
        _PRINTSTRING (690 - _PRINTWIDTH(label), 539 + i * 19), label
    NEXT

    ' A scaled-down thumbnail of the whole picture.
    _PUTIMAGE (700, 538)-(779, 593), Picture
    LINE (699, 537)-(780, 594), _RGB32(40, 40, 50), B

    ' Brush outline under the mouse.
    IF InCanvas(mx, my) AND Tool <> TOOL_BUCKET THEN
        CIRCLE (mx, my), BrushSize / 2 + 1, _RGBA32(0, 0, 0, 140)
    END IF
END SUB

SUB DrawToolIcon (index AS INTEGER, x AS INTEGER, y AS INTEGER)
    DIM c AS _UNSIGNED LONG
    c = _RGB32(30, 30, 40)
    IF index = Tool THEN c = _RGB32(255, 255, 255)
    SELECT CASE index
        CASE TOOL_PENCIL: LINE (x - 8, y + 6)-(x + 8, y - 6), c
        CASE TOOL_BRUSH: FillCircle x, y, 6, c
        CASE TOOL_SPRAY
            FOR i = 1 TO 14: PSET (x - 7 + RND * 14, y - 7 + RND * 14), c: NEXT
        CASE TOOL_ERASER: LINE (x - 8, y - 5)-(x + 8, y + 5), c, B
        CASE TOOL_LINE: LINE (x - 9, y + 6)-(x + 9, y - 6), c
        CASE TOOL_RECT: LINE (x - 9, y - 6)-(x + 9, y + 6), c, B
        CASE TOOL_FILLED: LINE (x - 9, y - 6)-(x + 9, y + 6), c, BF
        CASE TOOL_ELLIPSE: CIRCLE (x, y), 9, c, , , .65
        CASE TOOL_BUCKET
            LINE (x - 6, y - 4)-(x + 4, y + 7), c, B
            LINE (x + 4, y - 2)-(x + 9, y + 4), c
    END SELECT
END SUB

' ---- Self test: draw a sample picture with every tool, then save it ----

SUB SelfTest
    Ink = Swatches(9): Tool = TOOL_FILLED
    MousePressed 120, 60: MouseReleased 760, 260          ' sky
    Ink = Swatches(7): Tool = TOOL_FILLED
    MousePressed 120, 260: MouseReleased 760, 500         ' grass
    Ink = Swatches(6): Tool = TOOL_ELLIPSE
    MousePressed 600, 80: MouseReleased 680, 160          ' sun outline
    Tool = TOOL_BUCKET: MousePressed 640, 120             ' fill the sun
    Ink = Swatches(13): Tool = TOOL_FILLED
    MousePressed 220, 200: MouseReleased 380, 330         ' house
    Ink = Swatches(4): Tool = TOOL_LINE: BrushSize = 6
    MousePressed 210, 200: MouseReleased 300, 140         ' roof
    MousePressed 300, 140: MouseReleased 390, 200
    Ink = Swatches(0): Tool = TOOL_RECT: BrushSize = 2
    MousePressed 280, 260: MouseReleased 320, 330         ' door
    Ink = Swatches(1): Opacity = 128: Tool = TOOL_BRUSH: BrushSize = 24
    MousePressed 450, 90: MouseDragged 500, 80: MouseDragged 550, 95: MouseReleased 550, 95
    Opacity = 255: Ink = Swatches(15): Tool = TOOL_SPRAY: BrushSize = 20
    MousePressed 500, 420: MouseDragged 520, 430: MouseDragged 540, 420: MouseReleased 540, 420
    Ink = Swatches(0): Tool = TOOL_PENCIL
    MousePressed 600, 380: MouseDragged 650, 350: MouseDragged 700, 390: MouseReleased 700, 390
    ' Mirror, flip and undo twice each: the picture ends where it started.
    Reflect 1: Reflect 1: Reflect 0: Reflect 0
    CALL Undo: CALL Undo                                   ' (a bare "Undo:" would be a label)
    Tool = TOOL_RECT: Dragging = -1: StartX = 420: StartY = 300
    DrawWindow 700, 470                                    ' shows a dragged preview
    _DISPLAY
    SCREENSHOT "basika_paint_selftest.png"
END SUB
