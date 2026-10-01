# CHANGELOG

All notable changes to this project are recorded in this file.

## 2026-10-01

- **Error Line Reporting**: Console diagnostics for programs without explicit BASIC line numbers now report the physical source-file line instead of the internal virtual line number. `ERL` continues to return the internal line number. Added syntax-error coverage for numbered and unnumbered programs.
- **Raw Expression Parsing**: Replaced repeated operator lookahead tokenization with boundary-aware source checks in the raw expression evaluator. Expanded `DEF FN` coverage for keyword boundaries and precedence; the 2-million-call benchmark improved from 0.99 s to 0.64 s of CPU time.
- **Random Statistics Demo**: Added a console benchmark that sorts 100,000 integers in the range -10000 to 10000 and reports the mean, median, all modes, and compute time.
- **Array Index Fast Path**: Simple literal and scalar-variable subscripts now bypass full expression evaluation. On the random statistics workload, compute time measured about 1.38-1.43 seconds versus 1.46 seconds before the change.

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
