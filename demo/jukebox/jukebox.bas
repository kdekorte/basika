' ============================================================================
'  JUKEBOX - pick a song with the mouse and keep its play history in a
'  random-access database
' ----------------------------------------------------------------------------
'  The songs live in jukebox.dat next to the program, one 128-byte record per
'  song (the SongRecord TYPE below): title, author, the year it was written,
'  its sound file in assets/, how many times it has been played and when it
'  was last played. The file is read and written with OPEN ... FOR RANDOM,
'  GET and PUT; it is created with three public domain songs the first time
'  the program runs. Delete it to start over.
'
'  Click a song to play it; click it again to pause or resume. Each time a
'  song starts from the beginning its record gets one more play and a new
'  last played date. "Reset" on a song, or "Reset all stats", sets the play
'  count back to 0 and clears the date, after asking first.
'
'  Keys    1-3 play or pause a song   Space pause / resume   S stop
'          R reset all stats   Esc quit (or cancel the reset question)
'
'  The songs are in assets/, made by tools/make_songs.py.
'  Run with: basika -w demo/jukebox/jukebox.bas
'  (basika --headless demo/jukebox/jukebox.bas --selftest [frames] plays two
'   songs into a fresh jukebox_selftest.dat, saves jukebox_selftest.png after
'   that many frames, 30 by default, and exits.)
' ============================================================================

DEFLNG A-Z

CONST SW = 800
CONST SH = 640
CONST MAX_SONGS = 3
CONST REC_LEN = 128 ' the bytes in a SongRecord

TYPE SongRecord
    Title AS STRING * 32
    Author AS STRING * 40
    Written AS STRING * 12
    FileName AS STRING * 24 ' in assets/
    Plays AS LONG
    LastPlayed AS STRING * 16 ' "YYYY-MM-DD HH:MM", blank if never played
END TYPE

DIM SHARED Songs(1 TO MAX_SONGS) AS SongRecord, SongCount
DIM SHARED Snd(1 TO MAX_SONGS)
DIM SHARED BaseDir AS STRING, DbName AS STRING
DIM SHARED Current, Paused ' the song in the player (0 for none)
DIM SHARED Mx, My, Clicked, WasDown, Blocked
DIM SHARED Confirming, ResetWhich ' ResetWhich 0 means every song
DIM SHARED Status AS STRING, StatusTime AS DOUBLE
DIM SHARED FontBig, FontHuge, FontSmall, BgImg
DIM SHARED Frame, SelfTest, QuitApp
DIM SHARED cText, cDim, cCard, cHover, cEdge, cAmber, cPink, cTeal, cPanel, cRed
DIM SHARED Accent(1 TO MAX_SONGS)

DIM frames AS LONG

SCREEN _NEWIMAGE(SW, SH, 32)
_TITLE "Jukebox"
_PRINTMODE _KEEPBACKGROUND
_AUTODISPLAY OFF

FindBaseDir
DbName = "jukebox.dat"
IF LEFT$(COMMAND$, 10) = "--selftest" THEN
    SelfTest = -1
    frames = VAL(MID$(COMMAND$, 11))
    IF frames <= 0 THEN frames = 30
    DbName = "jukebox_selftest.dat"
    OPEN BaseDir + DbName FOR OUTPUT AS #1 ' empty, so it is made afresh
    CLOSE #1
END IF
SetUp
LoadDatabase
LoadSongs

DO
    ReadInput
    IF SelfTest THEN SelfTestStep
    CheckSongEnd
    Render
    _DISPLAY
    Frame = Frame + 1
    IF SelfTest THEN
        IF Frame >= frames THEN SCREENSHOT "jukebox_selftest.png": SYSTEM
    ELSE
        _LIMIT 30
    END IF
LOOP UNTIL QuitApp
IF Current THEN _SNDSTOP Snd(Current)
SYSTEM

