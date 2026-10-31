#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Assembles the release folder (dist/) from the build, and optionally zips it.

    python tools/package.py [--out dist] [--python EMBEDDABLE] [--zip] [--debug]

The folder is what a player unpacks: they need neither this repo nor MSYS2. They run
theatrhythm-setup.exe, which prepares the game image from their own dump (prepare/run.py), and then
start theatrhythm-fbl.exe or the desktop shortcut it offers.

    theatrhythm-fbl.exe, theatrhythm-setup.exe (debug info stripped), the DLLs the game needs
                    (from MSYS2 CLANG64)
    release.txt     this release; the setup stamps the image it prepares with it (src/launch.c)
    README.txt, LICENSE.txt
    prepare/        run.py, scripts/, the patch assets and settings.example.ini, run by the setup;
                    prepare/python/ holds Python for it when --python names the python.org
                    "embeddable package" (folder or .zip); otherwise the player needs Python 3.10+.

--debug packages for chasing a bug on someone's PC instead: the exes keep their debug info and
diagnose/ holds the glitch kit (packaging/windows/diagnose: start.bat arms the frame capture,
optimization toggles and reload trigger). Its zip is <release>-debug.zip. A plain --zip also writes
<release>-symbols.zip with the unstripped exes, which resolve the exe+0x... offsets of a player's
theatrhythm.log (llvm-addr2line -fe theatrhythm-fbl.exe 0x140000000+offset).

What the setup adds later (game/, paths.ini, settings.ini) and what the kit writes (diagnose/
captures, its log) is left alone, so dist/ also works as the developer's own install. Never ship
game/: it is built from a player's own dump.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zipfile

PORT = Path(__file__).resolve().parent.parent
EXES = ('theatrhythm-fbl.exe', 'theatrhythm-setup.exe')
# What prepare/run.py needs: the scripts, the assets they read and the template of settings.ini.
PREPARE_FILES = ['run.py', 'settings.example.ini', *sorted(f'scripts/{p.name}' for p in (PORT / 'scripts').glob('*.py')),
                 *sorted(f'assets/button_icons/{p.name}' for p in (PORT / 'assets' / 'button_icons').glob('*.bc7')),
                 'assets/config_icon/display.bc7']
# The glitch kit that --debug ships in diagnose/.
DIAGNOSE = PORT / 'packaging' / 'windows' / 'diagnose'

README = """Theatrhythm Final Bar Line on PC
Release {release}

You need a decrypted dump of your own copy of the game (PS4, version 1.00): the folder with
eboot.bin, sce_module, sce_sys and Media. No game files are included.

1. Run theatrhythm-setup.exe. Pick the game folder and where your saves go, then press Install.
   It prepares the game from your dump (a few seconds) and offers a desktop shortcut.
2. Press Play Game, or later start theatrhythm-fbl.exe or the shortcut.

Run theatrhythm-setup.exe again to change the game or save folder. After unpacking a newer release
over this folder, run it once more: it keeps your folders and settings.

settings.ini (made by the setup) holds the window, audio and control options; the in-game
Options > Display Settings page changes the display ones. If the game stops, the reason is in
theatrhythm.log, in the log folder of your save folder. Shader caches are kept per PC in
%LOCALAPPDATA%\\Theatrhythm Final Bar Line.

Needs Windows 10/11 x64, a CPU with AVX and a GPU with Vulkan 1.3.{python_note}
Source and license (GPL-2.0-or-later): see LICENSE.txt.
{debug_note}"""
PYTHON_NOTE = '\nThe setup needs Python 3.10 or newer (python.org) to prepare the game.'
DEBUG_NOTE = """
This is a debugging build. To catch a rendering glitch, start the game with diagnose\\start.bat
(its log goes to diagnose\\theatrhythm.log). While the glitch shows, run 1-capture.bat, then
2-optimizations-off.bat, then 3-reload.bat, noting after each whether the glitch changed. Restart
the game afterwards, and send the diagnose folder (log and captures).
"""


