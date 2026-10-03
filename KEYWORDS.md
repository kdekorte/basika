# Supported BASIKA Keywords and Syntax

## Complete Keyword Index

The following is the complete keyword set recognized by the lexer:

String values use length-aware heap-backed storage and grow dynamically as
needed. Dynamic strings are the default; fixed-length declarations are available
with `DIM name AS STRING * n` and `DIM name(size) AS STRING * n`.

`ABS`, `AND`, `ARGC`, `ARGV$`, `AS`, `ASC`, `ATN`, `BASE`, `BEEP`, `CALL`, `CHDIR`,
`CHR$`, `CIRCLE`, `CLOSE`, `CLS`, `COLOR`, `COMMAND$`, `COS`, `CVD`, `CVI`,
`CVS`, `DATA`, `DATE$`, `DECLARE`, `DEF`, `DEFDBL`, `DEFINT`, `DEFSNG`, `DEFSTR`,
`DELETE`, `DIM`, `DRAW`, `ELSE`, `END`, `ENVIRON`, `ENVIRON$`, `EOF`, `ERASE`,
`ERROR`, `EXIT`, `EXP`, `FIELD`, `FILES`, `FIX`, `FOR`, `FUNCTION`, `GET`, `GET$`, `GOSUB`, `GOTO`,
`HEX$`, `IF`, `INKEY$`, `INPUT`, `INSTR`, `INT`, `KEY`, `KILL`, `LCASE$`,
`LEFT$`, `LEN`, `LET`, `LINE`, `LIST`, `LOC`, `LOCATE`, `LOF`, `LOG`, `LSET`,
`LTRIM$`, `MID$`, `MKD$`, `MKDIR`, `MKI$`, `MKS$`, `MOD`, `NAME`, `NEW`, `NEXT`,
`NOT`, `OCT$`, `OFF`, `ON`, `OPEN`, `OPTION`, `OR`, `PAINT`, `PEEK`, `PLAY`,
`POKE`, `PRINT`, `PSET`, `PUT`, `QUIT`, `RANDOMIZE`, `READ`, `REM`, `RESTORE`,
`RESUME`, `RETURN`, `REVERSE`, `RIGHT$`, `RMDIR`, `RND`, `RSET`, `RTRIM$`, `RUN`,
`SCREEN`, `SCREENSHOT`, `SEEK`, `SGN`, `SHARED`, `SHELL`, `SIN`, `SLEEP`, `SOUND`, `SPACE$`,
`SPC`, `SQR`, `STATIC`, `STEP`, `STR$`, `STRIG`, `STRING$`, `SUB`, `SWAP`, `SYSTEM`, `TAB`, `TAN`,
`THEN`, `TIME$`, `TIMER`, `TO`, `TRIM$`, `TYPE`, `UCASE$`, `USING`, `VAL`, `VARPTR`,
`VIEW`, `WEND`, `WHILE`, `WINDOW`, `XOR`, `_AUTODISPLAY`, `_DISPLAY`, `_FONT`,
`_FREEFONT`, `_FREEIMAGE`, `_LOADFONT`, `_LOADIMAGE`, `_NEWIMAGE`, `_PRINTSTRING`, `_PRINTWIDTH`, `_PUTIMAGE`,
`_DEFLATE$`, `_INFLATE$`.

`_AUTODISPLAY OFF` suppresses automatic window presents while drawing commands update the canvas.
Use `_DISPLAY` to present a completed frame, then `_AUTODISPLAY ON` to resume automatic presents.

`PSET`, `LINE`, `CIRCLE`, and `PAINT` accept an optional final alpha value from
0 (transparent) to 255 (opaque). Omitting alpha preserves opaque drawing.

## User-defined types

