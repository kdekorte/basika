' ? is shorthand for PRINT everywhere PRINT can go; a ? inside a string
' is just a character.
? "A"; 1; 2
?
? 3: ?4
X = 5: IF X > 1 THEN ? "BIG" ELSE ? "SMALL"
IF X < 1 THEN ? "BIG" ELSE ? "SMALL"
? USING "##.#"; 3.14159
? "Why?"; "?"
OPEN "tests/print_shorthand.tmp" FOR OUTPUT AS #1
?#1, "FILE"; 9
CLOSE #1
OPEN "tests/print_shorthand.tmp" FOR INPUT AS #1
LINE INPUT #1, L$
CLOSE #1
? "[" + L$ + "]"