' The songs a new database starts with: title, author, written, sound file.
DATA "Happy Birthday to You","Mildred J. Hill and Patty S. Hill","1893","happy_birthday.wav"
DATA "Ode to Joy","Ludwig van Beethoven","1824","ode_to_joy.wav"
DATA "Greensleeves","Traditional English","c. 1580","greensleeves.wav"

' ------------------------------------------------------------------- set up

' The database and sounds live next to this program, wherever it is run from.
SUB FindBaseDir
    DIM i AS INTEGER, p$
    p$ = ARGV$(0)
    BaseDir = ""
    FOR i = LEN(p$) TO 1 STEP -1
        IF MID$(p$, i, 1) = "/" OR MID$(p$, i, 1) = "\" THEN BaseDir = LEFT$(p$, i): EXIT FOR
    NEXT
END SUB

SUB SetUp
    DIM y, t!
    cText = _RGB32(246, 240, 255): cDim = _RGB32(176, 160, 212)
    cCard = _RGB32(44, 32, 76): cHover = _RGB32(62, 47, 104)
    cEdge = _RGB32(92, 74, 146): cPanel = _RGB32(30, 22, 54)
    cAmber = _RGB32(255, 186, 77): cPink = _RGB32(255, 95, 162)
    cTeal = _RGB32(77, 225, 210): cRed = _RGB32(255, 110, 110)
    Accent(1) = cAmber: Accent(2) = cPink: Accent(3) = cTeal

    ' The repository's DOS font at 1, 2 and 3 times its size; the built-in
    ' font stands in if it is missing.
    FontSmall = _LOADFONT(BaseDir + "../../fonts/ModernDOS8x16.ttf", 16)
    FontBig = _LOADFONT(BaseDir + "../../fonts/ModernDOS8x16.ttf", 32)
    FontHuge = _LOADFONT(BaseDir + "../../fonts/ModernDOS8x16.ttf", 48)
    IF FontSmall = -1 THEN FontSmall = 16
    IF FontBig = -1 THEN FontBig = FontSmall
    IF FontHuge = -1 THEN FontHuge = FontBig

    ' The background never changes, so it is drawn once.
    BgImg = _NEWIMAGE(SW, SH, 32)
    _DEST BgImg
    _PRINTMODE _KEEPBACKGROUND
    FOR y = 0 TO SH - 1
        t! = y / SH
        LINE (0, y)-(SW - 1, y), _RGB32(26 - 12 * t!, 16 - 8 * t!, 48 - 18 * t!)
    NEXT
    LINE (24, 90)-(SW - 25, 91), cEdge, BF
    Txt SW \ 2 - TextW("JUKEBOX", FontHuge) \ 2 + 3, 19, "JUKEBOX", FontHuge, cPink
    Txt SW \ 2 - TextW("JUKEBOX", FontHuge) \ 2, 16, "JUKEBOX", FontHuge, cAmber
    _DEST 0
END SUB

' ----------------------------------------------------------------- database

FUNCTION OpenDatabase
    DIM f
    f = FREEFILE
    OPEN BaseDir + DbName FOR RANDOM AS #f LEN = REC_LEN
    OpenDatabase = f
END FUNCTION

' Reads every record into Songs(), first writing the starting songs into an
' empty file.
SUB LoadDatabase
    DIM f, i, rec AS SongRecord, t$, a$, w$, n$
    f = OpenDatabase
    IF LOF(f) < REC_LEN THEN
        RESTORE
        FOR i = 1 TO MAX_SONGS
            READ t$, a$, w$, n$
            rec.Title = t$: rec.Author = a$: rec.Written = w$: rec.FileName = n$
            rec.Plays = 0: rec.LastPlayed = ""
            PUT #f, i, rec
        NEXT
    END IF
    SongCount = LOF(f) \ REC_LEN
    IF SongCount > MAX_SONGS THEN SongCount = MAX_SONGS
    FOR i = 1 TO SongCount
        GET #f, i, rec
        Songs(i) = rec
    NEXT
    CLOSE #f
