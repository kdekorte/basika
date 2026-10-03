REM CHROMATIC FLUX - a high-resolution, alpha-layered graphics showcase
REM Use a large 1280x1024 canvas with the complete 256-color drawing palette.
SCREEN _NEWIMAGE(1280, 1024, 256): CLS
WINDOW SCREEN (0,0)-(639,479)
_AUTODISPLAY OFF

PI = 3.14159265
CALL DrawShowcase

REM Animate a large comet clearly back and forth beneath the planet.
ANIM_STEP = 0
WHILE 1
	CALL DrawAnimationStage(ANIM_STEP)
	_DISPLAY
	IF INKEY$ <> "" THEN END
	SLEEP 25
	ANIM_STEP = ANIM_STEP + 1
WEND
END

SUB DrawShowcase
	REM Use a continuous base so alpha circles blend without horizontal banding.
	LINE (0,0)-(639,479), 1, BF

	REM Nebula clouds: each translucent disc adds another layer of color.
	CIRCLE (165,170), 150, 45, 2, 22
	CIRCLE (190,155), 112, 202, 2, 25
	CIRCLE (475,165), 170, 92, 2, 20
	CIRCLE (450,190), 112, 226, 2, 22
	CIRCLE (320,370), 190, 165, 2, 17
	CIRCLE (320,365), 130, 35, 2, 20

	REM Starfield, using alpha PSET points with varied color and intensity.
	FOR STAR = 1 TO 220
		SX = (STAR * 157 + STAR * STAR * 13) MOD 636 + 2
		SY = (STAR * 83 + STAR * STAR * 7) MOD 470 + 2
		STAR_COLOR = 16 + ((STAR * 29) MOD 240)
		STAR_ALPHA = 70 + ((STAR * 47) MOD 186)
		PSET (SX,SY), STAR_COLOR, STAR_ALPHA
	NEXT STAR

	REM Perspective orbit rings behind the central world.
	FOR RING = 0 TO 2
		RADIUS_X = 132 + RING * 19
		RADIUS_Y = 35 + RING * 8
		OLD_X = 320 + RADIUS_X
		OLD_Y = 220
		FOR SEGMENT = 1 TO 72
			ANGLE = SEGMENT * 2 * PI / 72
			NEW_X = 320 + COS(ANGLE) * RADIUS_X
			NEW_Y = 220 + SIN(ANGLE) * RADIUS_Y
			RING_COLOR = 110 + RING * 40 + SEGMENT MOD 28
			LINE (OLD_X,OLD_Y)-(NEW_X,NEW_Y), RING_COLOR, 72
			OLD_X = NEW_X
			OLD_Y = NEW_Y
		NEXT SEGMENT
	NEXT RING

	REM PAINT / ALPHA card aligned with the primitive legend card.
	LINE (28,360)-(190,470), 225, B
	PAINT (30,362), 24, 225, 148
	LINE (38,397)-(180,459), 180, B
	PAINT (40,399), 68, 180, 95

	REM Planet halo, built from nested transparent fills.
	FOR HALO = 0 TO 7
		HALO_RADIUS = 116 - HALO * 5
		HALO_COLOR = 32 + HALO * 25
		HALO_ALPHA = 12 + HALO * 2
		CIRCLE (320,220), HALO_RADIUS, HALO_COLOR, 2, HALO_ALPHA
	NEXT HALO

	REM The planet body and softly layered color bands.
	CIRCLE (320,220), 78, 38, 2
	CIRCLE (320,220), 72, 214, 2, 185
	FOR BAND = -58 TO 58 STEP 8
		HALF_WIDTH = SQR(62 * 62 - BAND * BAND)
		BAND_COLOR = 25 + ((BAND + 64) * 2)
		LINE (320-HALF_WIDTH,220+BAND)-(320+HALF_WIDTH,220+BAND), BAND_COLOR, 105
	NEXT BAND
	CIRCLE (320,220), 78, 239, 72
	CIRCLE (293,193), 15, 255, 2, 95
	CIRCLE (293,193), 6, 255, 2, 220

	REM Foreground orbital arc: blended short line segments.
	FOR SEGMENT = 1 TO 72
		ANGLE = SEGMENT * 2 * PI / 72
		OLD_X = 320 + COS(ANGLE - 2 * PI / 72) * 171
		OLD_Y = 220 + SIN(ANGLE - 2 * PI / 72) * 64
		NEW_X = 320 + COS(ANGLE) * 171
		NEW_Y = 220 + SIN(ANGLE) * 64
		ARC_COLOR = 32 + ((SEGMENT * 11) MOD 224)
		LINE (OLD_X,OLD_Y)-(NEW_X,NEW_Y), ARC_COLOR, 150
	NEXT SEGMENT

	REM Alpha swatches and framed primitive examples along the bottom edge.
	LINE (220,360)-(612,470), 0, BF, 185
	LINE (220,360)-(612,470), 190, B
	FOR SWATCH = 0 TO 7
		SWATCH_X = 235 + SWATCH * 45
		SWATCH_COLOR = 16 + SWATCH * 32
		LINE (SWATCH_X,420)-(SWATCH_X+34,438), SWATCH_COLOR, BF, 48 + SWATCH * 28
		LINE (SWATCH_X,443)-(SWATCH_X+34,459), SWATCH_COLOR, BF
	NEXT SWATCH
	FOR SPARK = 0 TO 30
		PX = 225 + (SPARK * 61) MOD 380
		PY = 464 + (SPARK MOD 2) * 5
		PSET (PX,PY), 16 + ((SPARK * 23) MOD 240), 80 + (SPARK MOD 6) * 30
	NEXT SPARK

	REM Keep the title, mode and controls in a dedicated top bar.
	LINE (0,0)-(639,43), 0, BF
	LINE (0,44)-(639,44), 190, 125
	COLOR 15
	_PRINTSTRING (40,18), "CHROMATIC FLUX"
	_PRINTSTRING (40,52), "1280 x 1024  |  256 COLOR PALETTE  |  ALPHA PRIMITIVES"
	_PRINTSTRING (930,52), "PRESS ANY KEY TO EXIT"

	REM Matching card headings align above their respective content.
	_PRINTSTRING (56,746), "PAINT / ALPHA"
	_PRINTSTRING (440,746), "DRAWING PRIMITIVES"
	_PRINTSTRING (440,782), "PSET  LINE  CIRCLE  PAINT"
	_DISPLAY