`TYPE name ... END TYPE` defines a record type. Fields use `field AS typeName`,
where `typeName` can be `STRING`, `INTEGER`, `SINGLE`, `DOUBLE`, or another
user-defined type. Fields may be arrays, and `STRING * n` declares a fixed-width
string field. Declare scalar or array instances with `DIM variable AS typeName`
or `DIM variable(size) AS typeName`, then read or assign fields with chained
member notation such as `variable.location.x = 10` or
`variable.samples(2) = 5`. Whole records of the same type can be assigned with
`destination = source`. Typed `SUB`/`FUNCTION` parameters accept UDT variables
by reference; parenthesizing a scalar argument passes a copy. `GET #file,
record, variable` and `PUT #file, record, variable` support random-record I/O
for UDTs containing numeric fields and fixed-length strings, including arrays
and nested types. UDT file records use packed, little-endian field storage;
dynamic string fields cannot be written as records.

## Line Numbers and Labels

Line numbers are optional. Programs may use traditional line numbers (`10 PRINT "Hi"`),
labels (`my_label: PRINT "Hi"`), or free-form lines with no prefix at all. When
line numbers are omitted, statements receive internal line numbers in increments of 10.
Console errors report the physical file line for unnumbered statements; `ERL` continues
to return the internal line number.

Labels are identifiers followed by a colon (`my_label:`). They may appear at the start
of a line, optionally sharing the line with a statement (`loop: PRINT X`). Labels can
be used anywhere a line number is accepted: `GOTO`, `GOSUB`, `ON...GOTO/GOSUB`,
`IF...THEN`, and `ON ERROR GOTO`.

## Subroutines and Functions (QBasic Procedures)
- `SUB subname[(param1[, param2...])] [STATIC] ... END SUB`: Defines a QBasic-style subroutine procedure with isolated local variable scope.
- `FUNCTION funcname[(param1[, param2...])] [STATIC] ... END FUNCTION`: Defines a QBasic-style function procedure that returns a value by assigning `funcname = expression`. Supports recursion.
- `CALL subname[(arg1[, arg2...])]` or `subname arg1[, arg2...]`: Invokes a subroutine procedure. Simple variable arguments are passed by reference; parenthesized expressions `((x))` are passed by value.
- `DECLARE {SUB | FUNCTION} name[(params)]`: Declare procedure signatures (procedures are also pre-scanned automatically).
- `SHARED var1[, var2...]`: Grants access to main program global variables from within a procedure block.
- `STATIC var1[, var2...]`: Declares static local variables preserved across procedure calls.
- `EXIT {SUB | FUNCTION}`: Exits the active procedure prematurely.

## Control Flow and Program Structure
- `END`: Terminates program execution.
- `FOR var = start TO end [STEP step] ... NEXT [var]`: Standard loop.
- `GOSUB line|label ... RETURN`: Subroutine call and return.
- `GOTO line|label`: Unconditional jump.
- `IF condition THEN [line | label | statement] [ELSE statement]`: Conditional execution.
- `ON expression {GOTO | GOSUB} line1|label1[, line2|label2...]`: Computed jump.
- `ON KEY(n) GOSUB line`: Enables a key trap for key `n`.
- `ON TIMER(n) GOSUB line`: Enables a periodic timer trap.
- `ON ERROR GOTO line`: Error trapping.
- `RESUME [0 | NEXT | line]`: Error recovery.
- `STOP`: Halts execution (can be resumed with `CONT`).
- `WHILE condition ... WEND`: Conditional loop.

## Variables and Data
- `DATA constant1[, constant2...]`: Internal data storage.
- `DEF FNname(param) = expression`: Single-line user-defined function.
- `DEFINT letter_range`: Defines variables starting with these letters as integers.
- `DEFSTR letter_range`: Defines variables starting with these letters as strings.
- `DEFSNG letter_range`: Defines variables starting with these letters as single-precision.
- `DEFDBL letter_range`: Defines variables starting with these letters as double-precision.
- `DIM var(dim1[, dim2, dim3])`: Array dimensioning (up to 3D).
- `DIM name AS STRING * n`: Declares a fixed-width scalar string padded with spaces.
- `DIM name(size) AS STRING * n`: Declares a fixed-width string array; assignments are padded or truncated to `n` characters.
- `ERASE var`: Reinitializes variables or arrays.
- `LET var = expression`: Assignment (keyword is optional).
- `OPTION BASE {0 | 1}`: Sets minimum array subscript.
- `READ var1[, var2...]`: Reads from `DATA` statements.
- `RESTORE [line]`: Resets `DATA` pointer.
- `SWAP var1, var2`: Exchanges values of two variables or array elements.