END SUB

' One more play for song i, dated now.
SUB RecordPlay (i)
    DIM f, rec AS SongRecord
    f = OpenDatabase
    GET #f, i, rec
    rec.Plays = rec.Plays + 1
    rec.LastPlayed = Now$
    PUT #f, i, rec
    CLOSE #f
    Songs(i) = rec
    SetStatus "Saved record" + STR$(i) + ":" + STR$(rec.Plays) + " play" + Plural$(rec.Plays) + ", last played " + Clean$(rec.LastPlayed)
END SUB

' Clears the play count and last played date of song which, or of every
' song when which is 0.
SUB ResetStats (which)
    DIM f, i, rec AS SongRecord
    f = OpenDatabase
    FOR i = 1 TO SongCount
        IF which = 0 OR which = i THEN
            GET #f, i, rec
            rec.Plays = 0
            rec.LastPlayed = ""
            PUT #f, i, rec
            Songs(i) = rec
        END IF
    NEXT
    CLOSE #f
    IF which = 0 THEN
        SetStatus "Reset the stats of all" + STR$(SongCount) + " songs"
    ELSE
        SetStatus "Reset the stats of record" + STR$(which) + ", " + Clean$(Songs(which).Title)
    END IF
END SUB

' Now as "YYYY-MM-DD HH:MM"; DATE$ is "MM-DD-YYYY".
FUNCTION Now$
    DIM d$
    d$ = DATE$
    Now$ = MID$(d$, 7, 4) + "-" + LEFT$(d$, 2) + "-" + MID$(d$, 4, 2) + " " + LEFT$(TIME$, 5)
END FUNCTION

' A fixed-length field without its padding.
FUNCTION Clean$ (s$)
    DIM n
    n = LEN(s$)
    DO WHILE n > 0
        IF MID$(s$, n, 1) <> " " AND MID$(s$, n, 1) <> CHR$(0) THEN EXIT DO
        n = n - 1
    LOOP
    Clean$ = LEFT$(s$, n)
END FUNCTION

FUNCTION Plural$ (n)
    IF n = 1 THEN Plural$ = "" ELSE Plural$ = "s"
END FUNCTION

FUNCTION LastPlayedText$ (i)
    DIM s$
    s$ = Clean$(Songs(i).LastPlayed)
    IF s$ = "" THEN
        LastPlayedText$ = "Never"
    ELSEIF LEFT$(s$, 10) = LEFT$(Now$, 10) THEN
        LastPlayedText$ = "Today " + MID$(s$, 12)
    ELSE
        LastPlayedText$ = s$
    END IF
END FUNCTION

' ------------------------------------------------------------------- player

SUB LoadSongs
    DIM i
    FOR i = 1 TO SongCount
        Snd(i) = _SNDOPEN(BaseDir + "assets/" + Clean$(Songs(i).FileName))
    NEXT
END SUB

' Starts song i from the beginning, or pauses or resumes it if it is the one
' in the player.
SUB PickSong (i)
    IF i < 1 OR i > SongCount THEN EXIT SUB
    IF i = Current THEN TogglePause: EXIT SUB
    IF Snd(i) = 0 THEN
        SetStatus "Cannot load assets/" + Clean$(Songs(i).FileName) + "; run tools/make_songs.py"
        EXIT SUB
    END IF
    IF Current THEN _SNDSTOP Snd(Current)
    Current = i: Paused = 0
    _SNDPLAY Snd(i)
    RecordPlay i
END SUB

SUB TogglePause
    IF Current = 0 THEN EXIT SUB
    IF Paused THEN _SNDPLAY Snd(Current) ELSE _SNDPAUSE Snd(Current)
    Paused = NOT Paused
END SUB

