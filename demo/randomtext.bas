10 REM Random Text Demo
20 REM This demo displays text at random positions on the screen.
30 REM Current interpreter limitations:
40 REM - Text color is now supported; each print uses a random color.
50 SCREEN 12: CLS ' Set to highest resolution (640x480)
60
70 FOR I = 1 TO 500 ' Display 500 random messages
80   R = INT(RND * 28) + 1 ' Random row (1-28)
90   C = INT(RND * 73) + 1 ' Leave room for the 7-column message
100  LOCATE R, C
105  COLOR INT(RND * 15) + 1
110  PRINT "BASICA!";
120  SLEEP 20 ' Pause for 20 milliseconds
130 NEXT I
140 COLOR 15: LOCATE 25, 1: PRINT "Demo finished. Press any key to quit."
150 END