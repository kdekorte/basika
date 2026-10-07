# CHANGELOG

All notable changes to this project are recorded in this file.

## Unreleased

- **QBASIC Graphics Syntax (breaking)**: `PSET`, `PRESET`, `LINE`, `CIRCLE`, and `PAINT` now take QBasic argument lists. `CIRCLE` adds `start`/`end` arc angles (negative angles draw pie-wedge radius lines) and `aspect` ellipses, with QBasic's default aspect so circles look round in 200- and 350-line modes; its old fill argument is gone, so fill shapes with `PAINT`. `LINE` adds the 16-bit `style` mask, `PAINT` adds tile strings and the `background$` argument, and every primitive accepts `STEP(dx, dy)` relative coordinates and empty arguments. Primitives without a color use the `COLOR` foreground, `PRESET`/`CLS` use the background, `POINT(2)`/`POINT(3)` return logical coordinates, and outlines are rasterized pixel by pixel so each pixel is drawn once. The old trailing alpha arguments were removed; demos and tests were updated.
- **QB64 32-bit Color and Alpha**: `SCREEN _NEWIMAGE(w, h, 32)` creates a 32-bit image whose colors are `&HAARRGGBB` values; translucent colors blend in every primitive, including `PAINT`. Added `_RGB32`, `_RGBA32`, `_RGB`, `_RGBA`, `_RED32`/`_GREEN32`/`_BLUE32`/`_ALPHA32`, `_RED`/`_GREEN`/`_BLUE`/`_ALPHA`, `_PI`, `_WIDTH`, and `_HEIGHT`. `PAINT` inside a `VIEW` reads back only the view.
- **Timing (breaking)**: `SLEEP` now takes seconds and wakes on a key press as in QBasic; `SLEEP` with no argument waits for a key. Added `_DELAY seconds` and `_LIMIT fps`. `TIMER` now returns wall-clock seconds since midnight instead of CPU time.
- **Typed Declarations**: Added `DIM name AS INTEGER | LONG | _UNSIGNED LONG | SINGLE | DOUBLE | STRING` for scalars and arrays, `DIM SHARED`, `lower TO upper` array bounds, typed `SUB`/`FUNCTION` parameters that convert their arguments, `SHARED name AS type`, and `LONG`/`_UNSIGNED LONG` `TYPE` fields (4 bytes in records). Added the `&` (LONG) and `~&` (_UNSIGNED LONG) suffixes.
- **Numeric Literals**: Added `&H`, `&O`, and `&B` literals with QBasic INTEGER/LONG typing; whole-number literals are now exact; `HEX$`/`OCT$` print 32-bit values; `AND`/`OR`/`XOR`/`NOT` keep all bits of LONG values.
- **Fixes**: String parameters and locals of `SUB`/`FUNCTION` calls are now released when their call frame is reused; previously each call leaked a string-pool slot until the 2048-slot pool ran out and string expressions failed with a misleading "Syntax error" (the showcase demo hit this after about 12 seconds). The pool now falls back to the heap when full. `LEN` of a string local or parameter inside a procedure returned the length of the module-level variable of the same name instead. `CONST` names passed to procedures are passed by value instead of as empty variables; statements after `CASE ELSE:` on the same line now run; `RESUME label` works; `WINDOW` without a prior `VIEW` maps coordinates correctly; `PUT` honors the `AND`, `OR`, and `XOR` actions.
- **Interpreter Performance**: The run loop no longer copies each statement's first token, cutting the `basica.bas` benchmark from about 1.04 s to 0.78 s on the same machine.
- **Graphics Showcase**: Rewrote `demo/graphics_primitives_showcase.bas` as an animated 32-bit scene (aurora, RGB alpha orbs, a ringed planet built from `CIRCLE` arcs, a comet with particle sparks, and cards demonstrating pie wedges, `LINE` styles, `PAINT` tiles, and alpha) written with `CONST`, `TYPE`, `DIM SHARED`, typed procedures, `_RGBA32`, and `_LIMIT`. Converted `boing_ball.bas` to a 32-bit image with an `_RGBA32` shadow.
- **QBASIC `CONST`**: Added the `CONST name = expression` statement for declaring named numeric constants. Constants can be used in numeric expressions, array dimensioning, `FOR` loop bounds, and `DEF FN` bodies; they are resolved at runtime so they may reference variables in scope at execution time. Constants are cleared on `RUN` and `NEW`. Added test coverage.
- **QBASIC `POINT`**: Added `POINT(x, y)` to read the palette color index of a pixel and `POINT(0)`/`POINT(1)` to read the last graphics cursor X/Y coordinate. Implemented as an identifier-level function (not a keyword) to avoid conflicts with identifiers like `Point`. Returns -1 for out-of-bounds coordinates and 0 for `POINT(n)` with n outside 0/1. Added test coverage.
- **QBASIC `CSRLIN` and `POS(n)`**: Added `CSRLIN` to return the current text cursor row (1-based) and `POS(n)` to return the current text cursor column (1-based). The `n` argument is accepted but ignored per QBASIC specification. Added text cursor row (`print_row`) tracking to the interpreter and `get_text_cursor()` to the graphics subsystem. Added test coverage.
- **CONST/Point Performance**: Added an early-out guard in the named-constant lookup (`find_named_constant`) that skips string normalization when no constants are defined, keeping the 10-million-iteration BASICA benchmark at its prior ~0.59-second baseline.
- **Statement and Procedure Lookup Performance**: The run loop now advances to the next statement via the program list instead of rescanning from the first line after every statement, and expression evaluation caches each identifier's procedure lookup on its token (invalidated whenever procedures are rescanned). `boing_ball.bas` frame rendering dropped from about 88 ms to 34 ms (headless), and the `basica.bas` benchmark improved from about 1.10 s to 1.04 s on the same machine.

