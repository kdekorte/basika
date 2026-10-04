10 CLS
20 PRINT "BASIKA SOUND AND PLAY DEMO"
30 PRINT
40 PRINT "ORIGINAL POP-STYLE SOUND LOGO"
50 PLAY "MF MN T132 O4 L8 C E G >C. <G E D P8"
60 PLAY "MF ML T132 O4 L8 C E G >C <G E D C"
70 PLAY "MF T132 O4 L8 E. G A >C <A G E D C"
80 PLAY "MF MS T132 O5 L8 C P8 C P8 >C.."
90 PRINT "SOUND: two pitches and the legacy BEEP"
100 SOUND 440, 4
110 SOUND 660, 4
120 BEEP
130 CLS
140 PRINT "PLAY: tempo, note length, octave, and octave shifts"
150 PLAY "MF T150 L8 O4 C D E F G A B > C"
160 PRINT "PLAY: sharps, flats, dotted notes, and rests"
170 PLAY "MF T120 L8 O4 C# D- E. P8 F+ G- A.."
180 PRINT "PLAY: normal, legato, and staccato articulation"
190 PLAY "MF MN T180 L8 O4 C D E F"
200 PLAY "MF ML T180 L8 O4 C D E F"
210 PLAY "MF MS T180 L8 O4 C D E F"
220 PRINT "PLAY: numeric notes and numeric rest (N0)"
230 PLAY "MF T150 L8 N49 N51 N53 N0 N53 N51 N49"
240 PRINT "PLAY: background playback (MB), then foreground (MF)"
250 PLAY "MB T180 L8 O4 C E G > C"
260 PRINT "Background phrase started; foreground phrase waits."
270 PLAY "MF T180 L8 O5 G E C"
280 PLAY "MF P4"
290 PRINT
300 PRINT "Sound demo complete."
310 END
