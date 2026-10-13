'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const vscode = require('vscode');

const TERMINAL_NAME = 'Basika';
const EXE_NAME = process.platform === 'win32' ? 'basika.exe' : 'basika';

// Statements that only make sense with a graphics window. LINE is left out
// because LINE INPUT is a text statement.
const GRAPHICS_PATTERN =
    /\b(SCREEN|PSET|PRESET|CIRCLE|PAINT|DRAW|PALETTE|PCOPY|VIEW|WINDOW|_NEWIMAGE|_LOADIMAGE|_PUTIMAGE|_DISPLAY|_FULLSCREEN|_MOUSEINPUT)\b/i;

let terminal;

function activate(context) {
    const register = (id, mode) =>
        context.subscriptions.push(vscode.commands.registerCommand(id, (uri) => run(uri, mode)));

    register('basika.run', undefined);
    register('basika.runInWindow', 'window');
    register('basika.runInTerminal', 'terminal');
    register('basika.runHeadless', 'headless');

    context.subscriptions.push(
        vscode.commands.registerCommand('basika.stop', stop),
        vscode.commands.registerCommand('basika.showExecutable', showExecutable),
        vscode.window.onDidCloseTerminal((closed) => {
            if (closed === terminal) {
                terminal = undefined;
            }
        })
    );
}

function deactivate() {
    stop();
}

async function run(uri, mode) {
    const document = await programDocument(uri);
    if (!document) {
        vscode.window.showErrorMessage('Basika: open a .bas file to run it.');
        return;
    }

    const config = vscode.workspace.getConfiguration('basika', document.uri);
    if (config.get('saveBeforeRun') && document.isDirty && !(await document.save())) {
        return;
    }

    const program = document.uri.fsPath;
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    const exe = findExecutable(config, program, folder);
    if (!exe) {
        await reportMissingExecutable(config);
        return;
    }

    if (!mode) {
        mode = config.get('mode');
        if (mode === 'auto') {
            mode = usesGraphics(document.getText()) ? 'window' : 'terminal';
        }
    }

    const args = [];
    if (mode === 'window') {
        args.push('-w');
        if (config.get('exitOnFinish')) {
            args.push('-x');
        }
    } else if (mode === 'headless') {
        args.push('--headless');
    }
    if (config.get('quiet')) {
        args.push('-q');
    }
    if (config.get('noAudio')) {
        args.push('--no-audio');
    }
    args.push(...config.get('extraArgs'), program, ...config.get('programArgs'));

    const cwd =
        config.get('workingDirectory') === 'file' || !folder
            ? path.dirname(program)
            : folder.uri.fsPath;

    // A fresh terminal per run ends the previous program, which would
    // otherwise read the new command line as its own input.
    stop();
    terminal = vscode.window.createTerminal({ name: TERMINAL_NAME, cwd });
    terminal.show(true);
    terminal.sendText(commandLine(exe, args));
}

function stop() {
    if (terminal) {
        terminal.dispose();
        terminal = undefined;
    }
}

// The document for an explorer/context-menu URI, else the active editor's.
async function programDocument(uri) {
    if (uri instanceof vscode.Uri) {
        return vscode.workspace.openTextDocument(uri);
    }
    const editor = vscode.window.activeTextEditor;
    if (editor && /\.bas$/i.test(editor.document.fileName)) {
        return editor.document;
    }
    return undefined;
}

