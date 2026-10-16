' Records demo/fractal.bas for the video: the same Mandelbrot set in a
' 640 x 480, 256-color screen, saving a frame every few rows as it fills in,
' then holding the finished picture. Frames go to build/frames/fractal/.
' Run from video_demo/: basika --headless record/fractal.bas
DEFDBL A-H, O-Z
DEFINT I-N
DIM SHARED FrameNo AS LONG
CONST OUTDIR = "build/frames/fractal/"
CONST ROWS_PER_FRAME = 4

SCREEN _NEWIMAGE(640, 480, 256): CLS
_AUTODISPLAY OFF
MAXITER = 128
RMIN = -2.0: RMAX = 0.8
IMIN = -1.05: IMAX = 1.05
DR = (RMAX - RMIN) / 640.0
DI = (IMAX - IMIN) / 480.0
COLOR 15: LOCATE 1, 1: PRINT "GENERATING MANDELBROT FRACTAL..."
FOR Y = 0 TO 479
    CI = IMIN + Y * DI
    FOR X = 0 TO 639
        CR = RMIN + X * DR
        ZR = 0.0: ZI = 0.0
        ZR2 = 0.0: ZI2 = 0.0
        K = 0
        WHILE (ZR2 + ZI2 <= 4.0) AND (K < MAXITER)
            ZI = 2.0 * ZR * ZI + CI
            ZR = ZR2 - ZI2 + CR
            ZR2 = ZR * ZR
            ZI2 = ZI * ZI
            K = K + 1
        WEND
        IF K = MAXITER THEN N = 0 ELSE N = (K * 3 + 16) MOD 256
        PSET (X, Y), N
    NEXT X
    IF Y MOD ROWS_PER_FRAME = ROWS_PER_FRAME - 1 THEN _DISPLAY: SaveFrame
NEXT Y
COLOR 15: LOCATE 30, 1: PRINT "MANDELBROT SET (640x480, 256 COLORS)";
_DISPLAY
FOR I = 1 TO 75: SaveFrame: NEXT ' hold the finished picture
SYSTEM

SUB SaveFrame
    FrameNo = FrameNo + 1
    SCREENSHOT OUTDIR + RIGHT$("00000" + LTRIM$(STR$(FrameNo)), 5) + ".png"
END SUB
