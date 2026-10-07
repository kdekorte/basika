REM BUBBLE SORT VISUALIZER
REM Run with: basika -w demo/sort3.bas
SCREEN _NEWIMAGE(1280, 1024, 256)
_AUTODISPLAY OFF

DIM SortValue(20), BarColor(20)

FOR BarIndex = 1 TO 20
	SortValue(BarIndex) = (INT(RND * 150) + 10) * 2
	BarColor(BarIndex) = INT(RND * 16) + 1
NEXT BarIndex

CLS
CALL DrawAllBars
_DISPLAY

FOR Pass = 1 TO 19
	FOR BarIndex = 1 TO 20 - Pass
		IF SortValue(BarIndex) <= SortValue(BarIndex + 1) THEN GOTO NoSwap
		CALL EraseBarPair((BarIndex))
		SWAP SortValue(BarIndex), SortValue(BarIndex + 1)
		SWAP BarColor(BarIndex), BarColor(BarIndex + 1)
		CALL DrawBarPair((BarIndex))
		_DISPLAY
		_DELAY .01
	NoSwap:
	NEXT BarIndex
NEXT Pass

LOCATE 1, 1
PRINT "SORTED!"
_DISPLAY
END

SUB DrawAllBars
	SHARED SortValue, BarColor
	FOR BarIndex = 1 TO 20
		X = BarIndex * 50
		LINE (X, 780)-(X + 40, 480 - SortValue(BarIndex)), BarColor(BarIndex), BF
	NEXT BarIndex
END SUB

SUB EraseBarPair(FirstBar)
	SHARED SortValue, BarColor
	FOR BarIndex = FirstBar TO FirstBar + 1
		X = BarIndex * 50
		LINE (X - 5, 780)-(X + 45, 0), 0, BF
	NEXT BarIndex
END SUB

SUB DrawBarPair(FirstBar)
	SHARED SortValue, BarColor
	FOR BarIndex = FirstBar TO FirstBar + 1
		X = BarIndex * 50
		LINE (X, 780)-(X + 40, 480 - SortValue(BarIndex)), BarColor(BarIndex), BF
	NEXT BarIndex
END SUB