SUB StopSong
    IF Current = 0 THEN EXIT SUB
    _SNDSTOP Snd(Current)
    Current = 0: Paused = 0
END SUB

' A song that has played to its end leaves the player. The self-test has no
' audio, so its songs would end at once.
SUB CheckSongEnd
    IF Current = 0 OR Paused OR SelfTest THEN EXIT SUB
    IF NOT _SNDPLAYING(Snd(Current)) THEN Current = 0
END SUB

' --------------------------------------------------------------------- input

SUB ReadInput
    DIM k, down
    WHILE _MOUSEINPUT: WEND
    Mx = _MOUSEX: My = _MOUSEY
    down = _MOUSEBUTTON(1)
    Clicked = down AND NOT WasDown
    WasDown = down
    DO
        k = _KEYHIT
        IF k > 0 THEN
            IF Confirming THEN
                SELECT CASE k
                    CASE 13, ASC("y"), ASC("Y"): ResetStats ResetWhich: Confirming = 0
                    CASE 27, ASC("n"), ASC("N"): Confirming = 0
                END SELECT
            ELSE
                SELECT CASE k
                    CASE ASC("1") TO ASC("9"): PickSong k - ASC("0")
                    CASE 32: TogglePause
                    CASE ASC("s"), ASC("S"): StopSong
                    CASE ASC("r"), ASC("R"): AskReset 0
                    CASE 27: QuitApp = -1
                END SELECT
            END IF
        END IF
    LOOP WHILE k
END SUB

SUB AskReset (which)
    Confirming = -1
    ResetWhich = which
END SUB

' The self-test plays song 1, stops it and plays song 2, so the database
' and the player both show up in the screenshot.
SUB SelfTestStep
    SELECT CASE Frame
        CASE 3: PickSong 1
        CASE 6: StopSong
        CASE 9: PickSong 2
    END SELECT
END SUB

FUNCTION Inside (x1, y1, x2, y2)
    Inside = NOT Blocked AND Mx >= x1 AND Mx <= x2 AND My >= y1 AND My <= y2
END FUNCTION

' True once for a click inside the box; the click is then used up.
FUNCTION ClickIn (x1, y1, x2, y2)
    IF Clicked AND Inside(x1, y1, x2, y2) THEN
        Clicked = 0
        ClickIn = -1
    END IF
END FUNCTION

SUB SetStatus (s$)
    Status = s$
    StatusTime = TIMER
END SUB

' ------------------------------------------------------------------- drawing

SUB Render
    DIM i
    _PUTIMAGE (0, 0), BgImg
    Blocked = Confirming
    FOR i = 1 TO SongCount
        DrawCard i
    NEXT
    IF SongCount = 0 THEN Txt 44, 120, "The database has no songs.", FontBig, cDim
    DrawPlayer
    DrawFooter
    IF Confirming THEN
        Blocked = 0
        DrawConfirm
    END IF
END SUB

