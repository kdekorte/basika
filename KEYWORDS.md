# Supported BASIKA Keywords and Syntax

## Complete Keyword Index

The following is the complete keyword set recognized by the lexer:

String values use length-aware heap-backed storage and grow dynamically as
needed. Dynamic strings are the default; fixed-length declarations are available
with `DIM name AS STRING * n` and `DIM name(size) AS STRING * n`.

Numeric literals may be written in hexadecimal, octal or binary: `&HFF`, `&O17`,
`&B1011`. As in QBasic, a literal of up to 16 significant bits is a signed
INTEGER (`&HFFFF` is -1), a wider one is a signed LONG, and a trailing `&`
forces LONG (`&HFFFF&` is 65535). Whole-number decimal literals are exact.
A decimal literal with more than 7 significant digits (`3.14159265358979`) is
DOUBLE, as are literals with a `#` suffix or `D` exponent; an `E` exponent or
`!` suffix keeps it SINGLE.

`ABS`, `AND`, `ARGC`, `ARGV$`, `AS`, `ASC`, `ATN`, `BASE`, `BEEP`, `CALL`, `CASE`, `CHAIN`, `CHDIR`,
`CHR$`, `CIRCLE`, `CLEAR`, `CLOSE`, `CLS`, `COLOR`, `COMMAND$`, `COMMON`, `CONST`, `CONT`, `COS`, `CVD`, `CVI`,
`CVS`, `DATA`, `DATE$`, `DECLARE`, `DEF`, `DEFDBL`, `DEFINT`, `DEFSNG`, `DEFSTR`,
`DELETE`, `DIM`, `DO`, `DRAW`, `ELSE`, `ELSEIF`, `END`, `ENVIRON`, `ENVIRON$`, `EOF`, `ERASE`,
`ERROR`, `EXIT`, `EXP`, `FIELD`, `FILES`, `FIX`, `FOR`, `FUNCTION`, `GET`, `GET$`, `GOSUB`, `GOTO`,
`HEX$`, `IF`, `INKEY$`, `INPUT`, `INPUT$`, `INSTR`, `INT`, `IS`, `KEY`, `KILL`, `LCASE$`,
`LEFT$`, `LEN`, `LET`, `LINE`, `LIST`, `LOC`, `LOCATE`, `LOCK`, `LOF`, `LOG`, `LOOP`, `LSET`,
`LTRIM$`, `MID$`, `MKD$`, `MKDIR`, `MKI$`, `MKS$`, `MOD`, `NAME`, `NEW`, `NEXT`,
`NOT`, `OCT$`, `OFF`, `ON`, `OPEN`, `OPTION`, `OR`, `PAINT`, `PALETTE`, `PCOPY`, `PEEK`, `PEN`, `PLAY`,
`POKE`, `PRESET`, `PRINT`, `PSET`, `PUT`, `QUIT`, `RANDOMIZE`, `READ`, `REM`, `RESTORE`,
`RESUME`, `RETURN`, `REVERSE`, `RIGHT$`, `RMDIR`, `RND`, `RSET`, `RTRIM$`, `RUN`,
`SCREEN`, `SCREENSHOT`, `SEEK`, `SELECT`, `SGN`, `SHARED`, `SHELL`, `SIN`, `SLEEP`, `SOUND`, `SPACE$`,
`SPC`, `SQR`, `STATIC`, `STEP`, `STICK`, `STOP`, `STR$`, `STRIG`, `STRING$`, `SUB`, `SWAP`, `SYSTEM`, `TAB`, `TAN`,
`THEN`, `TIME$`, `TIMER`, `TO`, `TRIM$`, `TYPE`, `UCASE$`, `UNLOCK`, `UNTIL`, `USING`, `VAL`, `VARPTR`,
`VIEW`, `WAIT`, `WEND`, `WHILE`, `WIDTH`, `WINDOW`, `WRITE`, `XOR`, `_AUTODISPLAY`, `_DELAY`, `_DISPLAY`, `_FONT`,
`_FREEFONT`, `_FREEIMAGE`, `_LIMIT`, `_LOADFONT`, `_LOADIMAGE`, `_NEWIMAGE`, `_PRINTSTRING`, `_PRINTWIDTH`, `_PUTIMAGE`,
`_DEFLATE$`, `_INFLATE$`.

