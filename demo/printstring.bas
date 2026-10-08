' ============================================================================
'  PRINTSTRING DEMO - text drawn anywhere, in any font, with QB64 extensions
' ----------------------------------------------------------------------------
'  _LOADFONT loads TrueType fonts (with BOLD, ITALIC and UNDERLINE styles),
'  _FONT picks one, _PRINTSTRING draws text at a pixel position and
'  _PRINTWIDTH measures it, which is all it takes to center and right-align
'  text, draw shadows and outlines, and move single letters around.
'  _PRINTMODE _KEEPBACKGROUND draws only the letters, not the COLOR
'  background box behind them.
'
'  Fonts that are not installed (the gallery tries macOS and Linux paths)
'  are listed as missing instead of stopping the program.
'
'  Run with: basika -w demo/printstring.bas        (any key quits)
'  (basika --headless demo/printstring.bas --selftest saves
'   tests/printstring.png and exits.)
' ============================================================================

CONST SCREEN_W = 800
CONST SCREEN_H = 600
CONST DEFAULT_FONT = 16 ' QB64's handle for the built-in 8x16 font
CONST BUNDLED = "fonts/ModernDOS8x16.ttf"
CONST TICKER = "   _PRINTSTRING draws text at any pixel   *   _PRINTWIDTH measures it   *   _LOADFONT loads TrueType fonts with BOLD, ITALIC and UNDERLINE   *   _FONT switches between them   *"

DIM SHARED TitleFont AS LONG, LabelFont AS LONG, WaveFont AS LONG
DIM SHARED Gallery(5) AS LONG, GalleryName(5) AS STRING
DIM SHARED Styled(3) AS LONG
DIM SHARED Backdrop AS LONG

SCREEN _NEWIMAGE(SCREEN_W, SCREEN_H, 32)
_PRINTMODE _KEEPBACKGROUND ' draw only the letters, so shadows and outlines show
_TITLE "BASIKA _PRINTSTRING demo"
_AUTODISPLAY OFF
LoadFonts
DrawBackdrop

DIM frame AS LONG, k AS LONG
DO
    _PUTIMAGE (0, 0), Backdrop
    DrawWave 290, frame
    DrawTypewriter 340, frame
    DrawTicker frame
    _DISPLAY
    IF COMMAND$ = "--selftest" AND frame = 90 THEN
        SCREENSHOT "tests/printstring.png"
        EXIT DO
    END IF
    frame = frame + 1
    _LIMIT 60
    k = _KEYHIT
LOOP UNTIL k > 0
FreeFonts
SYSTEM

' ---- Fonts ----

' The first of several font files (separated by ";") that loads, or -1.
FUNCTION LoadFirst& (paths AS STRING, size AS INTEGER, style AS STRING)
    DIM rest AS STRING, path AS STRING, cut AS INTEGER, handle AS LONG
    rest = paths
    handle = -1
    DO WHILE handle = -1 AND rest <> ""
        cut = INSTR(rest, ";")
        IF cut = 0 THEN cut = LEN(rest) + 1
        path = LEFT$(rest, cut - 1)
        rest = MID$(rest, cut + 1)
        handle = _LOADFONT(path, size, style)
    LOOP
    LoadFirst& = handle
END FUNCTION

' A font if it loaded, otherwise the default font.
FUNCTION Pick& (handle AS LONG)
    IF handle = -1 THEN Pick& = DEFAULT_FONT ELSE Pick& = handle
END FUNCTION

SUB LoadFonts
    DIM sans AS STRING, i AS INTEGER
    sans = "/System/Library/Fonts/Supplemental/Arial.ttf;/System/Library/Fonts/Geneva.ttf;"
    sans = sans + "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf;" + BUNDLED
    TitleFont = LoadFirst&(sans, 40, "BOLD")
    LabelFont = LoadFirst&(BUNDLED, 16, "")
    WaveFont = LoadFirst&(sans, 32, "BOLD")

    RESTORE GalleryFonts
    FOR i = 0 TO 5
        DIM paths AS STRING, size AS INTEGER
        READ GalleryName(i), size, paths
        Gallery(i) = LoadFirst&(paths, size, "")
    NEXT

    Styled(0) = LoadFirst&(sans, 22, "")
    Styled(1) = LoadFirst&(sans, 22, "BOLD")
    Styled(2) = LoadFirst&(sans, 22, "ITALIC")
    Styled(3) = LoadFirst&(sans, 22, "UNDERLINE")
END SUB

