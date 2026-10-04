10 SCORE = 55
20 IF SCORE >= 50 THEN
30 PRINT "You passed!"
40 ELSEIF SCORE >= 40 THEN
50 PRINT "You are on the waiting list."
60 ELSE
70 PRINT "Please try again."
80 END IF
90 SCORE = 45
100 IF SCORE >= 50 THEN
110 PRINT "Wrong high branch"
120 ELSEIF SCORE >= 40 THEN
130 PRINT "You are on the waiting list."
140 ELSE
150 PRINT "Wrong low branch"
160 END IF
170 SCORE = 20
180 IF SCORE >= 50 THEN
190 PRINT "Wrong high branch"
200 ELSEIF SCORE >= 40 THEN
210 PRINT "Wrong middle branch"
220 ELSE
230 PRINT "Please try again."
240 END IF
250 IF SCORE = 20 THEN
260 IF SCORE < 30 THEN
270 PRINT "Nested IF passed."
280 ELSE
290 PRINT "Wrong nested branch"
300 END IF
310 ELSE
320 PRINT "Wrong outer branch"
330 END IF
340 IF SCORE < 0 THEN
350 IF SCORE = 20 THEN
360 PRINT "Wrong skipped nested branch"
370 END IF
380 ELSE
390 PRINT "Nested skip passed."
400 END IF
410 CALL CheckScore(45)
420 END
500 SUB CheckScore(SCORE)
510 IF SCORE >= 50 THEN
520 PRINT "Wrong procedure branch"
530 ELSEIF SCORE >= 40 THEN
540 PRINT "Procedure ELSEIF passed."
550 ELSE
560 PRINT "Wrong procedure else"
570 END IF
580 END SUB
