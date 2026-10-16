' Records demo/3dwave.bas for the video: the same sinc-wave surface in
' SCREEN 1, first drawn line by line as the demo draws it, then turned
' around the Y axis. Every frame is saved as build/frames/3dwave/NNNNN.png.
' Run from video_demo/: basika --headless record/3dwave.bas
DEFINT I-N
DECLARE SUB DrawWave (ry AS SINGLE, rows AS INTEGER)
DECLARE SUB SaveFrame ()
DIM SHARED FrameNo AS LONG
CONST OUTDIR = "build/frames/3dwave/"

SCREEN 1
_AUTODISPLAY OFF
' Draw it the way the demo does, a few rows of the surface per frame.
FOR rows = 1 TO 51 STEP 1
    DrawWave .7854, rows
    SaveFrame
NEXT
FOR i = 1 TO 20: SaveFrame: NEXT ' hold the finished picture
' Then turn it: 150 frames, one full turn.
FOR i = 1 TO 150
    DrawWave .7854 + i * 6.2832 / 150, 51
    SaveFrame
NEXT
SYSTEM

SUB DrawWave (ry AS SINGLE, rows AS INTEGER)
    DIM x, z, first, cx AS SINGLE, cy AS SINGLE, sc AS SINGLE
    DIM rx AS SINGLE, rz AS SINGLE, d AS SINGLE, y AS SINGLE
    DIM x1 AS SINGLE, z1 AS SINGLE, y1 AS SINGLE, px AS SINGLE, py AS SINGLE
    DIM nx AS SINGLE, ny AS SINGLE
    CLS
    cx = 160: cy = 100: sc = 4
    rx = .7854: rz = .7854
    FOR z = -25 TO -25 + rows - 1
        first = 1
        FOR x = -25 TO 25
            d = SQR(x * x + z * z)
            y = 0: IF d > 0 THEN y = 12 * SIN(d) / d
            x1 = x * COS(ry) - z * SIN(ry)
            z1 = x * SIN(ry) + z * COS(ry)
            y1 = y * COS(rx) - z1 * SIN(rx)
            nx = cx + (x1 * COS(rz) - y1 * SIN(rz)) * sc
            ny = cy - (x1 * SIN(rz) + y1 * COS(rz)) * sc
            IF first = 0 THEN LINE (px, py)-(nx, ny), 3
            px = nx: py = ny: first = 0
        NEXT
    NEXT
    LOCATE 23, 1: PRINT "3D WAVE - 45 DEGREE TRIPLE TILT"
    _DISPLAY
END SUB

SUB SaveFrame
    FrameNo = FrameNo + 1
    SCREENSHOT OUTDIR + RIGHT$("00000" + LTRIM$(STR$(FrameNo)), 5) + ".png"
END SUB