SUB DrawCard (i)
    DIM x1, y1, x2, y2, cx, cy, bg, hover, playing, s$
    x1 = 24: x2 = SW - 25
    y1 = 104 + (i - 1) * 118: y2 = y1 + 106
    cx = x1 + 60: cy = y1 + 53

    ' The Reset button sits inside the card, so it takes its click first.
    IF ClickIn(690, y1 + 70, 760, y1 + 94) THEN AskReset i
    IF ClickIn(x1, y1, x2, y2) THEN PickSong i

    hover = Inside(x1, y1, x2, y2)
    playing = (i = Current)
    IF hover THEN bg = cHover ELSE bg = cCard
    IF playing THEN
        FillRound x1 - 2, y1 - 2, x2 + 2, y2 + 2, 16, Accent(i)
    ELSEIF hover THEN
        FillRound x1 - 1, y1 - 1, x2 + 1, y2 + 1, 15, cEdge
    END IF
    FillRound x1, y1, x2, y2, 14, bg

    ' The badge: the song's number, or bars that dance while it plays.
    FillCircle cx, cy, 36, Accent(i)
    FillCircle cx, cy, 32, _RGB32(_RED32(Accent(i)) * .35, _GREEN32(Accent(i)) * .35, _BLUE32(Accent(i)) * .35)
    IF playing THEN
        DrawBars cx, cy, Accent(i)
    ELSE
        s$ = LTRIM$(STR$(i))
        Txt cx - TextW(s$, FontBig) \ 2, cy - 15, s$, FontBig, Accent(i)
    END IF

    Txt 140, y1 + 16, Clean$(Songs(i).Title), FontBig, cText
    Txt 140, y1 + 54, Clean$(Songs(i).Author) + ", " + Clean$(Songs(i).Written), FontSmall, cDim
    IF Snd(i) = 0 THEN
        s$ = "Missing assets/" + Clean$(Songs(i).FileName): bg = cRed
    ELSEIF playing AND Paused THEN
        s$ = "Paused - click to resume": bg = Accent(i)
    ELSEIF playing THEN
        s$ = "Now playing - click to pause": bg = Accent(i)
    ELSE
        s$ = "Click to play": bg = cDim
        IF NOT hover THEN bg = cEdge
    END IF
    Txt 140, y1 + 78, s$, FontSmall, bg

    Txt 500, y1 + 16, "PLAYS", FontSmall, cDim
    Txt 500, y1 + 38, LTRIM$(STR$(Songs(i).Plays)), FontBig, cText
    Txt 590, y1 + 16, "LAST PLAYED", FontSmall, cDim
    Txt 590, y1 + 44, LastPlayedText$(i), FontSmall, cText
    DrawButton 690, y1 + 70, 760, y1 + 94, "Reset", -1
END SUB

' Five bars in the badge, still while the song is paused.
SUB DrawBars (cx, cy, c)
    DIM k, h, x
    FOR k = 0 TO 4
        IF Paused THEN
            h = 6
        ELSE
            h = 6 + 24 * ABS(SIN(Frame * (.17 + .05 * k) + k * 1.3))
        END IF
        x = cx - 22 + k * 9
        LINE (x, cy + 16 - h)-(x + 5, cy + 16), c, BF
    NEXT
END SUB

SUB DrawPlayer
    DIM x1, y1, x2, y2, h, length!, pos!, w, label$
    x1 = 24: y1 = 462: x2 = SW - 25: y2 = 590
    FillRound x1, y1, x2, y2, 14, cPanel
    Txt 44, 476, "NOW PLAYING", FontSmall, cDim
    IF Current THEN
        h = Snd(Current)
        Txt 44, 496, Clean$(Songs(Current).Title), FontBig, Accent(Current)
        length! = _SNDLEN(h)
        pos! = _SNDGETPOS(h)
    ELSE
        Txt 44, 496, "Pick a song", FontBig, cEdge
    END IF

    ' The progress bar and the time played.
    FillRound 44, 554, 400, 566, 6, cCard
    IF length! > 0 THEN
        w = (400 - 44) * pos! / length!
        IF w > 12 THEN FillRound 44, 554, 44 + w, 566, 6, Accent(Current)
    END IF
    Txt 414, 552, Clock$(pos!) + " / " + Clock$(length!), FontSmall, cDim

    IF Paused OR Current = 0 THEN label$ = "Play" ELSE label$ = "Pause"
    IF ClickIn(520, 478, 640, 520) AND Current <> 0 THEN TogglePause
    DrawButton 520, 478, 640, 520, label$, Current <> 0
    IF ClickIn(652, 478, 756, 520) THEN StopSong
    DrawButton 652, 478, 756, 520, "Stop", Current <> 0
    IF ClickIn(520, 532, 756, 572) THEN AskReset 0
    DrawButton 520, 532, 756, 572, "Reset all stats", -1
END SUB