def pe_imports(path):
    """The DLLs a PE file imports, normal and delay-loaded."""
    data = path.read_bytes()
    fh = struct.unpack_from('<I', data, 0x3C)[0] + 4
    sections, opt_size = struct.unpack_from('<H', data, fh + 2)[0], struct.unpack_from('<H', data, fh + 16)[0]
    opt = fh + 20
    if struct.unpack_from('<H', data, opt)[0] != 0x20B:
        raise SystemExit(f'{path}: not a 64-bit PE file')
    table = opt + opt_size

    def offset(rva):
        for i in range(sections):
            vsize, va, raw, ptr = struct.unpack_from('<4I', data, table + i * 40 + 8)
            if va <= rva < va + max(vsize, raw):
                return ptr + rva - va
        raise SystemExit(f'{path}: bad import table')

    names = []
    # Import descriptors are 20 bytes with the name at +12; delay-load descriptors 32 with it at +4.
    for directory, size, name_at in ((1, 20, 12), (13, 32, 4)):
        rva = struct.unpack_from('<I', data, opt + 112 + directory * 8)[0]
        if not rva:
            continue
        at = offset(rva)
        while (name := struct.unpack_from('<I', data, at + name_at)[0]):
            start = offset(name)
            names.append(data[start:data.index(b'\0', start)].decode('ascii'))
            at += size
    return names


def dll_closure(exe, bin_dir):
    """The DLLs `exe` needs from MSYS2, and theirs. What Windows has (System32, API sets) stays
    Windows': vulkan-1.dll is the driver's, as when run.py starts the game, also on a build machine
    without a GPU driver (CI), where System32 has none."""
    system = Path(os.environ.get('SYSTEMROOT', r'C:\Windows')) / 'System32'
    queue, seen, found, missing = list(pe_imports(exe)), set(), [], []
    while queue:
        name = queue.pop(0)
        key = name.lower()
        if (key in seen or key.startswith(('api-ms-', 'ext-ms-')) or key == 'vulkan-1.dll'
                or (system / name).exists()):
            continue
        seen.add(key)
        source = bin_dir / name
        if not source.is_file():
            missing.append(name)
            continue
        found.append(source)
        queue += pe_imports(source)
    if missing:
        raise SystemExit(f'Not in {bin_dir}: {", ".join(missing)} (packages in docs/building.md)')
    return found


def release_name():
    """theatrhythm-fbl-pc-<commit>-<date>, or -<commit>-dirty-<date>-<time> for uncommitted work:
    what release.txt and the zip are named after. Two releases from one dirty tree differ by the
    packaging time, so the launcher asks for the setup again after either is unpacked."""
    def git(*args):
        return subprocess.run(['git', *args], cwd=PORT, capture_output=True, text=True).stdout.strip()
    described = git('describe', '--tags', '--always', '--dirty') or 'unknown'
    when = time.strftime('%Y-%m-%d-%H%M') if described.endswith('-dirty') else git('log', '-1', '--format=%cs')
    return f'theatrhythm-fbl-pc-{described}-{when}'


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def strip(source, target, bin_dir):
    """Copies an exe without its debug info (RelWithDebInfo DWARF is ~95% of theatrhythm-fbl.exe).
    Crash logs give exe+offset, which the unstripped build resolves."""
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(bin_dir / 'llvm-strip.exe'), '--strip-all', '-o', str(target), str(source)], check=True)