## 0.99.6 — 2026-10-03

- **QBASIC `SELECT CASE`**: Added the full `SELECT CASE ... END SELECT` block construct, supporting `CASE value1[, value2...]` value lists, `CASE value1 TO value2` inclusive ranges, `CASE IS comparison-op value` comparisons (`=`, `<>`, `<`, `>`, `<=`, `>=`), and a catch-all `CASE ELSE`. Multiple comma-separated items may be combined within a single `CASE` clause. Works with both numeric and string expressions, only the first matching branch executes, and it supports arbitrary nesting (including within `IF`, `FOR`, `WHILE`, and `DO` blocks) and use inside `SUB`/`FUNCTION` procedures.
- **SELECT CASE Regression Coverage**: Added tests covering single values, comma-separated value lists, `TO` ranges (including overlapping-range first-match semantics), all six `CASE IS` comparison operators, `CASE ELSE`, string `SELECT CASE`, nested `SELECT CASE`, `SELECT CASE` inside `SUB`/`FUNCTION` procedures, and `SELECT CASE` inside a loop with `EXIT DO` from within a `CASE` branch.
- **QBASIC `DO...LOOP`**: Added the full `DO...LOOP` block construct, including the infinite `DO ... LOOP` form, pre-test `DO WHILE`/`DO UNTIL` conditions, and post-test `LOOP WHILE`/`LOOP UNTIL` conditions. Supports `EXIT DO` to break out of the innermost loop, arbitrary nesting (including within `IF`, `FOR`, `WHILE`, and other `DO` blocks), and use inside `SUB`/`FUNCTION` procedures.
- **DO...LOOP Regression Coverage**: Added tests covering every loop variant, nested `DO` loops, `EXIT DO` scoping to the innermost loop, and `DO...LOOP` inside procedures.
- **Alpha Drawing Primitives**: Added optional alpha values to `PSET`, `LINE`, and `PAINT`, complementing `CIRCLE`. `PAINT` composites against existing pixels, and `GET` now reports the nearest palette color for blended pixels.
- **Custom Screen Window Sizing**: `_NEWIMAGE(width, height, colors)` now resizes the native window to match the requested canvas dimensions.
- **Graphics Showcase**: Added a high-resolution 1280x1024 demo featuring alpha-blended drawing primitives, palette swatches, a layered planet, and animation. Added graphics regression coverage for alpha blending.
- **Interpreter and Demo Performance**: Compiled eligible numeric expressions when programs load and cached procedure variable lookups. The `basica.bas` 10-million-iteration benchmark improved from about 1.17 s to 0.64-0.67 s, with the expected result unchanged. Also optimized translucent filled-circle rendering; the `boing_ball.bas` demo benefits from faster drawing and variable access.
- **Boing Ball Demo**: Improved floor-grid visibility with brighter, thicker lines; moved the ball's bounce point forward onto the floor grid; and made its alpha shadow scale and fade with its height above the floor.
- **User-Defined Types**: Expanded `TYPE ... END TYPE` with fixed-length string and array fields, arrays of records, chained indexed member access, same-type record assignment, and typed `SUB`/`FUNCTION` parameters with by-reference and scalar by-value behavior. Added declaration validation and packed little-endian random-record `GET`/`PUT` for supported fixed-layout records.
- **TYPE Regression Coverage**: Added tests for nested records, arrays, assignment, procedure scope and parameters, random-record I/O, and invalid declarations. Organized the test suite into topic-based subdirectories.
- **Performance Regression Guard**: Added an opt-out timing check during iterative debugging and an enabled pre-commit hook that runs the optimized BASICA 10-million-iteration benchmark against a per-machine baseline stored outside the repository. Skipped UDT resolution on programs without `TYPE` declarations; the BASICA demo remains around its prior 0.64-0.67-second range.
- **Audio Compatibility**: Expanded `PLAY` with foreground/background playback, articulation modes, volume control, numeric notes, and MML tempo, octave, accidental, rest, and dotted-note coverage. Faded note edges to reduce clicks. `SOUND` now validates BASICA frequency and duration ranges. Added `--no-audio` for delay-free audio tests and automation.
- **Sound Demo**: Added `demo/sound_demo.bas` to demonstrate `SOUND`, `BEEP`, `PLAY` note controls and articulation, numeric notes, rests, and background/foreground playback.
- **Boing Ball Sound**: Added short, deep-pitched, quieter background-played retro boing cues when the ball bounces off the side walls or floor.
- **Multiline IF Blocks**: Added QBASIC-style `IF...THEN`, `ELSEIF`, `ELSE`, and `END IF` blocks with nested conditionals and procedure support, while preserving single-line `IF` behavior.

