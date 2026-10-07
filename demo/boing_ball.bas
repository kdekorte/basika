REM BOING BALL - AN AMIGA-STYLE RED AND WHITE CHECKER SPHERE
REM Press Space to pause/resume; press another key to quit.
REM Run with: basika -w demo/boing_ball.bas
SCREEN _NEWIMAGE(640, 480, 32): CLS
_AUTODISPLAY OFF

CONST GREY = _RGB32(170, 170, 170)
CONST CYAN = _RGB32(85, 255, 255)
CONST RED = _RGB32(255, 85, 85)
CONST DARK_RED = _RGB32(170, 0, 0)
CONST WHITE = _RGB32(255, 255, 255)
CONST DARK_GREY = _RGB32(85, 85, 85)
LINE (0, 0)-(639, 479), GREY, BF

DIM SHARED SIN_ANGLE(96), COS_ANGLE(96)
PI = 3.14159
BALL_RADIUS = SphereHalfWidth(60, 0)
FLOOR_Y = 330
BALL_CONTACT_Y = FLOOR_Y + 34
BALL_X = 140
BALL_Y = 115
OLD_X = BALL_X
OLD_Y = BALL_Y
VELOCITY_X = 4
VELOCITY_Y = 0
FRAME = 0
PAUSED = 0

CALL InitializeTextureAngles(PI)
CALL DrawBackdrop((FLOOR_Y))

WHILE 1
	KEY$ = INKEY$
	IF KEY$ <> "" THEN
		IF ASC(KEY$) = 32 THEN
			PAUSED = 1 - PAUSED
		ELSE
			END
		END IF
	END IF

	IF PAUSED = 0 THEN
		CALL RestoreBallArea((OLD_X), (OLD_Y), (BALL_RADIUS), (FLOOR_Y), (BALL_CONTACT_Y))

		BALL_X = BALL_X + VELOCITY_X
		BALL_Y = BALL_Y + VELOCITY_Y
		VELOCITY_Y = VELOCITY_Y + .62

		IF BALL_X < 138 THEN BALL_X = 138: VELOCITY_X = -VELOCITY_X: PLAY "MBT240V4O2L16MS C G O1 C"
		IF BALL_X > 502 THEN BALL_X = 502: VELOCITY_X = -VELOCITY_X: PLAY "MBT240V4O2L16MS G C O1 G"
		IF BALL_Y > BALL_CONTACT_Y - BALL_RADIUS THEN BALL_Y = BALL_CONTACT_Y - BALL_RADIUS: VELOCITY_Y = -11.2: PLAY "MBT240V4O2L16MS G C O1 G"

		CALL DrawBall((BALL_X), (BALL_Y), (BALL_RADIUS), (FRAME), (PI), (BALL_CONTACT_Y))
		OLD_X = BALL_X
		OLD_Y = BALL_Y
		FRAME = FRAME + 1

		_DISPLAY
	END IF
	_LIMIT 55
WEND
END

SUB InitializeTextureAngles(PiValue)
	FOR Index = 0 TO 96
		Angle = -PiValue / 2 + Index * PiValue / 96
		SIN_ANGLE(Index) = SIN(Angle)
		COS_ANGLE(Index) = COS(Angle)
	NEXT Index
END SUB

SUB DrawBackdrop(FloorY)
	REM Bright cyan, doubled strokes stay visible after the canvas is scaled down.
	LINE (76, 20)-(564, FloorY), CYAN, B
	LINE (77, 21)-(563, FloorY - 1), CYAN, B
	FOR GridX = 101 TO 539 STEP 25
		LINE (GridX, 20)-(GridX, FloorY), CYAN
		LINE (GridX + 1, 20)-(GridX + 1, FloorY), CYAN
	NEXT GridX
	FOR GridY = 45 TO 305 STEP 25
		LINE (76, GridY)-(564, GridY), CYAN
		LINE (76, GridY + 1)-(564, GridY + 1), CYAN
	NEXT GridY
	LINE (76, FloorY)-(22, 465), CYAN
	LINE (77, FloorY)-(23, 465), CYAN
	LINE (564, FloorY)-(618, 465), CYAN
	LINE (563, FloorY)-(617, 465), CYAN
	FOR GridX = 76 TO 564 STEP 25
		BottomX = 22 + (GridX - 76) * 600 / 488
		LINE (GridX, FloorY)-(BottomX, 465), CYAN
		LINE (GridX + 1, FloorY)-(BottomX + 1, 465), CYAN
	NEXT GridX
	FOR Row = 1 TO 8
		GridY = FloorY + Row * Row * 135 / 64
		LeftX = 76 - (GridY - FloorY) * 54 / 135
		RightX = 564 + (GridY - FloorY) * 54 / 135
		LINE (LeftX, GridY)-(RightX, GridY), CYAN
		LINE (LeftX, GridY + 1)-(RightX, GridY + 1), CYAN
	NEXT Row