## Operators
- `AND`: Bitwise logical AND.
- `OR`: Bitwise logical OR.
- `XOR`: Bitwise logical XOR.

## Input and Output
- `BEEP`: Sounds the speaker.
- `CLS`: Clears the screen.
- `COLOR foreground[, background]`: Sets text or graphics colors.
- `INPUT ["prompt"{,|;}] [#n,] var1[, var2...]`: User input.
- `KEY(n) ON|OFF|STOP`: Enables, disables, or stops a key trap for key `n`.
- `LOCATE row, col`: Positions the cursor.
- `LSET var$ = expression$`: Left-justifies a string into a field buffer or string variable.
- `PRINT [#n,] [USING "fmt";] [expressions] [,|;]`: Output to screen or file.
- `RSET var$ = expression$`: Right-justifies a string into a field buffer or string variable.
- `SPC(n)`: Used in `PRINT` to output spaces.
- `TAB(n)`: Used in `PRINT` to move to a specific column.
- `TIMER ON|OFF|STOP`: Controls timer trapping.

## File and System Operations
- `CHDIR "path"`: Changes current directory.
- `CLOSE [#n]`: Closes files.
- `DELETE "file" | line`: Deletes a file or program line.
- `ENVIRON "VAR=VALUE"`: Sets environment variables.
- `FILES ["pattern"] [, "output_file"]`: Lists files (wildcards supported). Output shows modification date/time, size (or `<DIR>`), and filename — formatted similar to DOS/BASICA. If `"output_file"` is provided, filenames are written to that file (one per line).
- `FIELD #n, len AS var$[, len AS var$...]`: Defines record fields for random-file buffers.
- `GET #n, record[, length, var$]`: Binary/Random file input. If length and `var$` are omitted, the current field buffer for `#n` is read.
- `KILL "pattern"`: Deletes files using wildcards.
- `MKDIR "path"`: Creates a directory.
- `NAME "old" AS "new"`: Renames a file.
- `OPEN "file" FOR mode AS #n`: Opens a file (INPUT, OUTPUT, RANDOM, RWB).
- `PUT #n, record[, length, data$]`: Binary/Random file output. If length and `data$` are omitted, the current field buffer for `#n` is written.
- `RMDIR "path"`: Removes a directory.
- `SEEK #n, pos`: Sets file position.
- `SHELL ["command"]`: Executes a system command.
- `SYSTEM` / `QUIT`: Exits the interpreter.