function usesGraphics(source) {
    for (let line of source.split(/\r?\n/)) {
        line = line.replace(/"[^"]*("|$)/g, '""');
        const comment = line.indexOf("'");
        if (comment >= 0) {
            line = line.slice(0, comment);
        }
        line = line.replace(/(^\s*(\d+\s*)?|:\s*)REM\b.*$/i, '');
        if (GRAPHICS_PATTERN.test(line)) {
            return true;
        }
    }
    return false;
}

function findExecutable(config, program, folder) {
    const explicit = config.get('executablePath').trim();
    if (explicit) {
        let resolved = explicit
            .replace(/\$\{workspaceFolder\}/g, folder ? folder.uri.fsPath : '')
            .replace(/^~(?=$|[\\/])/, os.homedir());
        if (!path.isAbsolute(resolved)) {
            resolved = path.resolve(folder ? folder.uri.fsPath : path.dirname(program), resolved);
        }
        return isExecutable(resolved) ? resolved : undefined;
    }

    const source = config.get('executableSource');
    if (source !== 'path') {
        const local = findInProject(program, folder);
        if (local || source === 'workspace') {
            return local;
        }
    }
    return findOnPath();
}

// Looks for basika next to the program, then in each parent folder up to
// the workspace folder (or only next to the program outside a workspace).
function findInProject(program, folder) {
    const root = folder ? folder.uri.fsPath : path.dirname(program);
    let dir = path.dirname(program);
    for (;;) {
        const candidate = path.join(dir, EXE_NAME);
        if (isExecutable(candidate)) {
            return candidate;
        }
        const parent = path.dirname(dir);
        if (dir === root || parent === dir || !isInside(parent, root)) {
            return undefined;
        }
        dir = parent;
    }
}

function findOnPath() {
    const dirs = (process.env.PATH || '').split(path.delimiter).filter(Boolean);
    // VS Code started from the macOS Dock may not see Homebrew's PATH.
    if (process.platform === 'darwin') {
        dirs.push('/opt/homebrew/bin', '/usr/local/bin');
    }
    for (const dir of dirs) {
        const candidate = path.join(dir, EXE_NAME);
        if (isExecutable(candidate)) {
            return candidate;
        }
    }
    return undefined;
}

function isInside(dir, root) {
    const relative = path.relative(root, dir);
    return !relative.startsWith('..') && !path.isAbsolute(relative);
}

function isExecutable(file) {
    try {
        if (!fs.statSync(file).isFile()) {
            return false;
        }
        fs.accessSync(file, process.platform === 'win32' ? fs.constants.F_OK : fs.constants.X_OK);
        return true;
    } catch {
        return false;
    }
}

async function reportMissingExecutable(config) {
    const where = config.get('executablePath').trim()
        ? `at "${config.get('executablePath')}"`
        : {
              auto: 'in the project folder or on PATH',
              workspace: 'in the project folder',
              path: 'on PATH',
          }[config.get('executableSource')];
    const choice = await vscode.window.showErrorMessage(
        `Basika: could not find the basika executable ${where}. Build it with make, install it with Homebrew, or set basika.executablePath.`,
        'Open Settings'
    );
    if (choice === 'Open Settings') {
        vscode.commands.executeCommand('workbench.action.openSettings', 'basika.executable');
    }
}

async function showExecutable() {
    const editor = vscode.window.activeTextEditor;
    const uri = editor ? editor.document.uri : undefined;
    const folder = uri
        ? vscode.workspace.getWorkspaceFolder(uri)
        : (vscode.workspace.workspaceFolders || [])[0];
    const config = vscode.workspace.getConfiguration('basika', uri);
    const program = uri && uri.scheme === 'file'
        ? uri.fsPath
        : path.join(folder ? folder.uri.fsPath : os.homedir(), 'program.bas');
    const exe = findExecutable(config, program, folder);
    if (exe) {
        vscode.window.showInformationMessage(`Basika: using ${exe}`);
    } else {
        await reportMissingExecutable(config);
    }
}

// Quotes the command for the default shell the Basika terminal will start.
function commandLine(exe, args) {
    const shell = path.basename(vscode.env.shell || '').toLowerCase();
    if (process.platform === 'win32' && /^(pwsh|powershell)/.test(shell)) {
        const quote = (s) => `'${s.replace(/'/g, "''")}'`;
        return `& ${[exe, ...args].map(quote).join(' ')}`;
    }
    if (process.platform === 'win32' && (shell === '' || shell.startsWith('cmd'))) {
        const quote = (s) => (/[\s"&|<>^]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s);
        return [exe, ...args].map(quote).join(' ');
    }
    const quote = (s) => (/^[\w@%+=:,./-]+$/.test(s) ? s : `'${s.replace(/'/g, `'\\''`)}'`);
    return [exe, ...args].map(quote).join(' ');
}

module.exports = { activate, deactivate };
