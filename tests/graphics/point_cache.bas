' POINT reads from a copy of the image once enough reads find it unchanged.
' Each section reads a pixel often enough to make that copy, changes the
' image, and reads again: every drawing command must drop the copy.
SCREEN _NEWIMAGE(160, 120, 32)
OPEN "tests/point_cache.result" FOR OUTPUT AS #1
DIM SHARED img
img = _NEWIMAGE(40, 30, 32)
warm 1
PSET (5, 5), _RGB32(255, 0, 0): warm 2
LINE (0, 0)-(30, 30), _RGB32(0, 255, 0): warm 3
LINE (18, 18)-(22, 22), _RGB32(0, 0, 255), BF: warm 4
CIRCLE (20, 20), 3, _RGB32(9, 9, 9): warm 5
PAINT (20, 20), _RGB32(7, 8, 9), _RGB32(9, 9, 9): warm 6
CLS: warm 7
' Text is drawn as spaces on a background color: antialiased glyph pixels
' differ between renderers.
COLOR _RGB32(255, 255, 255), _RGB32(0, 0, 128)
LOCATE 1, 1: PRINT "   ";: warm 8
COLOR _RGB32(255, 255, 255), _RGB32(0, 128, 0)
_PRINTSTRING (0, 0), "   ": warm 9
COLOR _RGB32(255, 255, 255), _RGB32(0, 0, 0)
DRAW "BM5,5 C" + STR$(_RGB32(1, 2, 3)) + " R3": warm 10
_DEST img: LINE (0, 0)-(39, 29), _RGB32(50, 60, 70), BF: _DEST 0
_PUTIMAGE (0, 0), img: warm 11
DIM buf(200)
GET (0, 0)-(9, 9), buf
CLS: PUT (0, 0), buf, PSET: warm 12

' Reading another image while drawing on the screen keeps its copy.
_SOURCE img
FOR k = 1 TO 200: d = POINT(3, 3): NEXT
_DEST img: PSET (3, 3), _RGB32(1, 1, 1): _DEST 0
PRINT #1, "img"; POINT(3, 3)
FOR k = 1 TO 200: d = POINT(4, 4): PSET (k MOD 100, 50), _RGB32(k, 0, 0): NEXT
PRINT #1, "img2"; POINT(4, 4); POINT(3, 3)
' A new image may get the freed one's texture address.
_FREEIMAGE img
img = _NEWIMAGE(40, 30, 32)
_SOURCE img
PRINT #1, "new"; POINT(3, 3); POINT(4, 4)
_SOURCE 0

' Reads and writes taking turns on one image.
FOR k = 1 TO 500
    c = POINT(k MOD 100, 60)
    PSET (k MOD 100, 60), c + 1
NEXT
PRINT #1, "inter"; POINT(1, 60); POINT(50, 60)

' Palette modes, PALETTE, VIEW, WINDOW and screen pages.
SCREEN 13
FOR k = 1 TO 200: d = POINT(1, 1): NEXT
PSET (5, 5), 4
PRINT #1, "13:"; POINT(5, 5);
FOR k = 1 TO 200: d = POINT(1, 1): NEXT
PALETTE 4, 63
PRINT #1, POINT(5, 5);
FOR k = 1 TO 200: d = POINT(5, 5): NEXT
LINE (0, 0)-(10, 10), 9, BF
PRINT #1, POINT(5, 5)
SCREEN 12
FOR k = 1 TO 200: d = POINT(1, 1): NEXT
PSET (1, 1), 14
PRINT #1, "12:"; POINT(1, 1);
VIEW (10, 10)-(100, 100)
FOR k = 1 TO 200: d = POINT(2, 2): NEXT
PSET (2, 2), 3
PRINT #1, POINT(2, 2);
WINDOW (0, 0)-(1, 1)
FOR k = 1 TO 200: d = POINT(.5, .5): NEXT
PSET (.5, .5), 5
PRINT #1, POINT(.5, .5)
SCREEN 7, , 1, 0
PSET (3, 3), 2
FOR k = 1 TO 200: d = POINT(3, 3): NEXT
PCOPY 0, 1
PRINT #1, "pcopy"; POINT(3, 3)
CLOSE #1
END

SUB warm (n)
    FOR k = 1 TO 200: dummy = POINT(1, 1): NEXT
    PRINT #1, n; POINT(5, 5); POINT(20, 20); POINT(1, 1)
END SUB
