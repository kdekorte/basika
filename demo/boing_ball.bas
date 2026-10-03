REM BOING BALL - AN AMIGA-STYLE RED AND WHITE CHECKER SPHERE
REM Run with: basika -w demo/boing_ball.bas
SCREEN 12: CLS
_AUTODISPLAY OFF
LINE (0, 0)-(639, 479), 7, BF

DIM SIN_ANGLE(96), COS_ANGLE(96)
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

CALL InitializeTextureAngles(PI)
CALL DrawBackdrop((FLOOR_Y))

WHILE 1
	CALL RestoreBallArea((OLD_X), (OLD_Y), (BALL_RADIUS), (FLOOR_Y), (BALL_CONTACT_Y))

	BALL_X = BALL_X + VELOCITY_X
	BALL_Y = BALL_Y + VELOCITY_Y
	VELOCITY_Y = VELOCITY_Y + .62

	IF BALL_X < 138 THEN BALL_X = 138: VELOCITY_X = -VELOCITY_X
	IF BALL_X > 502 THEN BALL_X = 502: VELOCITY_X = -VELOCITY_X
	IF BALL_Y > BALL_CONTACT_Y - BALL_RADIUS THEN BALL_Y = BALL_CONTACT_Y - BALL_RADIUS: VELOCITY_Y = -11.2

	CALL DrawBall((BALL_X), (BALL_Y), (BALL_RADIUS), (FRAME), (PI), (BALL_CONTACT_Y))
	OLD_X = BALL_X
	OLD_Y = BALL_Y
	FRAME = FRAME + 1

	_DISPLAY
	IF INKEY$ <> "" THEN END
	SLEEP 18
WEND
END

SUB InitializeTextureAngles(PiValue)
	SHARED SIN_ANGLE, COS_ANGLE
	FOR Index = 0 TO 96
		Angle = -PiValue / 2 + Index * PiValue / 96
		SIN_ANGLE(Index) = SIN(Angle)
		COS_ANGLE(Index) = COS(Angle)
	NEXT Index
END SUB

SUB DrawBackdrop(FloorY)
	REM Bright cyan, doubled strokes stay visible after the canvas is scaled down.
	LINE (76, 20)-(564, FloorY), 11, B
	LINE (77, 21)-(563, FloorY - 1), 11, B
	FOR GridX = 101 TO 539 STEP 25
		LINE (GridX, 20)-(GridX, FloorY), 11
		LINE (GridX + 1, 20)-(GridX + 1, FloorY), 11
	NEXT GridX
	FOR GridY = 45 TO 305 STEP 25
		LINE (76, GridY)-(564, GridY), 11
		LINE (76, GridY + 1)-(564, GridY + 1), 11
	NEXT GridY
	LINE (76, FloorY)-(22, 465), 11
	LINE (77, FloorY)-(23, 465), 11
	LINE (564, FloorY)-(618, 465), 11
	LINE (563, FloorY)-(617, 465), 11
	FOR GridX = 76 TO 564 STEP 25
		BottomX = 22 + (GridX - 76) * 600 / 488
		LINE (GridX, FloorY)-(BottomX, 465), 11
		LINE (GridX + 1, FloorY)-(BottomX + 1, 465), 11
	NEXT GridX
	FOR Row = 1 TO 8
		GridY = FloorY + Row * Row * 135 / 64
		LeftX = 76 - (GridY - FloorY) * 54 / 135
		RightX = 564 + (GridY - FloorY) * 54 / 135
		LINE (LeftX, GridY)-(RightX, GridY), 11
		LINE (LeftX, GridY + 1)-(RightX, GridY + 1), 11
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
	LINE (OldX - Radius - 4, OldY - Radius - 4)-(ShadowRight, RestoreBottom), 7, BF
	CALL DrawBackdrop((FloorY))
END SUB

SUB DrawBall(BallX, BallY, Radius, Frame, PiValue, ContactY)
	HeightAboveFloor = ContactY - (BallY + Radius)
	IF HeightAboveFloor < 0 THEN HeightAboveFloor = 0
	ShadowRadius = 32 + HeightAboveFloor * .1
	ShadowOffsetX = 18 + HeightAboveFloor * .1
	ShadowAlpha = 160 - HeightAboveFloor * .35
	IF ShadowAlpha < 80 THEN ShadowAlpha = 80
	CIRCLE (BallX + ShadowOffsetX, ContactY), ShadowRadius, 0, 2, ShadowAlpha
	CIRCLE (BallX, BallY), Radius, 15, 2
	CALL DrawSphereTexture((BallX), (BallY), (Radius), (Frame), (PiValue))
	CIRCLE (BallX, BallY), Radius, 8
	CIRCLE (BallX - 24, BallY - 28), 4, 15, 2
END SUB

SUB DrawSphereTexture(CenterX, CenterY, Radius, Frame, PiValue)
	SHARED SIN_ANGLE, COS_ANGLE

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
		PreviousColor = 15

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
			TileColor = 12
			IF Parity = 1 THEN TileColor = 15
			IF Light < 38 AND Parity = 0 THEN TileColor = 4
			IF Light < 38 AND Parity = 1 THEN TileColor = 7
			IF Light < 18 AND Parity = 0 THEN TileColor = 4
			IF Light < 18 AND Parity = 1 THEN TileColor = 8

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
