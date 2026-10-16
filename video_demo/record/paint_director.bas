' ---- Video recording: replaces SUB SelfTest in a copy of qb64_paint.bas ----
' Paints a picture the way a person would: the pointer glides to the tool
' bar, the palette and the canvas, clicks and drags, and every frame is the
' program's own window (DrawWindow) with an arrow pointer drawn on top.
' Frames go to build/frames/paint/. make_paint.sh puts this file in place.

SUB SelfTest
    PX = 400: PY = 300
    Hold 12
    ' Sky and grass with filled boxes.
    PickTool TOOL_FILLED: PickInk 9
    DragTo 120, 60, 760, 250, 24
    PickInk 7
    DragTo 120, 250, 760, 500, 24
    ' A sun: an oval outline, then the fill bucket.
    PickTool TOOL_ELLIPSE: PickInk 6
    DragTo 600, 80, 690, 170, 18
    PickTool TOOL_BUCKET
    ClickAt 645, 125
    ' A house with a line roof, a door and a window.
    PickTool TOOL_FILLED: PickInk 13
    DragTo 220, 200, 380, 330, 18
    PickTool TOOL_LINE: PickInk 4: SetSize 6
    DragTo 210, 200, 300, 135, 14
    DragTo 300, 135, 390, 200, 14
    PickTool TOOL_RECT: PickInk 0: SetSize 2
    DragTo 280, 260, 320, 330, 12
    PickInk 1: SetSize 5
    DragTo 235, 225, 270, 260, 12
    ' A soft cloud with a half-opacity brush.
    PickTool TOOL_BRUSH: PickInk 1: Opacity = 128: SetSize 26
    Scribble 430, 100, 560, 95, 30
    Opacity = 255
    ' Flowers sprayed on the grass, and a pencil path.
    PickTool TOOL_SPRAY: PickInk 12: SetSize 22
    Scribble 470, 420, 620, 430, 26
    PickInk 5
    Scribble 150, 440, 230, 420, 18
    PickTool TOOL_PENCIL: PickInk 0
    Scribble 300, 330, 470, 480, 26
    ' Mirror the picture and mirror it back (M), then admire it.
    Hold 10
    Notify "Mirror": Reflect 1: Hold 20
    Notify "Mirror": Reflect 1: Hold 30
END SUB

' Draws the window with the pointer at (PX, PY) and saves it as a frame.
SUB Shoot
    DrawWindow PX, PY
    DrawPointer PX, PY
    _DISPLAY
    RecFrame = RecFrame + 1
    SCREENSHOT "build/frames/paint/" + RIGHT$("00000" + LTRIM$(STR$(RecFrame)), 5) + ".png"
END SUB

SUB Hold (n AS INTEGER)
    DIM i AS INTEGER
    FOR i = 1 TO n: Shoot: NEXT
END SUB

' Glides the pointer to (x, y) over n frames, easing in and out. While the
' button is down, each step drags.
SUB GlideTo (x AS INTEGER, y AS INTEGER, n AS INTEGER, down AS INTEGER)
    DIM i AS INTEGER, sx AS INTEGER, sy AS INTEGER, t AS SINGLE
    sx = PX: sy = PY
    FOR i = 1 TO n
        t = i / n: t = t * t * (3 - 2 * t)
        PX = sx + (x - sx) * t: PY = sy + (y - sy) * t
        IF down THEN MouseDragged PX, PY
        Shoot
    NEXT
END SUB

SUB ClickAt (x AS INTEGER, y AS INTEGER)
    GlideTo x, y, 10, 0
    MousePressed PX, PY: Shoot
    MouseReleased PX, PY: Shoot
    Hold 4
END SUB

SUB PickTool (t AS INTEGER)
    ClickAt 34, 40 + t * 50 + 22
END SUB

SUB PickInk (i AS INTEGER)
    ClickAt 135 + (i MOD 8) * 34, ScreenH - 60 + (i \ 8) * 25 + 11
END SUB

SUB SetSize (s AS INTEGER)
    BrushSize = s
END SUB

' Presses at (x1, y1), drags to (x2, y2) over n frames and lets go.
SUB DragTo (x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER, n AS INTEGER)
    GlideTo x1, y1, 10, 0
    MousePressed PX, PY: Shoot
    GlideTo x2, y2, n, -1
    MouseReleased PX, PY: Shoot
    Hold 4
END SUB

' A wavy freehand stroke from (x1, y1) to (x2, y2) over n frames.
SUB Scribble (x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER, n AS INTEGER)
    DIM i AS INTEGER, t AS SINGLE
    GlideTo x1, y1, 10, 0
    MousePressed PX, PY: Shoot
    FOR i = 1 TO n
        t = i / n
        PX = x1 + (x2 - x1) * t
        PY = y1 + (y2 - y1) * t + 14 * SIN(t * 12.566)
        MouseDragged PX, PY
        Shoot
    NEXT
    MouseReleased PX, PY: Shoot
    Hold 4
END SUB

' A classic black-and-white arrow pointer with its tip at (x, y).
SUB DrawPointer (x AS INTEGER, y AS INTEGER)
    DIM r AS INTEGER, w AS INTEGER
    FOR r = 0 TO 15
        w = r \ 2 + 1
        IF r > 11 THEN w = 12 - r + 4
        LINE (x, y + r)-(x + w, y + r), _RGB32(255, 255, 255)
    NEXT
    LINE (x, y)-(x, y + 16), _RGB32(0, 0, 0)
    LINE (x, y)-(x + 10, y + 11), _RGB32(0, 0, 0)
    LINE (x + 10, y + 11)-(x + 5, y + 11), _RGB32(0, 0, 0)
    LINE (x + 5, y + 11)-(x, y + 16), _RGB32(0, 0, 0)
END SUB
