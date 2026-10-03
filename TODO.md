# Basika TODO

## Short-term improvements

- Add `TRON` and `TROFF` for program tracing and debugging.
- Expand `PLAY` MML coverage beyond the initial `T`, `O`, `L`, notes, rests, dotted notes, and octave changes.

## Audio

- Add more complete `PLAY` MML support: `MB`, `MF`, `MN`, `ML`, `MS`, `Nn`, and foreground/background behavior.
- Add tests for `PLAY` octave changes, sharps/flats, dotted notes, rests, and tempo changes.
- Add `SOUND` range validation matching BASICA more closely.
- Consider a quiet/test audio mode flag so audio commands can be tested without delays.

## Interpreter correctness

- Add support for `CONT`, `STOP`, and better direct-mode behavior.
- Implement `WAIT` for port monitoring (or a simulated equivalent).

### User-defined `TYPE`s (QBASIC compatibility)

- **Implemented:** Nested `TYPE` declarations, scalar and array UDT instances, nested and primitive array fields, explicit field-array bounds, fixed-length string fields, chained member access, same-type whole-record and array-element assignment, and typed procedure parameters (by-reference, nested calls, scalar by-value copies, and isolated procedure-local instances).
- **Validation:** Duplicate types/fields, unknown field types, malformed declarations, missing `END TYPE`, recursive by-value layouts, and incompatible record assignment are rejected with source-line diagnostics. Regression coverage is in the `tests/user_types*.bas` cases.
- **Random records:** `GET #file, record, udtVariable` and `PUT #file, record, udtVariable` use packed little-endian fields and support nested UDTs, numeric fields/arrays, and fixed-length string fields. Dynamic string fields have no fixed QBASIC record layout and are rejected. `tests/user_types_record_io.bas` verifies round-trip values. Other QBASIC binary-layout edge cases remain unverified.

## Lexer and interpreter performance

### Memory and strings

- Profile large string assignments and concatenations, then avoid unnecessary copies through direct append or safe ownership transfer where BASICA value semantics permit it.

### File and graphics I/O

- Profile file-heavy workloads; if `LOF`/`LOC` or frequent random-record access is hot, cache file-position/size metadata where it can be kept correct.
- Profile graphics-heavy workloads for redundant state changes or presents; retain canvas batching and only add command batching when measurements show a benefit.

### Parsing and evaluation


## Graphics

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
