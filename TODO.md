# Basika TODO

## Short-term improvements

- Add `TRON` and `TROFF` for program tracing and debugging.

## Unimplemented QBASIC language features

- `LINE INPUT [#n,] var$`: Reads an entire line of input (including commas) into a string variable.
- `INPUT$(n[, #file])`: Reads `n` characters from the keyboard or a file without echoing/delimiters.
- `WRITE [#n,] expr1[, expr2...]`: Writes comma-delimited, quoted output to the screen or a file.
- `LOCK #n [, record | record1 TO record2]` / `UNLOCK #n [...]`: File/record locking for shared access.
- `PALETTE` / `PALETTE USING`: Remaps screen color palette entries.
- ~~`POINT(x,y)` / `POINT(n)`: Returns the color of a pixel or the last graphics cursor coordinate.~~
- ~~`CSRLIN` / `POS(n)`: Returns the current cursor row/column.~~
- `PEN(n)` / `ON PEN GOSUB`: Light pen input and trapping.
- `STICK(n)`: Joystick position (note: `STRIG` is already supported for joystick/trigger buttons).
- `WIDTH [#n,] columns[, rows]`: Sets screen or file text width.
- `PCOPY page1, page2`: Copies one graphics page to another.
- `CHAIN "program"` / `COMMON var1[, var2...]`: Chains to another program, optionally passing shared variables.
- `CLEAR [, memory]`: Resets all variables and closes files.
- `OUT port, value` / `INP(port)`: Hardware I/O port access.
- `DEF SEG` / `VARSEG` / `VARPTR$` / `FRE` / `BLOAD` / `BSAVE`: Legacy memory-segment operations (low priority).
- `ERDEV` / `ERDEV$`: Device-error code and name from the last device I/O error.
- IDE-only commands, if a REPL/editing mode is ever desired: `AUTO`, `RENUM`, `EDIT`, `SAVE`, `LOAD`, `MERGE`.

## QB64 extensions

- Off-screen images: `_NEWIMAGE` as a function returning an image handle, `_DEST`/`_SOURCE`, `_PUTIMAGE` between images, and `_COPYIMAGE`, so animations can draw static layers once instead of every frame.
- `_TITLE`, `_KEYDOWN`/`_KEYHIT`, and `_MOUSEX`/`_MOUSEY`/`_MOUSEBUTTON` for interactive demos.
- `_INTEGER64`/`_UNSIGNED INTEGER` types and `DEFLNG`.

## Interpreter correctness

- `STATIC` procedures and `STATIC` variables are parsed but locals are not preserved between calls.
- Arrays of records `DIM`med inside a procedure are still module-level (numeric and string arrays are local).
- Each field of each `TYPE` array element uses one of the 8192 variable-table entries.
- Add support for `CONT`, `STOP`, and better direct-mode behavior.
- Implement `WAIT` for port monitoring (or a simulated equivalent).
- Verify remaining QBASIC random-record binary-layout edge cases.

## Lexer and interpreter performance

### Memory and strings

- Profile large string assignments and concatenations, then avoid unnecessary copies through direct append or safe ownership transfer where BASICA value semantics permit it.

### File and graphics I/O

- Profile file-heavy workloads; if `LOF`/`LOC` or frequent random-record access is hot, cache file-position/size metadata where it can be kept correct.
- Profile graphics-heavy workloads for redundant state changes or presents; retain canvas batching and only add command batching when measurements show a benefit. `PAINT` reads back and re-uploads the whole screen (or the active `VIEW`) on every call, which may be slow with GPU renderers; `demo/graphics_primitives_showcase.bas` runs at about 30 fps headless.

## Testing and project hygiene

- Add `make check-clean` or test cleanup verification.
- Add sanitizer builds: `make asan`, `make ubsan`.
- Add a developer note documenting how to add a new BASIC keyword.
- Add broader automated coverage for image rendering and font screenshots.
- Add automated visible-window coverage for graphics demos where CI supports a display server.

## UX and documentation

- Implement `ON STRIG(n)` trapping and add a demo/test for it.
- Add a command-line option to dump supported commands.
- Add a command support matrix: supported, partial, planned.
- Add examples for file I/O, graphics, sound, arrays, and error handling.
