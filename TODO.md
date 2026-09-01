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

## Lexer and interpreter performance


## Graphics

- ~~Implement `WINDOW` and `VIEW` for custom coordinate mapping and viewports.~~ ✅ Done
- ~~Implement `_LOADIMAGE`, `_PUTIMAGE`, and `_FREEIMAGE` with SDL image handles.~~ ✅ Done

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