def embeddable_python(source, target):
    """Unpacks or copies the python.org embeddable package to target (python.exe at its root)."""
    shutil.rmtree(target, ignore_errors=True)
    if source.suffix.lower() == '.zip':
        with zipfile.ZipFile(source) as z:
            z.extractall(target)
    else:
        shutil.copytree(source, target)
    if not (target / 'python.exe').is_file():
        raise SystemExit(f'{source}: no python.exe (expected the embeddable package from python.org)')
    # Its ._pth file fixes sys.path, without the running script's folder: the scripts import each
    # other (from prepare import ...), so prepare/scripts goes on it.
    for pth in target.glob('python*._pth'):
        lines = pth.read_text().splitlines()
        if '../scripts' not in lines:
            pth.write_bytes(('\r\n'.join(['../scripts', *lines]) + '\r\n').encode())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--out', type=Path, default=PORT / 'dist')
    p.add_argument('--python', type=Path, help='python.org embeddable package (folder or .zip) to ship in prepare/python')
    p.add_argument('--zip', action='store_true', help='also write out/<release>.zip (and <release>-symbols.zip)')
    p.add_argument('--debug', action='store_true', help='keep debug info in the exes and ship the diagnose/ kit')
    a = p.parse_args()
    for exe in EXES:
        if not (PORT / 'out' / exe).is_file():
            raise SystemExit(f'out/{exe} not found: build first (bash build.sh).')
    bin_dir = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64')) / 'clang64' / 'bin'
    out = a.out.resolve()
    release = release_name()
    shipped = []

    def ship(source, name):
        copy(source, out / name)
        shipped.append(name)

    for exe in EXES:
        if a.debug:
            ship(PORT / 'out' / exe, exe)
        else:
            strip(PORT / 'out' / exe, out / exe, bin_dir)
            shipped.append(exe)
    # The kit's own files come and go with --debug; its captures and log stay.
    for bat in sorted(DIAGNOSE.glob('*.bat')):
        if a.debug:
            ship(bat, f'diagnose/{bat.name}')
        else:
            (out / 'diagnose' / bat.name).unlink(missing_ok=True)
    dlls = dll_closure(PORT / 'out' / EXES[0], bin_dir)
    wanted = {dll.name.lower() for dll in dlls}
    for old in out.glob('*.dll'):
        if old.name.lower() not in wanted:
            old.unlink()
    for dll in dlls:
        ship(dll, dll.name)

    prepare = out / 'prepare'
    python = prepare / 'python'
    for item in prepare.iterdir() if prepare.is_dir() else ():
        if item != python:
            shutil.rmtree(item) if item.is_dir() else item.unlink()
    for name in PREPARE_FILES:
        ship(PORT / name, f'prepare/{name}')
    if a.python:
        embeddable_python(a.python, python)
    if python.is_dir():
        shipped += [str(f.relative_to(out)).replace('\\', '/') for f in sorted(python.rglob('*')) if f.is_file()]

    (out / 'release.txt').write_bytes(f'{release}\n'.encode())
    (out / 'README.txt').write_bytes(README.format(release=release, python_note='' if python.is_dir() else PYTHON_NOTE,
                                                   debug_note=DEBUG_NOTE if a.debug else '')
                                     .replace('\n', '\r\n').encode())
    ship(PORT / 'LICENSE', 'LICENSE.txt')
    shipped += ['release.txt', 'README.txt']
    size = sum((out / name).stat().st_size for name in shipped)
    print(f'{out}: {release}{" (debug)" if a.debug else ""}, {len(dlls)} DLLs, {len(shipped)} files, {size / 2**20:.0f} MiB'
          + ('' if python.is_dir() else ' (no Python: the player needs Python 3.10+)'))

    if a.zip:
        archive = PORT / 'out' / f'{release}{"-debug" if a.debug else ""}.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name in sorted(shipped):
                z.write(out / name, f'{release}/{name}')
        print(f'{archive}: {archive.stat().st_size / 2**20:.0f} MiB')
        if not a.debug:
            symbols = PORT / 'out' / f'{release}-symbols.zip'
            with zipfile.ZipFile(symbols, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
                for exe in EXES:
                    z.write(PORT / 'out' / exe, exe)
            print(f'{symbols}: {symbols.stat().st_size / 2**20:.0f} MiB (not for players)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