QB64-style functions recognized by name: `_RGB32`, `_RGBA32`, `_RGB`, `_RGBA`,
`_RED32`, `_GREEN32`, `_BLUE32`, `_ALPHA32`, `_RED`, `_GREEN`, `_BLUE`, `_ALPHA`,
`_PI`, `_WIDTH`, `_HEIGHT`.

`_AUTODISPLAY OFF` suppresses automatic window presents while drawing commands update the canvas.
Use `_DISPLAY` to present a completed frame, then `_AUTODISPLAY ON` to resume automatic presents.

### Colors and alpha transparency

Graphics commands take QBasic argument lists. Alpha transparency follows QB64:
create a 32-bit image with `SCREEN _NEWIMAGE(width, height, 32)` and every
color is an `&HAARRGGBB` value whose top byte is its alpha. `_RGB32(r, g, b)`
gives an opaque color and `_RGBA32(r, g, b, a)` a translucent one; drawing with
an alpha below 255 blends with what is already on screen, and alpha 0 draws
nothing. Store 32-bit colors in `_UNSIGNED LONG` (or `LONG`) variables, since a
SINGLE cannot hold every 32-bit value exactly.

In every other screen mode colors are palette indexes. `_RGB`/`_RGBA` return a
32-bit color in a 32-bit image and the nearest palette index otherwise;
`_RED`, `_GREEN`, `_BLUE` and `_ALPHA` read the components of a color in the
current mode, while `_RED32` ... `_ALPHA32` always decode `&HAARRGGBB`.

Commands that omit a color draw in the foreground color set by `COLOR`
(15 in most palette modes, opaque white in 32-bit images); `PRESET` and `CLS`
use the background color.

## User-defined types

`TYPE name ... END TYPE` defines a record type. Fields use `field AS typeName`,
where `typeName` can be `STRING`, `INTEGER`, `LONG`, `_UNSIGNED LONG`,
`SINGLE`, `DOUBLE`, or another user-defined type (LONG fields are stored in
records as 4 bytes). Fields may be arrays, and `STRING * n` declares a fixed-width
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
- `SUB subname[(param1[, param2...])] [STATIC] ... END SUB`: Defines a QBasic-style subroutine procedure with isolated local variable scope. With `STATIC`, every local variable, array and record keeps its value between calls. A parameter may be declared `name AS type` with `INTEGER`, `LONG`, `_UNSIGNED LONG`, `SINGLE`, `DOUBLE`, `STRING` or a user-defined type; values passed in are converted to that type.
- `FUNCTION funcname[(param1[, param2...])] [STATIC] ... END FUNCTION`: Defines a QBasic-style function procedure that returns a value by assigning `funcname = expression`. Supports recursion.
- `CALL subname[(arg1[, arg2...])]` or `subname arg1[, arg2...]`: Invokes a subroutine procedure. Simple variable arguments are passed by reference; parenthesized expressions `((x))` are passed by value.
- `DECLARE {SUB | FUNCTION} name[(params)]`: Declare procedure signatures (procedures are also pre-scanned automatically).
- `SHARED var1[()] [AS type][, var2...]`: Grants access to main program global variables from within a procedure block.
- `STATIC var1[()] [AS type][, var2...]`: Inside a `SUB` or `FUNCTION`, keeps the listed local variables (and arrays) between calls.
- Arrays and records `DIM`med in a procedure are local and created on each call; in a `STATIC` procedure they are allocated once.
- `EXIT {SUB | FUNCTION}`: Exits the active procedure prematurely.