GalleryFonts:
DATA "Modern DOS", 24, "fonts/ModernDOS8x16.ttf"
DATA "Monaco", 20, "/System/Library/Fonts/Monaco.ttf;/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
DATA "Geneva", 22, "/System/Library/Fonts/Geneva.ttf;/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
DATA "Courier", 22, "/System/Library/Fonts/Courier.ttc;/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"
DATA "Georgia", 22, "/System/Library/Fonts/Supplemental/Georgia.ttf;/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
DATA "Chalkboard", 22, "/System/Library/Fonts/Supplemental/Chalkboard.ttc;/usr/share/fonts/truetype/freefont/FreeSans.ttf"

SUB FreeFonts
    DIM i AS INTEGER
    _FONT DEFAULT_FONT
    FreeIfLoaded TitleFont: FreeIfLoaded LabelFont: FreeIfLoaded WaveFont
    FOR i = 0 TO 5: FreeIfLoaded Gallery(i): NEXT
    FOR i = 0 TO 3: FreeIfLoaded Styled(i): NEXT
END SUB

' Several entries may share one fallback font, so each is freed only once.
SUB FreeIfLoaded (handle AS LONG)
    IF handle <> -1 THEN _FREEFONT handle: handle = -1
END SUB

' ---- Text helpers ----

SUB Centered (y AS INTEGER, text AS STRING)
    _PRINTSTRING ((SCREEN_W - _PRINTWIDTH(text)) \ 2, y), text
END SUB

' Text with a soft shadow below and to the right.
SUB Shadowed (x AS INTEGER, y AS INTEGER, text AS STRING, ink AS _UNSIGNED LONG)
    COLOR _RGBA32(0, 0, 0, 140)
    _PRINTSTRING (x + 3, y + 3), text
    COLOR ink
    _PRINTSTRING (x, y), text
END SUB

' Text drawn eight times around itself in the outline color, then on top.
SUB Outlined (x AS INTEGER, y AS INTEGER, text AS STRING, ink AS _UNSIGNED LONG, edge AS _UNSIGNED LONG)
    DIM dx AS INTEGER, dy AS INTEGER
    COLOR edge
    FOR dy = -2 TO 2 STEP 2
        FOR dx = -2 TO 2 STEP 2
            IF dx OR dy THEN _PRINTSTRING (x + dx, y + dy), text
        NEXT
    NEXT
    COLOR ink
    _PRINTSTRING (x, y), text
END SUB

SUB Label (x AS INTEGER, y AS INTEGER, text AS STRING)
    _FONT Pick&(LabelFont)
    COLOR _RGB32(150, 170, 210)
    _PRINTSTRING (x, y), text
END SUB

' ---- The still part, drawn once into an image ----

SUB DrawBackdrop
    DIM y AS INTEGER, i AS INTEGER, x AS INTEGER, w AS INTEGER, text AS STRING
    Backdrop = _NEWIMAGE(SCREEN_W, SCREEN_H, 32)
    _PRINTMODE _KEEPBACKGROUND, Backdrop
    _DEST Backdrop
    FOR y = 0 TO SCREEN_H - 1
        LINE (0, y)-(SCREEN_W - 1, y), _RGB32(16 + y \ 20, 18 + y \ 16, 48 + y \ 8)
    NEXT

    ' Title: outlined and centered with _PRINTWIDTH.
    _FONT Pick&(TitleFont)
    text = "_PRINTSTRING & _LOADFONT"
    Outlined (SCREEN_W - _PRINTWIDTH(text)) \ 2, 18, text, _RGB32(255, 210, 60), _RGB32(120, 40, 0)
    _FONT Pick&(LabelFont)
    COLOR _RGB32(200, 210, 240)
    Centered 72, "Pixel-placed text in any TrueType font"

    ' Font gallery, two columns.
    Label 30, 112, "FONTS"
    FOR i = 0 TO 5
        x = 30 + (i MOD 2) * 380
        y = 136 + (i \ 2) * 40
        IF Gallery(i) = -1 THEN
            _FONT DEFAULT_FONT
            COLOR _RGB32(110, 110, 130)
            _PRINTSTRING (x, y + 6), GalleryName(i) + " (not installed)"
        ELSE
            _FONT Gallery(i)
            Shadowed x, y, GalleryName(i) + " Aa 123", _RGB32(240, 240, 255)
        END IF
    NEXT

    ' Styles of one font.
    Label 30, 380, "STYLES"
    x = 30
    RESTORE StyleNames
    FOR i = 0 TO 3
        READ text
        _FONT Pick&(Styled(i))
        COLOR _RGB32(140, 230, 160)
        _PRINTSTRING (x, 402), text
        x = x + _PRINTWIDTH(text) + 36
    NEXT

    ' Alignment in a box, measured with _PRINTWIDTH.
    Label 30, 446, "_PRINTWIDTH: LEFT, CENTER AND RIGHT"
    LINE (30, 468)-(SCREEN_W - 31, 512), _RGBA32(255, 255, 255, 30), BF
    LINE (30, 468)-(SCREEN_W - 31, 512), _RGB32(120, 140, 190), B
    _FONT Pick&(Styled(0))
    COLOR _RGB32(255, 255, 255)
    _PRINTSTRING (40, 479), "Left"
    text = "Center"
    _PRINTSTRING ((SCREEN_W - _PRINTWIDTH(text)) \ 2, 479), text
    text = "Right"
    w = _PRINTWIDTH(text)
    _PRINTSTRING (SCREEN_W - 40 - w, 479), text
    _FONT Pick&(LabelFont)
    COLOR _RGB32(150, 170, 210)
    _PRINTSTRING (SCREEN_W - 40 - _PRINTWIDTH(LTRIM$(STR$(w)) + " px"), 515), LTRIM$(STR$(w)) + " px"

    ' Translucent letters overlapping.
    _FONT Pick&(TitleFont)
    COLOR _RGBA32(255, 60, 60, 150): _PRINTSTRING (560, 386), "RGB"
    COLOR _RGBA32(60, 255, 60, 150): _PRINTSTRING (572, 396), "RGB"
    COLOR _RGBA32(80, 120, 255, 150): _PRINTSTRING (584, 406), "RGB"
    _DEST 0
