#!/usr/bin/env python3
"""Builds a BASIC program into a double-clickable macOS application.

    python3 tools/make_mac_app.py demo/burrow/burrow.bas --name Burrow

makes dist/Burrow.app, which needs nothing else installed to run:

    Contents/MacOS/Burrow          a launcher script: runs the program with -w
    Contents/MacOS/basika          basika, compiled without -march=native so it
                                   runs on any Mac with the same architecture
    Contents/Frameworks/*.dylib    SDL3 and every library it pulls in, repointed
                                   at this folder with install_name_tool
    Contents/Resources/fonts/      basika's built-in font
    Contents/Resources/demo/...    the program and, when it is the only .bas in
                                   its folder, the rest of that folder (assets,
                                   sounds) except tools/ and files git ignores
    Contents/Resources/AppIcon.icns
                                   from --icon, or assets/icon.png next to the
                                   program, a 1024 x 1024 PNG

The program finds its files next to itself as usual (ARGV$(0)). The app is
read-only, so the launcher sets BASIKA_DATA_DIR to
~/Library/Application Support/<name>/ for files the program saves, such as a
high score, and appends the program's output to ~/Library/Logs/<name>.log.

The app is signed ad hoc, so it runs on this Mac. Copied to another Mac it
is quarantined: open it once with right-click > Open (or run
xattr -dr com.apple.quarantine on it). It runs on Macs with this Mac's
architecture and at least the macOS version the Homebrew libraries were built
for, which Info.plist records.

Needs the Xcode command line tools, pkg-config and Homebrew's sdl3, sdl3_ttf,
sdl3_mixer and sdl3_image, as for building basika itself.
"""

import argparse
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMEWORKS_REF = "@executable_path/../Frameworks/"
PACKAGES = ["sdl3", "sdl3-ttf", "sdl3-mixer", "sdl3-image"]


def run(*cmd, capture=True):
    result = subprocess.run(cmd, check=True, text=True, capture_output=capture)
    return result.stdout if capture else None


def fail(message):
    sys.exit("make_mac_app: " + message)


def version():
    with open(os.path.join(ROOT, "src", "common.h")) as f:
        m = re.search(r'BASIKA_VERSION\s+"([^"]+)"', f.read())
    return m.group(1) if m else "1.0"


# ---------------------------------------------------------------- basika

def build_basika(work):
    """Compiles basika into work/ for any Mac of this architecture."""
    sources = [os.path.join(ROOT, "src", name) for name in
               ("main.c", "lexer.c", "interpreter.c", "program.c", "graphics.c", "audio.c")]
    cflags = run("pkg-config", "--cflags", *PACKAGES).split()
    libs = run("pkg-config", "--libs", *PACKAGES).split()
    out = os.path.join(work, "basika")
    print("Compiling basika...")
    run("cc", "-O3", "-I" + os.path.join(ROOT, "src"), *cflags, *sources, "-o", out, *libs, "-lz", "-lm")
    return out


# ---------------------------------------------------------------- libraries

def is_system(ref):
    return ref.startswith("/usr/lib/") or ref.startswith("/System/")


def load_commands(path):
    return run("otool", "-l", path)


def dependencies(path):
    """The libraries path links, without its own install name."""
    own = run("otool", "-D", path).splitlines()[1:]
    refs = []
    for line in run("otool", "-L", path).splitlines()[1:]:
        ref = line.strip().split(" (compatibility")[0]
        if ref and ref not in own:
            refs.append(ref)
    return refs


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset", load_commands(path))


def resolve(ref, loader, executable):
    """The file a library reference names, as seen from the loading file."""
    if ref.startswith("@loader_path/"):
        candidates = [os.path.join(os.path.dirname(loader), ref[len("@loader_path/"):])]
    elif ref.startswith("@executable_path/"):
        candidates = [os.path.join(os.path.dirname(executable), ref[len("@executable_path/"):])]
    elif ref.startswith("@rpath/"):
        rest = ref[len("@rpath/"):]
        candidates = []
        for rp in rpaths(loader) + rpaths(executable):
            rp = rp.replace("@loader_path", os.path.dirname(loader))
            rp = rp.replace("@executable_path", os.path.dirname(executable))
            candidates.append(os.path.join(rp, rest))
        candidates += [os.path.join(d, rest) for d in ("/opt/homebrew/lib", "/usr/local/lib")]
    else:
        candidates = [ref]
    for c in candidates:
        if os.path.exists(c):
            return os.path.realpath(c)
    fail(f"cannot find {ref} (needed by {loader})")