## Control Flow and Program Structure
- `END`: Terminates program execution.
- `FOR var = start TO end [STEP step] ... NEXT [var]`: Standard loop.
- `GOSUB line|label ... RETURN`: Subroutine call and return.
- `GOTO line|label`: Unconditional jump.
- `IF condition THEN [line | label | statement] [ELSE statement]`: Conditional execution. Multiline `IF...THEN` blocks support `ELSEIF`, `ELSE`, and `END IF`, including nesting and use in procedures.
- `ON expression {GOTO | GOSUB} line1|label1[, line2|label2...]`: Computed jump.
- `ON KEY(n) GOSUB line`: Enables a key trap for key `n`.
- `ON PEN GOSUB line` / `PEN ON|OFF|STOP`: Traps a light-pen press, which is a left mouse click.
- `ON STRIG(n) GOSUB line` / `STRIG ON|OFF|STOP`: Traps a joystick button press (n = 0, 2, 4, 6).
- `ON TIMER(n) GOSUB line`: Enables a periodic timer trap.
- `ON ERROR GOTO line`: Error trapping.
- `RESUME [0 | NEXT | line | label]`: Error recovery.
- `STOP`: Pauses the program with `Break in N`. In direct mode, `CONT` resumes after the `STOP` with variables, loops and `GOSUB`s intact; Ctrl+C breaks the same way. Run from a file, the program ends there.
- `CONT`: Continues after `STOP` or a Ctrl+C break. Editing the program, or running program lines after the break, gives "Can't continue".
- `WHILE condition ... WEND`: Conditional loop.
- `DO ... LOOP`: Infinite loop; exit with `EXIT DO` or a `GOTO`.
- `DO WHILE condition ... LOOP`: Pre-test loop that repeats while `condition` is true, checked before each iteration.
- `DO UNTIL condition ... LOOP`: Pre-test loop that repeats until `condition` becomes true, checked before each iteration.
- `DO ... LOOP WHILE condition`: Post-test loop; the body always runs at least once, then repeats while `condition` is true.
- `DO ... LOOP UNTIL condition`: Post-test loop; the body always runs at least once, then repeats until `condition` becomes true.
- `EXIT DO`: Exits the innermost active `DO...LOOP` prematurely.
- `SELECT CASE expression ... END SELECT`: Multi-way branch. Evaluates `expression` once, then dispatches to the first matching `CASE` clause; only that branch executes (no fallthrough). Works with both numeric and string expressions. Supports nesting, and use inside `IF`, loop, and `SUB`/`FUNCTION` bodies.
  - `CASE value1[, value2...]`: Matches if `expression` equals any listed value.
  - `CASE value1 TO value2`: Matches if `expression` falls within the inclusive range `value1` to `value2`.
  - `CASE IS comparison-op value`: Matches if `expression comparison-op value` is true, where `comparison-op` is one of `=`, `<>`, `<`, `>`, `<=`, `>=`. `IS` is only recognized as this special comparison marker at the start of a `CASE` item and otherwise remains usable as an ordinary variable name.
  - Multiple comma-separated items (values, `TO` ranges, and `IS` comparisons) may be combined within a single `CASE` clause.
  - `CASE ELSE`: Catch-all branch matched when no preceding `CASE` clause matches.