END SUB

StyleNames:
DATA "Plain", "Bold", "Italic", "Underline"

' ---- The moving part, drawn every frame ----

' Each letter rides a sine wave in its own color.
SUB DrawWave (baseY AS INTEGER, frame AS LONG)
    DIM text AS STRING, i AS INTEGER, x AS INTEGER, y AS INTEGER, ch AS STRING, hue AS SINGLE
    _FONT Pick&(WaveFont)
    text = "Letters on a wave!"
    x = (SCREEN_W - _PRINTWIDTH(text)) \ 2
    FOR i = 1 TO LEN(text)
        ch = MID$(text, i, 1)
        y = baseY + 12 * SIN(frame / 9 + i / 2)
        hue = (frame * 4 + i * 20) MOD 360
        COLOR Rainbow~&(hue)
        _PRINTSTRING (x, y), ch
        x = x + _PRINTWIDTH(ch)
    NEXT
END SUB

' A line that types itself out, with a blinking cursor, then starts over.
SUB DrawTypewriter (y AS INTEGER, frame AS LONG)
    DIM text AS STRING, shown AS INTEGER, x AS INTEGER
    text = "Text can appear one letter at a time..."
    shown = (frame \ 3) MOD (LEN(text) + 30)
    IF shown > LEN(text) THEN shown = LEN(text)
    _FONT Pick&(Gallery(0))
    x = (SCREEN_W - _PRINTWIDTH(text)) \ 2
    COLOR _RGB32(255, 230, 160)
    _PRINTSTRING (x, y), LEFT$(text, shown)
    IF (frame \ 15) MOD 2 = 0 THEN
        LINE (x + _PRINTWIDTH(LEFT$(text, shown)) + 2, y)-STEP(10, 20), _RGB32(255, 230, 160), BF
    END IF
END SUB

' A news ticker scrolling along the bottom of the window.
SUB DrawTicker (frame AS LONG)
    DIM w AS INTEGER, x AS INTEGER
    LINE (0, SCREEN_H - 40)-(SCREEN_W - 1, SCREEN_H - 1), _RGB32(10, 10, 30), BF
    LINE (0, SCREEN_H - 41)-(SCREEN_W - 1, SCREEN_H - 41), _RGB32(255, 210, 60)
    _FONT Pick&(LabelFont)
    w = _PRINTWIDTH(TICKER)
    x = SCREEN_W - (frame * 3) MOD (w + SCREEN_W)
    COLOR _RGB32(255, 255, 255)
    _PRINTSTRING (x, SCREEN_H - 28), TICKER
END SUB

' A bright color from a hue of 0 to 359 degrees.
FUNCTION Rainbow~& (hue AS SINGLE)
    DIM h AS SINGLE, f AS SINGLE, up AS INTEGER, down AS INTEGER
    h = hue / 60
    f = h - INT(h)
    up = 255 * f: down = 255 * (1 - f)
    SELECT CASE INT(h)
        CASE 0: Rainbow~& = _RGB32(255, up, 0)
        CASE 1: Rainbow~& = _RGB32(down, 255, 0)
        CASE 2: Rainbow~& = _RGB32(0, 255, up)
        CASE 3: Rainbow~& = _RGB32(0, down, 255)
        CASE 4: Rainbow~& = _RGB32(up, 0, 255)
        CASE ELSE: Rainbow~& = _RGB32(255, 0, down)
    END SELECT
END FUNCTION