def bundle_libraries(executable, frameworks):
    """Copies every non-system library the executable needs, however
    indirectly, into frameworks/ and repoints all references at the copies.
    Returns the copied files."""
    copies = {}  # library file name -> (original path, copy)
    pending = [(executable, executable)]
    fixups = []  # (copy, original) for every Mach-O file in the app
    while pending:
        original, copy = pending.pop()
        fixups.append((copy, original))
        for ref in dependencies(original):
            if is_system(ref):
                continue
            name = os.path.basename(ref)
            if name in copies:
                continue
            source = resolve(ref, original, executable)
            target = os.path.join(frameworks, name)
            shutil.copyfile(source, target)
            os.chmod(target, 0o755)
            copies[name] = (source, target)
            pending.append((source, target))

    for copy, original in fixups:
        args = []
        if copy != executable:
            args += ["-id", FRAMEWORKS_REF + os.path.basename(copy)]
        for ref in dependencies(original):
            if not is_system(ref):
                args += ["-change", ref, FRAMEWORKS_REF + os.path.basename(ref)]
        for rp in rpaths(original):
            args += ["-delete_rpath", rp]
        if args:
            run("install_name_tool", *args, copy)
    return [target for _, target in copies.values()]


def minimum_macos(files):
    """The newest minimum macOS version among the files."""
    best = (11, 0)
    for f in files:
        for v in re.findall(r"\bminos (\d+(?:\.\d+)*)", load_commands(f)):
            parts = tuple(int(p) for p in v.split("."))
            best = max(best, parts)
    return ".".join(str(p) for p in best)


# ---------------------------------------------------------------- program files

def program_files(program):
    """The program's folder contents to copy (relative paths), or just the
    program when its folder holds other programs too."""
    folder = os.path.dirname(program)
    if sum(1 for n in os.listdir(folder) if n.lower().endswith(".bas")) > 1:
        return [os.path.basename(program)]
    files = []
    for dirpath, dirnames, filenames in os.walk(folder):
        dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "tools"]
        for n in filenames:
            if not n.startswith("."):
                files.append(os.path.relpath(os.path.join(dirpath, n), folder))
    # Leave out what git ignores (high scores, self-test output).
    check = subprocess.run(["git", "-C", folder, "check-ignore", "--stdin"], input="\n".join(files),
                           text=True, capture_output=True)
    ignored = {os.path.normpath(line) for line in check.stdout.splitlines()} if check.returncode in (0, 1) else set()
    return sorted(f for f in files if os.path.normpath(f) not in ignored)


def make_icns(icon, resources, work):
    iconset = os.path.join(work, "AppIcon.iconset")
    os.makedirs(iconset)
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = size * scale
            name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
            run("sips", "-z", str(px), str(px), icon, "--out", os.path.join(iconset, name))
    run("iconutil", "-c", "icns", iconset, "-o", os.path.join(resources, "AppIcon.icns"))


LAUNCHER = """#!/bin/sh
# Runs {program} with the basika in this app; see tools/make_mac_app.py.
contents="$(cd "$(dirname "$0")/.." && pwd)"
data="$HOME/Library/Application Support/{name}"
logs="$HOME/Library/Logs"
mkdir -p "$data" "$logs"
export BASIKA_DATA_DIR="$data"
cd "$contents/Resources" || exit 1
exec "$contents/MacOS/basika" -w -x "$contents/Resources/{program}" >>"$logs/{name}.log" 2>&1
"""


