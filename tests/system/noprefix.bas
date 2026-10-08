$NoPrefix
' With $NOPREFIX, QB64 names may be written without their "_": Option
' Explicit is OPTION _EXPLICIT, Round is _ROUND, RGB32 is _RGB32. DIM AS type
' gives every name in its list that type, and $RESIZE:STRETCH is accepted.
DefLng A-Z
Option Explicit
$Resize:Stretch
Dim As Single x, y
Dim As String * 3 code
Dim As Long n, c
x = 1.5: y = x * 2
code = "abcdef"
n = Round(2.5) + Round(3.5)
c = RGB32(255, 255, 255)
Print x; y; code; Len(code); n; c; Hex$(RGB32(1, 2, 3))
System 0
