# Basika TODO

## Short-term improvements

- Add `TRON` and `TROFF` for program tracing and debugging.

## Unimplemented QBASIC language features

- `OUT port, value` / `INP(port)`: Hardware I/O port access (`WAIT` simulates the VGA status port only).
- `DEF SEG` / `VARSEG` / `VARPTR$` / `FRE` / `BLOAD` / `BSAVE`: Legacy memory-segment operations (low priority).
- `WIDTH #n` file widths are accepted but output is not wrapped.
- `CHAIN MERGE`, `CHAIN ... ALL` and `CHAIN ..., line` (GW-BASIC forms) are not supported.

## QB64 extensions

- Off-screen images: `_NEWIMAGE` as a function returning an image handle, `_DEST`/`_SOURCE`, `_PUTIMAGE` between images, and `_COPYIMAGE`, so animations can draw static layers once instead of every frame.
- `_TITLE`, `_KEYDOWN`/`_KEYHIT`, and `_MOUSEX`/`_MOUSEY`/`_MOUSEBUTTON` for interactive demos.
- `_INTEGER64`/`_UNSIGNED INTEGER` types and `DEFLNG`.

## Interpreter correctness

- Each field of each `TYPE` array element uses one of the 8192 variable-table entries; storing records as contiguous blocks would remove that limit and speed up record I/O.

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
