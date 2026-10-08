# Basika

A small IBM BASICA-compatible interpreter clone written in C.

## Install with Homebrew (macOS)

The easiest way to install basika on macOS is with [Homebrew](https://brew.sh):

```sh
brew tap kdekorte/basika https://github.com/kdekorte/basika.git
brew install basika
```

This will automatically install all required SDL3 dependencies.

To upgrade to a newer version:

```sh
brew update
brew upgrade basika
```

To uninstall:

```sh
brew uninstall basika
brew untap kdekorte/basika
```

## Build from Source

Requirements:

- `gcc`
- `pkg-config`
- `SDL3`, `SDL3_ttf`, `SDL3_mixer`, and `SDL3_image` development headers

Build with:

```sh
make
```

## License

Basika is distributed under the [MIT License](LICENSE).

## Run

Run a BASIC program:

```sh
./basika demo/hello.bas
```

Enable graphics window mode:

```sh
./basika -w demo/hello.bas
```

Suppress startup output and REPL prompts:

```sh
./basika -q demo/hello.bas
```

Exit immediately after finish in graphics mode (for automation/screenshots):

```sh
./basika -w -x demo/hello.bas
```

Run in headless graphics mode (virtual framebuffer without showing a window, e.g. for CI/CD tests):

```sh
./basika --headless demo/hello.bas
```

Run programs without producing sound or waiting for audio commands:

```sh
./basika --no-audio demo/hello.bas
```

Graphics demos can be run with a visible window using `-w`:

```sh
./basika -w demo/image_demo.bas
./basika -w demo/printstring.bas
./basika -w demo/randomtext.bas
./basika -w demo/boing_ball.bas
```

`demo/qb64_paint.bas` is a small MacPaint-style program built on the QB64 image, mouse and keyboard extensions (pencil, brush, spray, shapes, fill bucket, opacity, undo, mirror and flip). Its window can be resized: the layout and drawing area follow the new size, and scroll bars appear when the window is smaller than the picture:

```sh
./basika -w demo/qb64_paint.bas
```

Try the sound and `PLAY` Music Macro Language demo:

```sh
./basika demo/sound_demo.bas
```

Image demos require `SDL3_image` and load the repository fixture from  
`tests/image_fixture.bmp`. Use `--headless` when a windowing environment is not  
available.

Show help:

```sh
./basika -h
```

## Test

Run the repository test suite:

```sh
make test
```

The performance regression guard runs the 10-million-iteration
`demo/basica.bas` benchmark and compares its median over five runs against a
machine-local, benchmark-specific baseline kept outside the repository (under
`${XDG_CACHE_HOME:-$HOME/.cache}/basika/performance`). The first run on a
machine initializes that baseline and passes. During iterative debugging, run
`BASIKA_SKIP_PERFORMANCE=1 make test` to skip only the timing check. The
repository pre-commit hook always performs a clean optimized build and runs the
guard, regardless of that skip setting. Enable the hook once per clone with:

```sh
git config core.hooksPath .githooks
```

## Supported features

Basika supports a wide range of IBM BASICA-compatible commands and modern QBasic-style modular programming.  
Full support is included for `SUB` and `FUNCTION` procedure blocks, `CALL`, `DECLARE`, `SHARED` global variables, `STATIC` declarations, and `EXIT SUB`/`EXIT FUNCTION`. Procedures feature isolated local variable scope, pass-by-reference for simple variable parameters, pass-by-value for parenthesized expressions, and recursive function evaluation.

Graphics image handles are managed with `_LOADIMAGE`, `_PUTIMAGE`, and  
`_FREEIMAGE`; loaded fonts use `_LOADFONT`, `_FONT`, and `_FREEFONT`.

String variables use heap-backed storage and grow automatically as values are  
assigned, concatenated, or returned from string functions. Multi-megabyte  
values are supported by default. Fixed-length declarations are also supported  
with `DIM name AS STRING * n` and `DIM name(size) AS STRING * n`.

`_DEFLATE$` and `_INFLATE$` support lossless compression and decompression of  
dynamic strings, including multi-megabyte round trips. Coverage for procedures, dynamic  
growth, string functions, fixed-length declarations, and compression is  
included in `make test`.

For a full list of supported commands and their exact syntax, please refer to KEYWORDS.md.

## Future reminders

See `TODO.md` for planned improvements and future work items.

## Documentation

- `KEYWORDS.md`: complete lexer keyword index and syntax reference.
- `ERROR_CODES.md`: runtime error codes and messages.
- `CHANGELOG.md`: chronological feature and maintenance history.