## Variables and Data
- `DATA constant1[, constant2...]`: Internal data storage.
- `DEF FNname(param) = expression`: Single-line user-defined function.
- `DEFINT letter_range`: Defines variables starting with these letters as integers.
- `DEFSTR letter_range`: Defines variables starting with these letters as strings.
- `DEFSNG letter_range`: Defines variables starting with these letters as single-precision.
- `DEFDBL letter_range`: Defines variables starting with these letters as double-precision.
- `DIM [SHARED] var([lower TO] upper[, ...])`: Array dimensioning (up to 3D). Each dimension may give explicit bounds, such as `DIM grid(-1 TO 1, 1 TO 10)`.
- `DIM [SHARED] name[(bounds)] AS type[, ...]`: Declares a scalar or array of `INTEGER`, `LONG`, `_UNSIGNED LONG`, `SINGLE`, `DOUBLE`, `STRING`, or a user-defined type. The name is then used without a suffix (`DIM count AS INTEGER` makes `count` and `count%` the same variable). Declarations inside a `SUB` or `FUNCTION` are local to it.
- `DIM SHARED ...`: At module level, makes the declared variables and arrays visible inside every `SUB` and `FUNCTION` without a `SHARED` statement.
- Type suffixes: `%` INTEGER (16-bit), `&` LONG (32-bit), `~&` _UNSIGNED LONG (0 to 4294967295, wraps like QB64), `!` SINGLE, `#` DOUBLE, `$` STRING. INTEGER and LONG assignments round half to even like QBasic (`2.5` becomes 2, `3.5` becomes 4) and report Overflow when out of range. `AND`, `OR`, `XOR` and `NOT` keep all 32 bits of LONG values.
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
- `WIDTH [columns][, rows]`: Sets the text grid to 40 or 80 columns and 25, 30, 43, 50 or 60 rows. `WIDTH #n, width` and `WIDTH "device", width` are accepted; file output is not wrapped.
- `COLOR [foreground][, background]`: Sets the text color and the default graphics colors. Primitives drawn without a color use the foreground; `CLS` and `PRESET` use the background. In a 32-bit image both are `&HAARRGGBB` colors.
- `INPUT ["prompt"{,|;}] [#n,] var1[, var2...]`: Reads comma-separated values; a value may be a quoted string containing commas. From the keyboard, a wrong number of values or a non-numeric value for a numeric variable prints `?Redo from start` and asks again.
- `LINE INPUT [;] ["prompt";] [#n,] var$`: Reads a whole line, commas and quotes included, into a string variable.
- `WRITE [#n,] expr[, expr...]`: Writes values separated by commas, with strings in quotes and numbers without padding spaces, then a newline (the format `INPUT #` reads back).
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
- `CLOSE [[#]n[, [#]n...]]`: Closes the listed files, or every open file when none are given.
- `DELETE "file" | line`: Deletes a file or program line.
- `ENVIRON "VAR=VALUE"`: Sets environment variables.
- `FILES ["pattern"] [, "output_file"]`: Lists files (wildcards supported). Output shows modification date/time, size (or `<DIR>`), and filename — formatted similar to DOS/BASICA. If `"output_file"` is provided, filenames are written to that file (one per line).
- `FIELD #n, len AS var$[, len AS var$...]`: Defines record fields for random-file buffers.
- `GET #n[, [record][, variable]]`: Reads a record into a `TYPE` variable or, with no variable, into the `FIELD` buffer. An omitted record number reads the next record. BASIKA also accepts `GET #n, record, length, var$`, which reads `length` bytes at record stride `length`.
- `KILL "pattern"`: Deletes files using wildcards.
- `MKDIR "path"`: Creates a directory.
- `NAME "old" AS "new"`: Renames a file.
- `OPEN file$ [FOR mode] [ACCESS ...] AS [#]n [LEN = reclen]`: Opens a file for `INPUT`, `OUTPUT`, `APPEND`, `RANDOM` (the default; `RWB` is a synonym) or `BINARY`. RANDOM files hold records of `reclen` bytes (default 128): record n starts at byte (n - 1) × reclen, and a `TYPE` record larger than `reclen` is a "Bad record length". BINARY files are addressed by byte position (starting at 1). RANDOM and BINARY files are created if missing and never truncated. `ACCESS` and lock clauses are accepted and ignored. The GW-BASIC form `OPEN mode$, [#]n, file$[, reclen]` (mode `I`, `O`, `A`, `R` or `B`) is also accepted.
- Records are stored packed and little-endian: INTEGER 2 bytes, LONG and `_UNSIGNED LONG` 4, SINGLE 4 (IEEE), DOUBLE 8, `STRING * n` n bytes padded with spaces; reading past the end of the file gives zeros.
- `PUT #n[, [record][, variable]]`: Writes a `TYPE` variable or, with no variable, the `FIELD` buffer (which may not exceed the record length). An omitted record number writes the next record; record numbers below 1 are a "Bad record number". BASIKA also accepts `PUT #n, record, length, data$`.
- `RMDIR "path"`: Removes a directory.
- `SEEK #n, pos`: Sets file position.
- `SHELL ["command"]`: Executes a system command.
- `SYSTEM` / `QUIT`: Ends the program and exits the interpreter, from direct mode or a running program.
- `CHAIN file$`: Ends this program and runs another (`.bas` is added if needed). Variables listed in `COMMON` pass to the new program's `COMMON` variables by position, and open files stay open.
- `COMMON [SHARED] var[()] [AS type][, ...]`: Lists the module variables passed on by `CHAIN`; with `SHARED` they are also visible in every procedure. Named blocks (`COMMON /name/`) are accepted.
- `CLEAR [, [memory][, stack]]`: Zeroes all variables and arrays (keeping their dimensions), empties strings, closes all files and resets the GOSUB/FOR stack. `CONST` values, `TYPE`s and `DEF FN`s are kept. Not allowed inside a procedure.
- `LOCK [#]n[, record | [first] TO last]` / `UNLOCK ...`: Locks a whole file, a record or a range of records (bytes for BINARY files) with operating-system advisory locks; a range another process holds is a "Permission denied" error.
- `WAIT port, and_mask[, xor_mask]`: Pauses until `(port XOR xor_mask) AND and_mask` is nonzero. Ports are simulated: `&H3DA` reports a 60 Hz vertical retrace in bits 8 and 1, so `WAIT &H3DA, 8` syncs animation to frames; every other port reads 0, and a `WAIT` that could never end returns at once.