## Graphics and Sound
- `CIRCLE (x,y), radius[, color[, fill[, alpha]]]`: Draws a circle. Use `2` for solid fill.
- `DRAW "mml"`: String-driven graphics command.
- `GET (x1,y1)-(x2,y2), array`: Captures a screen area into a numeric array.
- `LINE [(x1,y1)]-(x2,y2)[, [color][, [B|BF][, alpha]]]`: Draws lines or boxes. Alpha may follow color directly when no box mode is specified.
- `PAINT (x,y)[, color[, border[, alpha]]]`: Area fill.
- `PLAY "mml"`: Plays Music Macro Language.
- `PSET (x,y)[, color[, alpha]]`: Sets a pixel.
- `PUT (x,y), array[, action]`: Places a captured area on the screen. Actions: `PSET`, `PRESET`, `AND`, `OR`, `XOR` (default).
- `SCREEN mode`: Sets graphics mode.
- `SCREENSHOT "filename.png"`: Saves the current graphics window content to a file. Supports `.png` and `.jpg`/`.jpeg` extensions.
- `_FONT handle`: Sets the active font to the loaded font specified by `handle` (or `0` to restore default font).
- `_FREEFONT handle`: Frees a loaded font handle.
- `_LOADFONT("fontfile.ttf", size)`: Loads a TTF/OTF font file at the specified pixel size and returns a numeric font handle.
- `_PRINTSTRING (x, y), text$`: Draws `text$` at pixel coordinates `(x, y)` using the current `COLOR`. Does not move the text cursor or scroll. Works like QB64's `_PRINTSTRING`.
- `_PRINTWIDTH(text$)`: Returns the pixel width that `text$` would occupy when rendered with the current font. Useful for centering text or layout calculations.
- `SLEEP ms`: Pauses for a specified number of milliseconds.
- `SOUND freq, duration`: Produces a tone.
- `VIEW [(x1,y1)-(x2,y2)[, [fillcolor][, border]]]`: Defines a physical viewport (in screen pixels). All subsequent graphics commands are clipped to this region. Coordinates are relative to the viewport origin unless `VIEW SCREEN` is used (absolute). Omit coordinates to reset.
- `WINDOW [(x1,y1)-(x2,y2)]`: Maps a custom logical coordinate system onto the current viewport. After this call, all graphics commands accept logical coordinates. `(x1,y1)` is the bottom-left and `(x2,y2)` is the top-right by default (Y increases upward, like math). Use `WINDOW SCREEN` to keep Y increasing downward. Omit coordinates to reset to screen coordinates.
- `_LOADIMAGE("filename", mode)`: Loads an image file and returns a numeric image handle. The optional mode is accepted for QB64 compatibility.
- `_PUTIMAGE (x1,y1), handle`: Draws an image at the destination position. A destination rectangle can be supplied as `(x1,y1)-(x2,y2)` to scale the image; source rectangles are supported with the corresponding QB64 syntax.
- `_FREEIMAGE handle`: Releases a loaded image handle so its texture resources can be reused.
- `_DEFLATE$(text$)`: Compresses a dynamic string with zlib and returns a lossless encoded compressed string.
- `_INFLATE$(data$)`: Decompresses a string returned by `_DEFLATE$`, including multi-megabyte values; invalid input returns an empty string.

## Numeric Functions
- `ABS(n)`, `SQR(n)`, `SIN(n)`, `COS(n)`, `TAN(n)`, `ATN(n)`
- `EXP(n)`, `LOG(n)`, `INT(n)`, `FIX(n)`, `RND[(n)]`, `SGN(n)`
- `ARGC`: Number of command-line arguments.
- `ARGV$(index)`: Returns a command-line argument by index.
- `CVI(s$)`, `CVS(s$)`, `CVD(s$)`: Convert a binary string to integer, single, or double precision.
- `EOF(n)`: Returns nonzero when file #n is at end of file.
- `LOF(n)`: Length of file #n in bytes.
- `LOC(n)`: Current position in file #n.
- `ASC(s$)`: ASCII value of first character.
- `INSTR([start,] s1$, s2$)`: String search.
- `LEN(s$)`: String length.
- `PEEK(addr)`: Read memory byte.
- `TIMER`: Seconds since midnight/startup.
- `VAL(s$)`: Numeric value of string.
- `VARPTR(var)`: Address of a variable.

## String Functions

All string variables are dynamic and may grow beyond the legacy parser buffer
size. String functions preserve the full value when assigning or returning
dynamic strings.
- `CHR$(n)`: Character from ASCII code.
- `COMMAND$`: Returns the command-line tail passed to the program.
- `DATE$`: Current system date.
- `ENVIRON$(name | index)`: Retrieve environment variable.
- `GET$(#n, record, length)`: Read string from file.
- `HEX$(n)` / `OCT$(n)`: Hex/Octal representation.
- `INKEY$`: Read single keypress.
- `LCASE$(s$)` / `UCASE$(s$)`: Case conversion.
- `LEFT$(s$, n)`, `RIGHT$(s$, n)`, `MID$(s$, start[, n])`: Substring.
- `LTRIM$(s$)`, `RTRIM$(s$)`, `TRIM$(s$)`: Whitespace removal.
- `MKD$(n)`, `MKI$(n)`, `MKS$(n)`: Convert numeric values to binary strings.
- `REVERSE(s$)`: Reverses a string.
- `SPACE$(n)`: Returns string of spaces.
- `STR$(n)`: Converts number to string.
- `TIME$`: Current system time.

## Memory and Special
- `POKE addr, value`: Write byte to memory.

## Misc
- `REM` or `'`: Comments.
- `LIST`: Lists the program.
- `RUN`: Starts program execution.
