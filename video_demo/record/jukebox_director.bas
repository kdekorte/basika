' ---- Video recording: replaces SUB SelfTestStep in a copy of jukebox.bas ----
' A pointer glides over the song cards and the player and clicks them, at
' 30 frames a second like the jukebox itself. ReadInput still reads the
' (idle) real mouse first, so this sets the position and the click after it.
' make_jukebox.sh puts this file in place and draws the pointer each frame.

SUB SelfTestStep
    ' Each step: the frame it starts on, where the pointer goes, how many
    ' frames it takes to get there, and whether it clicks on arrival.
    DIM t AS SINGLE
    IF Frame = 0 THEN RecX = 400: RecY = 560: Aim 400, 560, 0, 0
    SELECT CASE Frame
        CASE 20: Aim 380, 150, 18, -1 ' Happy Birthday
        CASE 140: Aim 380, 268, 18, 0 ' hover over Ode to Joy
        CASE 175: Aim 380, 268, 2, -1 ' ... and play it
        CASE 290: Aim 580, 499, 18, -1 ' Pause
        CASE 345: Aim 580, 499, 2, -1 ' Play again
        CASE 420: Aim 380, 386, 18, -1 ' Greensleeves
        CASE 560: Aim 725, 386 + 28, 18, -1 ' its Reset button
        CASE 625: Aim 495, 364, 18, -1 ' "No"
        CASE 680: Aim 700, 499, 18, -1 ' Stop
        CASE 730: Aim 400, 600, 20, 0 ' out of the way
    END SELECT
    IF Frame <= RecT1 THEN
        t = 1: IF RecT1 > RecT0 THEN t = (Frame - RecT0) / (RecT1 - RecT0)
        t = t * t * (3 - 2 * t)
        RecX = RecSX + (RecTX - RecSX) * t: RecY = RecSY + (RecTY - RecSY) * t
    END IF
    Mx = RecX: My = RecY
    IF Frame = RecT1 AND RecClick THEN Clicked = -1
END SUB

' Starts a glide from where the pointer is to (x, y), arriving n frames from
' now; click = -1 clicks on arrival.
SUB Aim (x, y, n, click)
    RecSX = RecX: RecSY = RecY: RecTX = x: RecTY = y
    RecT0 = Frame: RecT1 = Frame + n: RecClick = click
END SUB

SUB DrawPointer (x, y)
    DIM r, w
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
