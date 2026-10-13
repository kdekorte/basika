# Basika Runner for VS Code

Runs `.bas` programs with the [basika](https://github.com/kdekorte/basika)
interpreter from the editor.

## Running a program

Open a `.bas` file and do one of:

- press **Ctrl+F5**,
- click the **Run** button in the editor title bar (its drop-down also has
  *Run in a Graphics Window* and *Run in the Terminal*),
- right-click the file in the editor or the Explorer and choose
  **Run with Basika**,
- run **Basika: Run with Basika** from the Command Palette.

The program runs in a terminal named *Basika*, so `INPUT`, `INKEY$` and
program output work as they do from a shell. Running again, or
**Basika: Stop Running Program**, ends the previous run first.

By default the program is saved first, and a graphics window (`-w`) is opened
when the source uses graphics statements such as `SCREEN`, `PSET`, `CIRCLE` or
`_NEWIMAGE`; text-only programs run in the terminal.

## Which basika is used

With the default `basika.executableSource` of `auto`, the extension looks for
a `basika` executable next to the program and then in each parent folder up to
the workspace folder, so a freshly built `./basika` in a checkout of the
interpreter is picked up. If there is none it uses `basika` from `PATH` (on
macOS it also checks the Homebrew folders `/opt/homebrew/bin` and
`/usr/local/bin`). Set it to `workspace` or `path` to use only one of them, or
set `basika.executablePath` to an explicit path. **Basika: Show Which basika
Executable Is Used** reports the choice.

## Settings

| Setting | Default | Description |
| --- | --- | --- |
| `basika.executableSource` | `auto` | `auto`, `workspace` or `path`. |
| `basika.executablePath` | | Explicit executable; relative paths and `${workspaceFolder}` resolve against the workspace folder. |
| `basika.mode` | `auto` | `auto`, `window` (always `-w`) or `terminal` (never `-w`). |
| `basika.workingDirectory` | `workspace` | Run in the `workspace` folder or the program's `file` folder. |
| `basika.quiet` | `true` | Pass `-q`. |
| `basika.noAudio` | `false` | Pass `--no-audio`. |
| `basika.exitOnFinish` | `false` | Pass `-x` with `-w` so the window closes when the program ends. |
| `basika.extraArgs` | `[]` | Extra basika options before the program path. |
| `basika.programArgs` | `[]` | Arguments after the program path, for `COMMAND$`, `ARGC` and `ARGV$`. |
| `basika.saveBeforeRun` | `true` | Save the file before running. |

## Building and installing

The extension has no dependencies or build step. To package and install it:

```sh
cd editors/vscode
npx @vscode/vsce package
code --install-extension basika-runner-0.1.0.vsix
```

To try it without installing, open `editors/vscode` in VS Code and press F5
to start an Extension Development Host.
