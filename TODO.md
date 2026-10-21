# Basika TODO

## Short-term improvements

- Add `TRON` and `TROFF` for program tracing and debugging.

## Unimplemented QBASIC language features

- `OUT port, value` / `INP(port)`: Hardware I/O port access (`WAIT` simulates the VGA status port only).
- `DEF SEG` / `VARSEG` / `VARPTR$` / `FRE` / `BLOAD` / `BSAVE`: Legacy memory-segment operations (low priority).
- `WIDTH #n` file widths are accepted but output is not wrapped.
- `VAL` does not read `&H`, `&O` or `&B` prefixes: `VAL("&H1F")` is 0 where QBasic gives 31
  (`&H1F` in a program works). QB64's `_BIN$` is meant to round-trip through `VAL("&B" + s$)`.
- `PRINT a$ = b$` (a string comparison in a `PRINT` list) is "Syntax error"; QBasic prints -1 or
  0. Comparisons work in `IF` and in assignments.
- `CHAIN MERGE`, `CHAIN ... ALL` and `CHAIN ..., line` (GW-BASIC forms) are not supported.

## QB64 extensions

Basika implements 121 of the 311 QB64 Phoenix Edition keywords (checked against the
[alphabetical keyword reference](https://qb64phoenix.com/qb64wiki/index.php/Keyword_Reference_-_Alphabetical)
on 2026-10-16), not counting the OpenGL `_gl...` set. An unknown `_NAME` is not an error:
it reads as an ordinary variable, 0 or "", so `IF _UPTIME > 0 THEN` is quietly false.
Making unknown `_` names "Syntax error" would show QB64 programs what is missing.

### Most useful first

These come up in ordinary QB64 programs and are small to add.

- Control flow: `_CONTINUE` (next loop iteration), `_IIF(condition, a, b)`, and the
  short-circuit `_ANDALSO` / `_ORELSE`.
- System: `_CLIPBOARDIMAGE` (the other system keywords are done).
- Metacommands: `$CONSOLE` is ignored (the terminal is the console). `$DYNAMIC`, `$STATIC`,
  `$EXEICON` and `$VERSIONINFO` can stay ignored too.

### Graphics

- Transparency and blending: `_BLEND` / `_DONTBLEND`.
- Images: `_SCREENIMAGE`, `_COPYPALETTE`, `_DISPLAY` as a function (the screen's image handle),
  `_CLIP` (`_PUTIMAGE` partly off the edge),
  `_MAPTRIANGLE` (textured triangles), `_DISPLAYORDER`, `_DEPTHBUFFER`.
- Fonts and text: `_BLINK` does not make colors 16-31 blink (it only keeps its setting).
- The window: `_WINDOWHANDLE`, `_WINDOWSIZELIMIT`,
  `_ALLOWFULLSCREEN`, `_ICON`, `_SCREENCLICK`, `_SCREENPRINT`, `_PRINTIMAGE`.
- Dropped files: `_ACCEPTFILEDROP`, `_TOTALDROPPEDFILES`, `_DROPPEDFILE`, `_FINISHDROP`.

### Mouse, keyboard and game controllers

- Devices (QB64's general controller interface; `STICK` and `STRIG` work today):
  `_DEVICES`, `_DEVICE$`, `_DEVICEINPUT`, `_LASTBUTTON`, `_LASTAXIS`, `_LASTWHEEL`,
  `_BUTTON`, `_BUTTONCHANGE`, `_AXIS`, `_WHEEL`.

### Sound

- `_SNDBAL`, `_SNDSETPOS`, `_SNDCOPY`, `_SNDLIMIT`, `_SNDRATE`, `_SNDNEW`, `_SNDOPENRAW`,
  the raw sample queue (`_SNDRAW`, `_SNDRAWLEN`, `_SNDRAWDONE`, `_SNDRAWBATCH`), `_WAVE`,
  `_MEMSOUND`, `_MIDISOUNDBANK`.

### Data, encoding and memory

- Checksums and encoding: `_CRC32`, `_ADLER32`, `_MD5$`, `_BASE64ENCODE$`, `_BASE64DECODE$`,
  `_ENCODEURL$`, `_DECODEURL$`.
- Binary conversion: `_MK$` and `_CV` (any numeric type, like `MKI$`/`CVI`).
- Arrays and types: `_PRESERVE` (`REDIM _PRESERVE`), `_ARRAYCOPY`, `_DEFINE`, `_CAST`,
  `_NEGATE`, `_BIT` (the bit type), `_OFFSET`, `_STATIC` / `_DYNAMIC` qualifiers.
- The `_MEM` block interface: `_MEM`, `_MEMNEW`, `_MEMFREE`, `_MEMGET`, `_MEMPUT`,
  `_MEMCOPY`, `_MEMFILL`, `_MEMEXISTS`, `_MEMELEMENT`, `_MEMIMAGE`.

### Larger features

- Dialogs: `_MESSAGEBOX`, `_INPUTBOX$`, `_OPENFILEDIALOG$`, `_SAVEFILEDIALOG$`,
  `_SELECTFOLDERDIALOG$`, `_COLORCHOOSERDIALOG`, `_NOTIFYPOPUP`.
- Networking (TCP/IP): `_OPENHOST`, `_OPENCLIENT`, `_OPENCONNECTION`, `_CONNECTED`,
  `_CONNECTIONADDRESS$`, `_STATUSCODE`.
- Console windows: `_CONSOLE`, `_CONSOLETITLE`, `_CONSOLECURSOR`, `_CONSOLEFONT`,
  `_CONSOLEINPUT`, `_CINP`, `_ECHO`, `_DEST _CONSOLE`.
- Logging and checks: `_LOGTRACE`, `_LOGINFO`, `_LOGWARN`, `_LOGERROR`, `_LOGMINLEVEL`,
  `_ASSERT` with `$ASSERTS`, `$CHECKING`, `$DEBUG`, `$ERROR`, `$ERRORLOCATION`,
  `_INCLERRORFILE$`, `_INCLERRORLINE`.
- Embedding and build information: `$EMBED`, `_EMBEDDED$`, `_COMPILEDATE$`,
  `_COMPILETIME$`, `_COMPILERVERSION$`, `$USELIBRARY`, `$UNSTABLE`, `$COLOR`, `$FORMAT`.

### Probably out of scope

- OpenGL: `_GL`, `_GLRENDER` and the `_gl...` functions (about 300), which need a QB64-style
  OpenGL context.
- `_RETAIN`, `_MIDDLE` and other options of features above that basika does not have.

## Interpreter correctness

- Each field of each `TYPE` array element uses one of the 8192 variable-table entries; storing records as contiguous blocks would remove that limit and speed up record I/O.

## Lexer and interpreter performance

### File and graphics I/O

- Profile file-heavy workloads; if `LOF`/`LOC` or frequent random-record access is hot, cache file-position/size metadata where it can be kept correct.
- `_PRINTSTRING` and `_UPRINTSTRING` render their text and create, draw and destroy a texture on every call, which flushes the queued drawing (about 16% of `demo/graphics_primitives_showcase.bas` in a window, whose HUD redraws every frame). `PRINT` avoids this with its per-glyph texture cache; cache whole-string textures by font, color and text the same way. The showcase runs at about 120 fps headless (60 with its `_LIMIT 60`) and at the display rate in a window.

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