## 0.99.5 — 2026-10-02

- **Error Line Reporting**: Console diagnostics for programs without explicit BASIC line numbers now report the physical source-file line instead of the internal virtual line number. `ERL` continues to return the internal line number. Added syntax-error coverage for numbered and unnumbered programs.
- **Raw Expression Parsing**: Replaced repeated operator lookahead tokenization with boundary-aware source checks in the raw expression evaluator. Expanded `DEF FN` coverage for keyword boundaries and precedence; the 2-million-call benchmark improved from 0.99 s to 0.64 s of CPU time.
- **Compiled `DEF FN` Expressions**: Numeric, side-effect-free `DEF FN` bodies now compile once to a compact AST and avoid reparsing on every call. Expressions involving arrays, strings, built-ins, or nested function calls retain the raw evaluator for compatibility.
- **Random Statistics Demo**: Added a console benchmark that sorts 100,000 integers in the range -10000 to 10000 and reports the mean, median, all modes, and compute time.
- **Array Index Fast Path**: Simple literal and scalar-variable subscripts now bypass full expression evaluation. On the random statistics workload, compute time measured about 1.38-1.43 seconds versus 1.46 seconds before the change.
- **Boing Ball Demo**: Added `_AUTODISPLAY OFF`/`_DISPLAY` frame batching, a faster tilted checker texture, and an alpha-blended shadow. `CIRCLE` now accepts an optional alpha value for filled circles.