END SUB

SUB RestoreBallArea(OldX, OldY, Radius, FloorY, ContactY)
	HeightAboveFloor = ContactY - (OldY + Radius)
	IF HeightAboveFloor < 0 THEN HeightAboveFloor = 0
	ShadowRadius = 32 + HeightAboveFloor * .1
	ShadowOffsetX = 18 + HeightAboveFloor * .1
	ShadowRight = OldX + ShadowOffsetX + ShadowRadius + 4
	IF ShadowRight < OldX + Radius + 4 THEN ShadowRight = OldX + Radius + 4
	RestoreBottom = ContactY + ShadowRadius + 4
	IF RestoreBottom < OldY + Radius + 40 THEN RestoreBottom = OldY + Radius + 40
	LINE (OldX - Radius - 4, OldY - Radius - 4)-(ShadowRight, RestoreBottom), GREY, BF
	CALL DrawBackdrop((FloorY))
END SUB

SUB DrawBall(BallX, BallY, Radius, Frame, PiValue, ContactY)
	HeightAboveFloor = ContactY - (BallY + Radius)
	IF HeightAboveFloor < 0 THEN HeightAboveFloor = 0
	ShadowRadius = 32 + HeightAboveFloor * .1
	ShadowOffsetX = 18 + HeightAboveFloor * .1
	ShadowAlpha = 160 - HeightAboveFloor * .35
	IF ShadowAlpha < 80 THEN ShadowAlpha = 80
	REM The shadow fades as the ball rises: a translucent _RGBA32 fill blends with the floor.
	CALL FillEllipse((BallX + ShadowOffsetX), (ContactY), (ShadowRadius), (ShadowRadius * .45), _RGBA32(0, 0, 0, ShadowAlpha))
	CALL FillEllipse((BallX), (BallY), (Radius), (Radius), WHITE)
	CALL DrawSphereTexture((BallX), (BallY), (Radius), (Frame), (PiValue))
	CIRCLE (BallX, BallY), Radius, DARK_GREY
	CALL FillEllipse((BallX - 24), (BallY - 28), 4, 4, WHITE)
END SUB

SUB FillEllipse(CenterX, CenterY, RadiusX, RadiusY, FillColor AS _UNSIGNED LONG)
	REM One horizontal span per row, so translucent colors blend evenly.
	FOR Row = -INT(RadiusY) TO INT(RadiusY)
		HalfWidth = RadiusX * SQR(1 - (Row / RadiusY) * (Row / RadiusY))
		LINE (CenterX - HalfWidth, CenterY + Row)-(CenterX + HalfWidth, CenterY + Row), FillColor
	NEXT Row
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

	FOR DeltaY = -59 TO 59 STEP 2
		HalfWidth = SQR(Radius * Radius - DeltaY * DeltaY)
		PreviousColor = WHITE

		FOR Segment = 0 TO 95
			X1 = INT(HalfWidth * SIN_ANGLE(Segment))
			X2 = INT(HalfWidth * SIN_ANGLE(Segment + 1))
			DeltaX = INT((X1 + X2) / 2)
			Depth = SQR(Radius * Radius - DeltaX * DeltaX - DeltaY * DeltaY)

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
			IF Light < 38 AND Parity = 0 THEN TileColor = DARK_RED
			IF Light < 38 AND Parity = 1 THEN TileColor = GREY
			IF Light < 18 AND Parity = 0 THEN TileColor = DARK_RED
			IF Light < 18 AND Parity = 1 THEN TileColor = DARK_GREY

			IF Segment = 0 THEN RunStart = X1: PreviousColor = TileColor
			IF Segment > 0 AND TileColor <> PreviousColor THEN LINE (CenterX + RunStart, CenterY + DeltaY)-(CenterX + X1, CenterY + DeltaY + 2), PreviousColor, BF: RunStart = X1
			IF Segment > 0 THEN PreviousColor = TileColor
			IF Segment = 95 THEN LINE (CenterX + RunStart, CenterY + DeltaY)-(CenterX + X2, CenterY + DeltaY + 2), PreviousColor, BF
		NEXT Segment
	NEXT DeltaY
END SUB

FUNCTION SphereHalfWidth(Radius, RowValue)
	SphereHalfWidth = SQR(Radius * Radius - RowValue * RowValue)
END FUNCTION