END SUB

SUB DrawAnimationStage(StepNumber)
	REM Clear the slim track each frame so the comet moves without covering the planet.
	LINE (18,302)-(622,338), 0, BF
	LINE (18,302)-(622,338), 190, B
	LINE (30,320)-(610,320), 29, 2
	LINE (30,322)-(610,322), 130, 2
	FOR MARK = 0 TO 10
		MARK_X = 38 + MARK * 56
		LINE (MARK_X,312)-(MARK_X,328), 190, 2
	NEXT MARK

	TRAVEL = StepNumber MOD 240
	DIRECTION = 1
	IF TRAVEL > 120 THEN TRAVEL = 240 - TRAVEL: DIRECTION = -1
	ORB_X = 65 + TRAVEL * 4.25
	ORB_Y = 320 + SIN(StepNumber * .12) * 3
	FOR TRAIL = 9 TO 1 STEP -1
		TRAIL_X = ORB_X - DIRECTION * TRAIL * 10
		TRAIL_Y = 320 + SIN((StepNumber - TRAIL * 2) * .12) * 3
		TRAIL_COLOR = 16 + ((StepNumber * 11 + TRAIL * 23) MOD 240)
		CIRCLE (TRAIL_X,TRAIL_Y), 2 + (10 - TRAIL), TRAIL_COLOR, 2, 25 + (10 - TRAIL) * 9
	NEXT TRAIL
	ORB_COLOR = 16 + ((StepNumber * 13) MOD 240)
	CIRCLE (ORB_X,ORB_Y), 13, ORB_COLOR, 2, 65
	CIRCLE (ORB_X,ORB_Y), 9, 245, 2, 195
	CIRCLE (ORB_X,ORB_Y), 4, 255, 2
	PSET (ORB_X,ORB_Y), 255
END SUB