# ---------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description="Build a BASIC program into a macOS .app.")
    parser.add_argument("program", help="the .bas file")
    parser.add_argument("--name", help="the app's name (default: the program's, capitalized)")
    parser.add_argument("--id", help="bundle identifier (default: org.basika.<name>)")
    parser.add_argument("--icon", help="a 1024 x 1024 PNG (default: assets/icon.png next to the program)")
    parser.add_argument("--out", default=os.path.join(ROOT, "dist"), help="output folder (default: dist/)")
    parser.add_argument("--category", default="public.app-category.games", help="LSApplicationCategoryType")
    parser.add_argument("--basika", help="use this basika binary instead of compiling one")
    args = parser.parse_args()

    if sys.platform != "darwin":
        fail("this builds macOS apps; run it on a Mac")
    program = os.path.abspath(args.program)
    if not os.path.isfile(program):
        fail(f"no such program: {args.program}")
    stem = os.path.splitext(os.path.basename(program))[0]
    name = args.name or stem.replace("_", " ").title()
    bundle_id = args.id or "org.basika." + re.sub(r"[^A-Za-z0-9.-]", "-", name.lower())
    icon = args.icon or os.path.join(os.path.dirname(program), "assets", "icon.png")
    if args.icon and not os.path.isfile(icon):
        fail(f"no such icon: {args.icon}")

    # Inside the repository the program keeps its path (demo/burrow/...), so
    # relative paths such as ../../fonts still work.
    folder = os.path.dirname(program)
    rel_folder = os.path.relpath(folder, ROOT)
    if rel_folder.startswith(".."):
        rel_folder = os.path.basename(folder)

    app = os.path.join(os.path.abspath(args.out), name + ".app")
    contents = os.path.join(app, "Contents")
    macos, frameworks, resources = (os.path.join(contents, d) for d in ("MacOS", "Frameworks", "Resources"))
    if os.path.exists(app):
        shutil.rmtree(app)
    for d in (macos, frameworks, resources):
        os.makedirs(d)

    with tempfile.TemporaryDirectory() as work:
        binary = os.path.abspath(args.basika) if args.basika else build_basika(work)
        executable = os.path.join(macos, "basika")
        shutil.copyfile(binary, executable)
        os.chmod(executable, 0o755)
        print("Bundling libraries...")
        libraries = bundle_libraries(executable, frameworks)

        print("Copying the program...")
        shutil.copytree(os.path.join(ROOT, "fonts"), os.path.join(resources, "fonts"))
        files = program_files(program)
        for f in files:
            target = os.path.join(resources, rel_folder, f)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(os.path.join(folder, f), target)

        has_icon = os.path.isfile(icon)
        if has_icon:
            make_icns(icon, resources, work)
        else:
            print(f"No icon: {os.path.relpath(icon, ROOT)} does not exist")

    launcher = os.path.join(macos, name)
    with open(launcher, "w") as f:
        f.write(LAUNCHER.format(program=os.path.join(rel_folder, os.path.basename(program)), name=name))
    os.chmod(launcher, 0o755)

    info = {
        "CFBundleName": name,
        "CFBundleDisplayName": name,
        "CFBundleIdentifier": bundle_id,
        "CFBundleExecutable": name,
        "CFBundlePackageType": "APPL",
        "CFBundleVersion": version(),
        "CFBundleShortVersionString": version(),
        "CFBundleInfoDictionaryVersion": "6.0",
        "LSMinimumSystemVersion": minimum_macos([executable] + libraries),
        "LSApplicationCategoryType": args.category,
        "NSHighResolutionCapable": True,
        "NSHumanReadableCopyright": f"Made with basika {version()}",
    }
    if has_icon:
        info["CFBundleIconFile"] = "AppIcon"
    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(info, f)

    # install_name_tool broke the libraries' signatures; sign everything again
    # (ad hoc), the libraries first and the app last.
    print("Signing...")
    for path in libraries + [executable]:
        run("codesign", "--force", "--sign", "-", path)
    run("codesign", "--force", "--sign", "-", app)
    run("codesign", "--verify", "--strict", app)

    leftovers = [ref for path in libraries + [executable] for ref in dependencies(path)
                 if not is_system(ref) and not ref.startswith(FRAMEWORKS_REF)]
    if leftovers:
        fail("libraries still linked from outside the app: " + ", ".join(sorted(set(leftovers))))

    size = sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(app) for n in ns)
    print(f"Built {os.path.relpath(app, os.getcwd())}: {len(libraries)} libraries, "
          f"{len(files)} program files, {size / 1e6:.1f} MB, macOS {info['LSMinimumSystemVersion']}+")


if __name__ == "__main__":
    main()