## 2026-09-23

- **QBasic Procedures (`SUB` & `FUNCTION`)**: Implemented QBasic-style `SUB` subroutines and `FUNCTION` procedures with full parameter passing, local variable scope isolation, and recursive procedure support.
- **Pass-by-Reference & Pass-by-Value**: Simple variable arguments pass by reference into procedure local storage, while parenthesized expression arguments pass by value.
- **Procedure Control & Scoping**: Added `CALL`, `DECLARE`, `SHARED`, `STATIC`, `EXIT SUB`, and `EXIT FUNCTION`. Control flow loops (`WHILE`/`WEND`, `FOR`/`NEXT`, block `IF`/`THEN`/`ELSE`/`END IF`) execute seamlessly within procedures.
- **Demo & Testing**: Added comprehensive procedure demo (`demo/qbasic_procedures.bas`) and test cases (`qbasic_sub.bas`, `qbasic_function.bas`, `qbasic_scope.bas`) to `make test`.

## 2026-09-01

- **Dynamic Strings**: Added length-aware heap-backed string storage with automatic growth for scalar strings, string arrays, assignments, concatenation, and string functions.
- **Fixed-Length Strings**: Added `DIM name AS STRING * n` declarations for fixed-width scalar and string-array values with BASIC-compatible padding and truncation.
- **Large-String Compression**: Updated `_DEFLATE$` and `_INFLATE$` to use dynamic buffers and support multi-megabyte round trips.
- **Parser Compatibility**: Preserved FIELD, DATA/READ, command-line strings, numeric `STRING$` arguments, and string comparisons while removing fixed temporary-buffer limits from dynamic paths.
- **Performance**: Kept token storage compact to avoid slowing numeric programs.
- **Testing**: Added dynamic growth, large-string function, and compression round-trip regressions to `make test`.

## 2026-08-19

- **Image Functions**: Added `_LOADIMAGE`, `_PUTIMAGE`, and `_FREEIMAGE` using SDL3_image textures with numeric handles, alpha blending, scaling, and resource cleanup.
- **Font Demo and Rendering**: Improved `_LOADFONT` demo coverage, default-font metrics, descender padding, and nearest-neighbor glyph rendering for crisp text.
- **Graphics Demos**: Added image and font demos/assets and corrected random text placement so `LOCATE` stays within the active screen and does not scroll unexpectedly.
- **Lexer and Build Reliability**: Made keyword lookup deterministic when adding underscore-prefixed keywords and added header dependencies to the Makefile so token changes rebuild dependent objects.
- **Testing**: Added image/font test fixtures and expanded graphics validation scripts.
- **Image Demo Asset**: Replaced the weak screenshot fixture with a colorful BMP asset that makes full-image, scaled, and cropped `_PUTIMAGE` output easier to compare.
- **Parser Fixes**: Fixed `_PUTIMAGE` lookahead so a successful draw does not leave the command parser positioned at the wrong token boundary. Added Makefile header dependencies to avoid stale token enum objects.
- **Demo Corrections**: Kept `randomtext.bas` within the active `SCREEN 12` text area and restored bright text before its completion message.
- **String Buffers**: Increased practical parser string capacity from 255 to 511 characters and added long-string compression coverage. This was superseded by the heap-backed dynamic string implementation above.

## 2026-07-06

