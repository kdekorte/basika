REM BOING BALL - AN AMIGA-STYLE RED AND WHITE CHECKER SPHERE
REM Press Space to pause/resume; press another key to quit.
REM Run with: basika -w demo/boing_ball.bas
REM Everything is drawn in 1024x768 screen pixels.
SCREEN _NEWIMAGE(1024, 768, 32): CLS
_AUTODISPLAY OFF

CONST GREY = _RGB32(170, 170, 170)
CONST CYAN = _RGB32(85, 255, 255)
CONST RED = _RGB32(255, 85, 85)
CONST DARK_RED = _RGB32(170, 0, 0)
CONST WHITE = _RGB32(255, 255, 255)
CONST DARK_GREY = _RGB32(85, 85, 85)
REM Height in pixels of each band of the ball's checker texture.
CONST ROW_STEP = 2
REM The ball's spin repeats every 64 steps. Each frame of it is drawn the first
REM time it is shown, into a CELL x CELL square of an 8x8 sprite sheet, and
REM copied to the screen with _PUTIMAGE after that.
CONST SPIN_FRAMES = 64
CONST CELL = 196
LINE (0, 0)-(1023, 767), GREY, BF

DIM SHARED SIN_ANGLE(96), COS_ANGLE(96)
DIM SHARED BallSheet AS LONG, BallDrawn(SPIN_FRAMES - 1) AS INTEGER
BallSheet = _NEWIMAGE(8 * CELL, 8 * CELL, 32)
PI = 3.14159
BALL_RADIUS = SphereHalfWidth(96, 0)
FLOOR_Y = 528
BALL_CONTACT_Y = FLOOR_Y + 54
BALL_X = 224
BALL_Y = 184
OLD_X = BALL_X
OLD_Y = BALL_Y
VELOCITY_X = 5
VELOCITY_Y = 0
SPIN_POS = 0
PAUSED = 0

CALL InitializeTextureAngles(PI)
CALL DrawBackdrop((FLOOR_Y))
LAST_TICK# = TIMER