## Graphics and Sound
Coordinates written `STEP(dx, dy)` are relative to the last point referenced,
which every graphics command updates (after `SCREEN` it is the screen center).
Empty arguments keep their defaults, for example `LINE (0,0)-(9,9), , B` or
`CIRCLE (50,50), 20, , , , .5`.

- `CIRCLE [STEP](x,y), radius[, [color][, [start][, [end][, aspect]]]]`: Draws a circle, ellipse or arc. `start` and `end` are angles in radians from -2π to 2π, measured counterclockwise from 3 o'clock; a negative angle also draws a radius line to that end of the arc, making a pie wedge. `aspect` is the ratio of vertical to horizontal radius (below 1, `radius` is horizontal; above 1, it is vertical). The default aspect makes circles look round on screen. Fill a shape with `PAINT`.
- `DRAW "mml"`: String-driven graphics command.
- `GET (x1,y1)-(x2,y2), array`: Captures a screen area into a numeric array.
- `LINE [[STEP](x1,y1)]-[STEP](x2,y2)[, [color][, [B|BF][, style]]]`: Draws a line, a box (`B`) or a filled box (`BF`). Without a first point the line starts at the last point referenced. `style` is a 16-bit mask such as `&HF0F0`: each set bit, starting from the most significant, draws a pixel and each clear bit skips one. It applies to lines and `B` boxes.
- `PAINT [STEP](x,y)[, [color | tile$][, [border][, background$]]]`: Flood-fills the area around the point up to the `border` color (default: the fill color, or the foreground color for tiles). A translucent color in a 32-bit image blends with the pixels it covers. `tile$` paints a pattern of up to 64 bytes: in SCREEN 1 each byte is a row of four 2-bit pixels; in SCREEN 7-12 each row is four bytes, one per bit plane; in 256-color modes each row is eight bytes, one per pixel; in SCREEN 2 and 32-bit images each byte is a row of eight 1-bit pixels drawn in the foreground (set) and background (clear) colors. `background$` is accepted for compatibility; tiled fills track the pixels they visit, so it is not needed.
- `PLAY "mml"`: Plays Music Macro Language. Supports tempo (`T`), octave (`O`, `<`, `>`), default note length (`L`), volume (`V0`-`V15`), sharps/flats, numeric notes (`N0`-`N84`), rests (`P`/`R`), dotted notes, and normal/legato/staccato articulation (`MN`/`ML`/`MS`). `MB` plays in the background; `MF` waits for playback to finish.
- `POINT(x, y)`: Returns the color of a pixel (a palette index, or `&HFFRRGGBB` in a 32-bit image), or -1 outside the screen or view. `POINT(0)`/`POINT(1)` return the physical x/y and `POINT(2)`/`POINT(3)` the `WINDOW` x/y of the last point referenced.
- `PRESET [STEP](x,y)[, color]`: Sets a pixel, in the background color when no color is given.
- `PSET [STEP](x,y)[, color]`: Sets a pixel, in the foreground color when no color is given.
- `PUT (x,y), array[, action]`: Places a captured area on the screen. Actions: `PSET`, `PRESET`, `AND`, `OR`, `XOR` (default).
- `SCREEN [mode][, [colorswitch][, [apage][, [vpage]]]]`: Sets the graphics mode and the active (drawn) and visual (shown) pages. Modes 0 and 7 have 8 pages, 8 has 4, 9 has 2 and the others 1. Repeating the current mode only switches pages, so programs can flip pages every frame.
- `PCOPY source, destination`: Copies one screen page onto another (double buffering).
- `PALETTE [attribute, color]`: Changes a palette entry, recoloring pixels already on screen; with no arguments, restores the default palette. In SCREEN 12, 13 and 256-color images, `color` is red + 256 * green + 65536 * blue with 0-63 components; in SCREEN 0 and 9 it is a 6-bit EGA value; in other modes, one of the 16 default colors. Not available in 32-bit images.
- `PALETTE USING array(start)`: Sets every palette entry from consecutive array elements; an element of -1 leaves its entry unchanged.
- `PEN(n)`: The mouse as a light pen: 0 is -1 if pressed since the last `PEN(0)`; 1/2 the x/y of the last press; 3 is -1 while the button is down; 4/5 the current x/y; 6/7 the text row/column of the last press; 8/9 the current row/column.
- `STICK(n)`: Joystick position from 1 to 200 (100 at rest): 0/1 are joystick A's x/y and 2/3 joystick B's; 0 when no joystick is attached. `STRIG` reports the joysticks' first two buttons.
- `SCREEN _NEWIMAGE(width, height[, mode])`: Creates a graphics screen of any size. `mode` 32 makes a 32-bit image with alpha colors; any other value uses the 256-color palette.
- `SCREENSHOT "filename.png"`: Saves the current graphics window content to a file. Supports `.png` and `.jpg`/`.jpeg` extensions.
- `_FONT handle`: Sets the active font to the loaded font specified by `handle` (or `0` to restore default font).
- `_FREEFONT handle`: Frees a loaded font handle.
- `_LOADFONT("fontfile.ttf", size)`: Loads a TTF/OTF font file at the specified pixel size and returns a numeric font handle.
- `_PRINTSTRING (x, y), text$`: Draws `text$` at pixel coordinates `(x, y)` using the current `COLOR`. Does not move the text cursor or scroll. Works like QB64's `_PRINTSTRING`.
- `_PRINTWIDTH(text$)`: Returns the pixel width that `text$` would occupy when rendered with the current font. Useful for centering text or layout calculations.
- `SLEEP [seconds]`: Pauses for the given number of seconds or until a key is pressed (the key stays available to `INKEY$`). With no argument, or 0, it waits for a key.
- `_DELAY seconds`: Pauses for a number of seconds, which may be fractional (`_DELAY .05`), without waking on key presses.
- `_LIMIT fps`: Placed once in a loop, holds the loop to at most `fps` iterations per second, for steady animation frame rates.
- `SOUND freq, duration`: Produces a tone for a duration in 18.2-Hz timer ticks. Frequency must be 37-32767 and duration 0-65535.
- `VIEW [(x1,y1)-(x2,y2)[, [fillcolor][, border]]]`: Defines a physical viewport (in screen pixels). All subsequent graphics commands are clipped to this region. Coordinates are relative to the viewport origin unless `VIEW SCREEN` is used (absolute). Omit coordinates to reset.
- `WINDOW [(x1,y1)-(x2,y2)]`: Maps a custom logical coordinate system onto the current viewport. After this call, all graphics commands accept logical coordinates. `(x1,y1)` is the bottom-left and `(x2,y2)` is the top-right by default (Y increases upward, like math). Use `WINDOW SCREEN` to keep Y increasing downward. Omit coordinates to reset to screen coordinates.
- `_LOADIMAGE("filename", mode)`: Loads an image file and returns a numeric image handle. The optional mode is accepted for QB64 compatibility.
- `_PUTIMAGE (x1,y1), handle`: Draws an image at the destination position. A destination rectangle can be supplied as `(x1,y1)-(x2,y2)` to scale the image; source rectangles are supported with the corresponding QB64 syntax.
- `_FREEIMAGE handle`: Releases a loaded image handle so its texture resources can be reused.
- `_RGB32(r, g, b[, a])`, `_RGB32(gray[, a])`, `_RGBA32(r, g, b, a)`: Return a 32-bit `&HAARRGGBB` color. Channels are clamped to 0-255.
- `_RGB(r, g, b)`, `_RGBA(r, g, b, a)`: A 32-bit color in a 32-bit image, otherwise the nearest palette index.
- `_RED32(c)`, `_GREEN32(c)`, `_BLUE32(c)`, `_ALPHA32(c)`: Channels of a 32-bit color. `_RED(c)`, `_GREEN(c)`, `_BLUE(c)`, `_ALPHA(c)` do the same for a color in the current mode, looking palette indexes up in the palette.
- `_PI[(multiplier)]`: π in double precision, optionally multiplied (`_PI(2)` is 2π).
- `_WIDTH`, `_HEIGHT`: Width and height of the graphics screen in pixels.
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
- `LOC(n)`: For RANDOM files, the last record read or written; for BINARY files, the last byte; otherwise the current byte position in file #n.
- `ASC(s$)`: ASCII value of first character.
- `INSTR([start,] s1$, s2$)`: String search.
- `LEN(s$)`: String length.
- `PEEK(addr)`: Read memory byte.
- `TIMER`: Wall-clock seconds since midnight, with fractions.
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
- `HEX$(n)` / `OCT$(n)`: Hex/Octal representation. Negative values use 16-bit two's complement when they fit an INTEGER (`HEX$(-1)` is `FFFF`) and 32-bit otherwise; positive values up to 32 bits print in full, so `HEX$` shows `&HAARRGGBB` colors.
- `INKEY$`: Read single keypress.
- `INPUT$(n[, [#]file])`: Reads n characters from a file, or waits for n keys without echoing them (Enter is `CHR$(13)`).
- `ERDEV`, `ERDEV$`: Device error code and device name; no devices are simulated, so they are always 0 and "".
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
- `LIST [first][-[last]]`: Lists the program, a single line, or a range (`LIST 20-40`, `LIST -40`, `LIST 20-`).
- `SAVE file$[, A]`, `LOAD file$[, R]`, `MERGE file$`: Save the program as text, replace it with a file (and run it with `R`), or add a file's lines to it. `.bas` is added when the name has no extension.
- `RENUM [new][, [old][, increment]]`: Renumbers the lines from `old` on, starting at `new` (default 10, 10), and updates the line numbers after `GOTO`, `GOSUB`, `THEN`, `ELSE`, `RESTORE`, `RESUME` and `RUN` and in `ON ... GOTO/GOSUB` lists.
- `AUTO [start][, increment]`: Offers line numbers for typing a program; an empty line or Ctrl+C ends it.
- `EDIT line`: Offers that line for editing (in the window it is pre-filled; in a terminal it is shown to retype).
- `SAVE`, `LOAD`, `MERGE`, `RENUM`, `AUTO` and `EDIT` are direct-mode commands, not reserved words.
- `RUN [line]`: Clears variables and runs the program from the start or from `line`.
- Direct mode: lines typed without a number run immediately, with colons, one-line loops, `GOSUB` and calls; `GOTO line` continues into the program without clearing variables. Typing a line number alone deletes that line. Errors in direct mode are reported without a line number.