- **Optional Line Numbers and Labels**: Line numbers are now optional. When omitted, statements receive internal line numbers in increments of 10. Programs may use named labels (`my_label:`) as jump targets for `GOTO`, `GOSUB`, `ON...GOTO/GOSUB`, `IF...THEN`, and `ON ERROR GOTO`.
- **WINDOW and VIEW**: Implemented `WINDOW` for custom logical coordinate mapping and `VIEW` for physical viewport clipping. All drawing commands (`PSET`, `LINE`, `CIRCLE`, `PAINT`, `DRAW`) now accept floating-point logical coordinates transformed through the active window/viewport. Supports `WINDOW SCREEN` (Y-down) and `VIEW SCREEN` (absolute screen coordinates) variants.

## 2026-06-17

- **Headless Graphics Mode**: Add `--headless` command-line option to run graphics and screenshots headlessly (uses standard POSIX dummy video driver and hidden window). (commit ca8d271)
- **Performance Improvements**: Refactored interpreter engine and variable lookups to implement a 2x performance increase. (commit c874659)
- **Graphics Primitive & Pixel Buffer Testing**: Added test coverage validating graphics primitives (`PSET`, `LINE`, `CIRCLE`), virtual screen buffer operations (`GET`/`PUT` to arrays), and automatic `SCREENSHOT` generation.

## 2026-06-16

- **Performance Tuning**: Additional performance tuning and optimization to double speed of code execution. (commits 44c681d, f0e7fff)

## 2026-06-12

- **Type Suffixes**: Support type suffixes for variables: `%` (integer), `!` (single precision), and `#` (double precision). (commit 6e0fd77)
- **Default Type Declarations**: Implement `DEFINT`, `DEFSTR`, `DEFSNG`, and `DEFDBL` for default type declarations based on starting letters. (commit ef46add)
- **Bitwise Operations**: Implement bitwise operations for `AND`, `OR`, and `XOR` when used with numeric expressions (converts to 16-bit integers). (commit 9672bd5)
- **Exit on Finish**: Add `-x` / `--exit-on-finish` command-line option to exit immediately after program finishes in graphics window mode. (commit 81f898a)
- **Code Quality**: Run `cppcheck` and fix static analysis warnings. (commit d2e424e)

## 2026-06-11

- **Graphics Screenshots**: Implement `SCREENSHOT filename$` command. (commit 3a1886c)
- **Graphics Image Blocks**: Add graphics image block support for `GET` and `PUT` (distinct from file `GET`/`PUT` operations). (commit 625085b)
- **Command Line Arguments**: Pass command-line arguments using QBASIC style `COMMAND$`, and implement `ARGV$(index)` and `ARGC`. (commit eff784f)
- **Shebang & Versioning**: Support executable `.bas` scripts by ignoring shebang headers (`#!`), add support for `make install`/`uninstall`, and add `-v`/`--version` flags. (commit c185678)
- **Formatting Rules**: Update test cases for `PRINT` number formatting rules. (commit eff784f)

## 2026-06-10

- **Standard BASIC Error Handling**: Completed implementation of standard BASIC error codes, tracking variables `ERR` and `ERL`, and updated documentation/tests. (commit 93a22a5)
- **Files Formatting**: Add `FILES` formatting closer to DOS/BASICA output. (commit a8aaf5e)
- **Directory Operations**: Add `CHDIR`, `MKDIR`, and `RMDIR` tests for string variables, existing paths, and non-empty directory removal. (commit ec5ea35)
- **Environment**: Implement `ENVIRON` function. (commit c07b4d1)
- **Arrays**: Implement `OPTION BASE` support. (commit b0d381b)
- **Graphics**: Add `DRAW` command support. (commit 5a6a64e)
- **Documentation**: General documentation updates. (commit 961c096)

## 2026-06-09

- **Font Rendering**: Improve font rendering for graphics mode. (commit b865b66)
- **File Management**: Implement `NAME old AS new` function. (commit 5a4b49c)
- **Graphics**: Implement `PAINT` and improved error messaging. (commit 29adf79)
- **Interpreter**: Initial commit of the core interpreter. (commit 977191b)