WHILE 1
	KEY$ = INKEY$
	IF KEY$ <> "" THEN
		IF ASC(KEY$) = 32 THEN
			PAUSED = 1 - PAUSED
		ELSE
			END
		END IF
	END IF

	REM The motion moves in steps of 1/60 second, as many as have passed since
	REM the last frame (up to 3), so the ball keeps its speed while new frames
	REM of the spin are being drawn.
	NOW_TICK# = TIMER
	STEPS = (NOW_TICK# - LAST_TICK#) * 60
	IF STEPS < 0 THEN STEPS = 1 ' TIMER went past midnight
	IF STEPS > 3 THEN STEPS = 3
	LAST_TICK# = NOW_TICK#

	IF PAUSED = 0 THEN
		CALL RestoreBallArea((OLD_X), (OLD_Y), (BALL_RADIUS), (FLOOR_Y), (BALL_CONTACT_Y))

		BALL_X = BALL_X + VELOCITY_X * STEPS
		BALL_Y = BALL_Y + VELOCITY_Y * STEPS
		VELOCITY_Y = VELOCITY_Y + .7 * STEPS

		IF BALL_X < 221 THEN BALL_X = 221: VELOCITY_X = -VELOCITY_X: PLAY "MBT240V4O2L16MS C G O1 C"
		IF BALL_X > 803 THEN BALL_X = 803: VELOCITY_X = -VELOCITY_X: PLAY "MBT240V4O2L16MS G C O1 G"
		IF BALL_Y > BALL_CONTACT_Y - BALL_RADIUS THEN BALL_Y = BALL_CONTACT_Y - BALL_RADIUS: VELOCITY_Y = -18.14: PLAY "MBT240V4O2L16MS G C O1 G"

		CALL DrawBall((BALL_X), (BALL_Y), (BALL_RADIUS), INT(SPIN_POS), (PI), (BALL_CONTACT_Y))
		OLD_X = BALL_X
		OLD_Y = BALL_Y
		SPIN_POS = SPIN_POS + STEPS

		CALL DrawFps
		_DISPLAY
	END IF
	_LIMIT 60
WEND
END

SUB DrawFps
	REM Frames per second, updated twice a second, in the bottom-right corner.
	STATIC Frames AS LONG, StartTime AS DOUBLE, Shown AS STRING
	Frames = Frames + 1
	Elapsed# = TIMER - StartTime
	IF StartTime = 0 OR Elapsed# < 0 THEN
		StartTime = TIMER: Frames = 0: Shown = "-- FPS"
	ELSEIF Elapsed# >= .5 THEN
		Shown = LTRIM$(STR$(INT(Frames / Elapsed# + .5))) + " FPS"
		StartTime = TIMER: Frames = 0
	END IF
	TextWidth = _PRINTWIDTH(Shown)
	LINE (_WIDTH - TextWidth - 14, _HEIGHT - 26)-(_WIDTH - 1, _HEIGHT - 1), GREY, BF
	COLOR DARK_GREY, GREY
	_PRINTSTRING (_WIDTH - TextWidth - 8, _HEIGHT - 22), Shown
END SUB

SUB InitializeTextureAngles(PiValue)
	FOR Index = 0 TO 96
		Angle = -PiValue / 2 + Index * PiValue / 96
		SIN_ANGLE(Index) = SIN(Angle)
		COS_ANGLE(Index) = COS(Angle)
	NEXT Index
END SUB

SUB DrawBackdrop(FloorY)
	REM Bright cyan strokes, two pixels wide: a back wall with a 40-pixel grid
	REM from x 122 to 902, and a floor fanning out to the bottom corners.
	LINE (122, 32)-(902, FloorY), CYAN, B
	LINE (123, 33)-(901, FloorY - 1), CYAN, B
	FOR GridX = 162 TO 862 STEP 40
		LINE (GridX, 32)-(GridX + 1, FloorY), CYAN, BF
	NEXT GridX
	FOR GridY = 72 TO 488 STEP 40
		LINE (122, GridY)-(902, GridY + 1), CYAN, BF
	NEXT GridY
	FOR GridX = 122 TO 902 STEP 40
		BottomX = 35 + (GridX - 122) * 954 / 780
		LINE (GridX, FloorY)-(BottomX, 744), CYAN
		LINE (GridX + 1, FloorY)-(BottomX + 1, 744), CYAN
	NEXT GridX
	LINE (902, FloorY)-(989, 744), CYAN
	LINE (901, FloorY)-(988, 744), CYAN
	FOR Row = 1 TO 8
		GridY = FloorY + Row * Row * 216 / 64
		LeftX = 122 - (GridY - FloorY) * 87 / 216
		RightX = 902 + (GridY - FloorY) * 87 / 216
		LINE (LeftX, GridY)-(RightX, GridY + 1), CYAN, BF
	NEXT Row
END SUB

SUB RestoreBallArea(OldX, OldY, Radius, FloorY, ContactY)
	HeightAboveFloor = ContactY - (OldY + Radius)
	IF HeightAboveFloor < 0 THEN HeightAboveFloor = 0
	ShadowRadius = 51 + HeightAboveFloor * .1
	ShadowOffsetX = 29 + HeightAboveFloor * .1
	ShadowRight = OldX + ShadowOffsetX + ShadowRadius + 6
	IF ShadowRight < OldX + Radius + 6 THEN ShadowRight = OldX + Radius + 6
	RestoreBottom = ContactY + ShadowRadius + 6
	IF RestoreBottom < OldY + Radius + 64 THEN RestoreBottom = OldY + Radius + 64
	LINE (OldX - Radius - 6, OldY - Radius - 6)-(ShadowRight, RestoreBottom), GREY, BF
	CALL DrawBackdrop((FloorY))
END SUB

SUB DrawBall(BallX, BallY, Radius, Frame, PiValue, ContactY)
	HeightAboveFloor = ContactY - (BallY + Radius)
	IF HeightAboveFloor < 0 THEN HeightAboveFloor = 0
	ShadowRadius = 51 + HeightAboveFloor * .1
	ShadowOffsetX = 29 + HeightAboveFloor * .1
	ShadowAlpha = 160 - HeightAboveFloor * .22
	IF ShadowAlpha < 80 THEN ShadowAlpha = 80
	REM The shadow fades as the ball rises: a translucent _RGBA32 fill blends with the floor.
	CALL FillEllipse((BallX + ShadowOffsetX), (ContactY), (ShadowRadius), (ShadowRadius * .45), _RGBA32(0, 0, 0, ShadowAlpha))
	Spin = Frame MOD SPIN_FRAMES
	CellX = (Spin MOD 8) * CELL
	CellY = (Spin \ 8) * CELL
	IF NOT BallDrawn(Spin) THEN
		CALL DrawBallCell((CellX + CELL \ 2), (CellY + CELL \ 2), (Radius), (Spin), (PiValue))
		BallDrawn(Spin) = -1
	END IF
	REM The cell's corners are transparent, so only the ball covers the backdrop.
	_PUTIMAGE (INT(BallX + .5) - CELL \ 2, INT(BallY + .5) - CELL \ 2), BallSheet, 0, (CellX, CellY)-(CellX + CELL - 1, CellY + CELL - 1)
END SUB

REM Draws one frame of the spinning ball into the sprite sheet, centered at (CenterX, CenterY).
SUB DrawBallCell(CenterX, CenterY, Radius, Spin, PiValue)
	_DEST BallSheet
	CALL FillEllipse((CenterX), (CenterY), (Radius), (Radius), WHITE)
	CALL DrawSphereTexture((CenterX), (CenterY), (Radius), (Spin), (PiValue))
	CIRCLE (CenterX, CenterY), Radius, DARK_GREY
	CALL FillEllipse((CenterX - 38), (CenterY - 45), 6, 6, WHITE)
	_DEST 0
END SUB

SUB FillEllipse(CenterX, CenterY, RadiusX, RadiusY, FillColor AS _UNSIGNED LONG)
	REM One horizontal span per pixel row, so translucent colors blend evenly.
	FOR ScreenY = INT(CenterY - RadiusY + .5) TO INT(CenterY + RadiusY + .5)
		Row = ScreenY - CenterY
		IF ABS(Row) <= RadiusY THEN
			HalfWidth = RadiusX * SQR(1 - (Row / RadiusY) * (Row / RadiusY))
			LINE (CenterX - HalfWidth, ScreenY)-(CenterX + HalfWidth, ScreenY), FillColor
		END IF
	NEXT ScreenY
END SUB

SUB DrawSphereTexture(CenterX, CenterY, Radius, Frame, PiValue)
	DIM TileColor AS _UNSIGNED LONG, PreviousColor AS _UNSIGNED LONG
	Phase = (Frame MOD 64) * PiValue / 32
	Tilt = .62
	Roll = .38 + SIN(Phase) * .12
	CosPhase = COS(Phase)
	SinPhase = SIN(Phase)
	CosTilt = COS(Tilt)
	SinTilt = SIN(Tilt)
	CosRoll = COS(Roll)
	SinRoll = SIN(Roll)

	FOR DeltaY = -Radius + 1 TO Radius - 1 STEP ROW_STEP
		HalfWidth = SQR(Radius * Radius - DeltaY * DeltaY)
		PreviousColor = WHITE

		FOR Segment = 0 TO 95
			X1 = INT(HalfWidth * SIN_ANGLE(Segment))
			X2 = INT(HalfWidth * SIN_ANGLE(Segment + 1))
			DeltaX = INT((X1 + X2) / 2)
			' INT can round DeltaX just past the rim, so keep SQR's argument >= 0.
			DepthSquared = Radius * Radius - DeltaX * DeltaX - DeltaY * DeltaY
			IF DepthSquared < 0 THEN DepthSquared = 0
			Depth = SQR(DepthSquared)

			U0 = DeltaX * CosPhase + Depth * SinPhase
			Z1 = Depth * CosPhase - DeltaX * SinPhase
			V0 = DeltaY * CosTilt - Z1 * SinTilt
			W = DeltaY * SinTilt + Z1 * CosTilt
			U = U0 * CosRoll - V0 * SinRoll
			V = U0 * SinRoll + V0 * CosRoll

			IF ABS(W) < .001 AND W < 0 THEN W = -.001
			IF ABS(W) < .001 AND W >= 0 THEN W = .001
			Longitude = ATN(U / W)
			IF W < 0 AND U >= 0 THEN Longitude = Longitude + PiValue
			IF W < 0 AND U < 0 THEN Longitude = Longitude - PiValue

			LatitudeRadius = SQR(U * U + W * W)
			IF LatitudeRadius < .001 AND V >= 0 THEN Latitude = PiValue / 2
			IF LatitudeRadius < .001 AND V < 0 THEN Latitude = -PiValue / 2
			IF LatitudeRadius >= .001 THEN Latitude = ATN(V / LatitudeRadius)

			LatitudeRow = INT((Latitude + PiValue / 2) * 12 / PiValue)
			IF LatitudeRow < 0 THEN LatitudeRow = 0
			IF LatitudeRow > 11 THEN LatitudeRow = 11
			LongitudeColumn = INT((Longitude + PiValue) * 8 / PiValue)
			IF LongitudeColumn < 0 THEN LongitudeColumn = 0
			IF LongitudeColumn > 15 THEN LongitudeColumn = 15

			Parity = (LatitudeRow + LongitudeColumn) MOD 2
			Light = Depth - DeltaX * .28 - DeltaY * .32
			TileColor = RED
			IF Parity = 1 THEN TileColor = WHITE
			IF Light < 61 AND Parity = 0 THEN TileColor = DARK_RED
			IF Light < 61 AND Parity = 1 THEN TileColor = GREY
			IF Light < 29 AND Parity = 0 THEN TileColor = DARK_RED
			IF Light < 29 AND Parity = 1 THEN TileColor = DARK_GREY

			IF Segment = 0 THEN RunStart = X1: PreviousColor = TileColor
			IF Segment > 0 AND TileColor <> PreviousColor THEN LINE (CenterX + RunStart, CenterY + DeltaY)-(CenterX + X1, CenterY + DeltaY + ROW_STEP - 1), PreviousColor, BF: RunStart = X1
			IF Segment > 0 THEN PreviousColor = TileColor
			IF Segment = 95 THEN LINE (CenterX + RunStart, CenterY + DeltaY)-(CenterX + X2, CenterY + DeltaY + ROW_STEP - 1), PreviousColor, BF
		NEXT Segment
	NEXT DeltaY
END SUB

FUNCTION SphereHalfWidth(Radius, RowValue)
	SphereHalfWidth = SQR(Radius * Radius - RowValue * RowValue)
END FUNCTION
