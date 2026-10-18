' QB64 color, image and window extras; prints one line per failed check
SCREEN _NEWIMAGE(100, 100, 32)
failed = 0
OPEN "tests/qb64_image_extras.result" FOR OUTPUT AS #1

SUB Check (name$, ok)
    SHARED failed
    IF NOT ok THEN failed = failed + 1: PRINT #1, "FAIL "; name$
END SUB

Check "hsb green", _HSB32(120, 100, 100) = _RGB32(0, 255, 0)
Check "hsb red", _HSB32(0, 100, 100) = _RGB32(255, 0, 0)
Check "hsba alpha", _HSBA32(240, 100, 100, 50) = _RGBA32(0, 0, 255, 128)
Check "hue", _HUE32(_RGB32(0, 255, 0)) = 120 AND _HUE32(_RGB32(0, 0, 255)) = 240
Check "gray hue", _HUE32(_RGB32(90, 90, 90)) = 0
Check "saturation", _SATURATION32(_RGB32(255, 0, 0)) = 100
Check "brightness", _BRIGHTNESS32(_RGB32(255, 255, 255)) = 100
_TITLE "extras title"
Check "title", _TITLE$ = "extras title"

img& = _NEWIMAGE(40, 30, 32)
Check "pixelsize", _PIXELSIZE(img&) = 4 AND _WIDTH(img&) = 40
_DEST img&
LINE (0, 0)-(39, 29), _RGB32(255, 0, 0), BF
_DEST 0
_SETALPHA 128, _RGB32(255, 0, 0), img&
_SOURCE img&
Check "setalpha", POINT(5, 5) = _RGBA32(255, 0, 0, 128)
_SOURCE 0
Check "default color", _DEFAULTCOLOR(img&) = _RGB32(255, 255, 255)
Check "background color", _BACKGROUNDCOLOR(img&) = _RGB32(0, 0, 0)

LINE (0, 0)-(99, 99), _RGB32(0, 0, 255), BF
sprite& = _NEWIMAGE(20, 20, 32)
_DEST sprite&
LINE (0, 0)-(19, 19), _RGB32(255, 255, 255), BF
LINE (5, 5)-(14, 14), _RGB32(255, 0, 0), BF
_DEST 0
_CLEARCOLOR _RGB32(255, 255, 255), sprite&
_PUTIMAGE (10, 10), sprite&
Check "clearcolor corner", POINT(11, 11) = _RGB32(0, 0, 255)
Check "clearcolor center", POINT(20, 20) = _RGB32(255, 0, 0)

IF _FILEEXISTS("tests/extras_test.png") THEN KILL "tests/extras_test.png"
_SAVEIMAGE "tests/extras_test", sprite&
Check "saveimage", _FILEEXISTS("tests/extras_test.png")
IF _FILEEXISTS("tests/extras_test.png") THEN KILL "tests/extras_test.png"

pal& = _NEWIMAGE(10, 10, 256)
_PALETTECOLOR 40, _RGB32(10, 20, 30), pal&
Check "palettecolor", _PALETTECOLOR(40, pal&) = _RGB32(10, 20, 30) AND _PIXELSIZE(pal&) = 1
Check "font size", _FONTWIDTH > 0 AND _FONTHEIGHT > 0
Check "desktop", _DESKTOPWIDTH > 0 AND _DESKTOPHEIGHT > 0
Check "screen exists", _SCREENEXISTS

IF failed = 0 THEN PRINT #1, "QB64 IMAGE EXTRAS PASS"
CLOSE #1
