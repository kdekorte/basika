' PRINT USING follows QBasic: literal text around fields, the format reused
' per value, number and string fields, _ escapes, % on overflow. Formats
' with no fields may be C printf formats (a Basika extension).
PRINT USING "t=##.## x=###"; 1.5; 42
PRINT USING "t=##.##"; 1.5
PRINT USING "###.##"; 12.345, 7
PRINT USING "##.##"; 1.2, 3.4
PRINT USING "Total: $$#,###.## due"; 1234.5
PRINT USING "**$##.##"; 5.25
PRINT USING "+##.##"; 5; -5
PRINT USING "##.##-"; -3.5; 3.5
PRINT USING "##.##+"; -3.5; 3.5
PRINT USING "#.##"; .5; -.5
PRINT USING ".##"; .25
PRINT USING "##"; 123
PRINT USING "##.##^^^^"; 234.56
PRINT USING "+#.###^^^^^"; -0.000123
PRINT USING "!-!"; "Hello", "World"
PRINT USING "[\  \]"; "Basika"; "Hi"
PRINT USING "Name: &, age ##"; "Al", 30
PRINT USING "##.#% done"; 42.5
PRINT USING "_#_## ##"; 7
PRINT USING "a=# b=# end"; 1;
PRINT
PRINT USING "%6.2f"; 3.14159
PRINT USING "%s!"; "hi"
PRINT USING "[%5d]"; 42.6
PRINT USING "Value: %g"; 12.345
ON ERROR GOTO bad
PRINT USING "##"; "text"
PRINT USING "no fields"; 5
PRINT USING "%s"; 5
END
bad: PRINT "error"; ERR: RESUME NEXT