SUB DrawFooter
    DIM s$
    IF Status <> "" AND TIMER - StatusTime < 5 AND TIMER >= StatusTime THEN
        Txt 24, 606, Status, FontSmall, cAmber
    ELSE
        Txt 24, 606, "Click or 1-3: play/pause   Space pause   S stop   R reset   Esc quit", FontSmall, cDim
        s$ = DbName + ":" + STR$(SongCount) + " x" + STR$(REC_LEN) + " bytes"
        Txt SW - 24 - TextW(s$, FontSmall), 606, s$, FontSmall, cEdge
    END IF
END SUB

SUB DrawConfirm
    DIM x1, y1, x2, y2, s$
    x1 = 180: y1 = 216: x2 = SW - 181: y2 = 404
    LINE (0, 0)-(SW - 1, SH - 1), _RGBA32(0, 0, 0, 170), BF
    FillRound x1 - 2, y1 - 2, x2 + 2, y2 + 2, 16, cAmber
    FillRound x1, y1, x2, y2, 14, cPanel
    Center 236, "Reset stats?", FontBig, cText
    IF ResetWhich = 0 THEN
        s$ = "all" + STR$(SongCount) + " songs"
    ELSE
        s$ = Clean$(Songs(ResetWhich).Title)
    END IF
    Center 284, "The play count and last played date", FontSmall, cDim
    Center 304, "will be cleared for " + s$ + ".", FontSmall, cDim
    IF ClickIn(220, 344, 390, 384) THEN ResetStats ResetWhich: Confirming = 0
    DrawButton 220, 344, 390, 384, "Reset", -1
    IF ClickIn(410, 344, 580, 384) THEN Confirming = 0
    DrawButton 410, 344, 580, 384, "Cancel", -1
    IF Clicked AND NOT Inside(x1, y1, x2, y2) THEN Confirming = 0: Clicked = 0
END SUB

SUB DrawButton (x1, y1, x2, y2, label$, enabled)
    DIM bg, fg
    IF NOT enabled THEN
        bg = cCard: fg = cEdge
    ELSEIF Inside(x1, y1, x2, y2) THEN
        bg = cAmber: fg = cPanel
    ELSE
        bg = cEdge: fg = cText
    END IF
    FillRound x1, y1, x2, y2, (y2 - y1) \ 3, bg
    Txt (x1 + x2) \ 2 - TextW(label$, FontSmall) \ 2, (y1 + y2) \ 2 - 8, label$, FontSmall, fg
END SUB

SUB Center (y, s$, f, c)
    Txt SW \ 2 - TextW(s$, f) \ 2, y, s$, f, c
END SUB

SUB Txt (x, y, s$, f, c)
    _FONT f
    COLOR c
    _PRINTSTRING (x, y), s$
END SUB

FUNCTION TextW (s$, f)
    _FONT f
    TextW = _PRINTWIDTH(s$)
END FUNCTION

FUNCTION Clock$ (s!)
    DIM t
    t = INT(s!)
    Clock$ = LTRIM$(STR$(t \ 60)) + ":" + RIGHT$("0" + LTRIM$(STR$(t MOD 60)), 2)
END FUNCTION

' A filled box with corners rounded to radius r.
SUB FillRound (x1, y1, x2, y2, r, c)
    DIM i, d
    FOR i = 0 TO r - 1
        d = r - SQR(r * r - (r - i - .5) ^ 2)
        LINE (x1 + d, y1 + i)-(x2 - d, y1 + i), c
        LINE (x1 + d, y2 - i)-(x2 - d, y2 - i), c
    NEXT
    LINE (x1, y1 + r)-(x2, y2 - r), c, BF
END SUB

SUB FillCircle (cx, cy, r, c)
    DIM dy, w
    FOR dy = -r TO r
        w = SQR(r * r - dy * dy)
        LINE (cx - w, cy + dy)-(cx + w, cy + dy), c
    NEXT
END SUB
