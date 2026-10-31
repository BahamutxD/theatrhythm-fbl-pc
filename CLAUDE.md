# Theatrhythm FBL PC runner

Native Windows runner for the PS4 version of Theatrhythm Final Bar Line (Unity 2021.2, IL2CPP). The
game's x86-64 code runs directly; PS4 system calls are reimplemented in C and GNM rendering goes
through shadPS4's video core on Vulkan. See [docs/development.md](docs/development.md) for the
pipeline and [docs/building.md](docs/building.md) for the toolchain and packaging. No game files
are in the repository, and nothing assumes where the decrypted dump is: set `THEATRHYTHM_GAME_DIR`
to it (or pass `--game DIR` to `run.py`). Tests that need it skip without it.

## Loop
- Build: `bash build.sh` (Git Bash; it sources `msys2-env.sh` for MSYS2 CLANG64).
- Run: `python run.py --timeout 20`; read the first `STOP:` or `Fault:` line.
- Missing imports: `python run.py check` (full list in `out/game/missing-imports.txt`).
- Tests: `bash build.sh --test`, then `cd tests && python -m unittest discover -p "test_*.py"`.

## Conventions
- Names are Theatrhythm's: identifiers, files, env variables (`THEATRHYTHM_*`), log prefixes and
  patch markers in vendored code (`// theatrhythm:`).
- C runtime imports resolve by symbol name: a function goes in a `src/runtime_*.c` export table
  (`{"name",function}`) and its NID in `src/import_names.inc`. Name NIDs with
  `python tools/name_imports.py out/game --write` (candidates in `tools/ps4_names.txt`).
- `libScePosix` imports are looked up under the `libkernel` key (runtime.c, `canonical_name`).
- POSIX variants return errno; `sce*` variants return SCE codes (0x8002xxxx, errno in the low
  16 bits). See `posix_result` in runtime_mutex.c / runtime_rwlock.c.
- The loader builds with `-Wall -Wextra -Werror`. Match the surrounding terse C style and comment
  each function with the PS4 contract it follows.
- Guest-visible pointers must stay below 1 TiB (`runtime_low_alloc`).
- Keep only what this game needs: no upscalers, extra menus or hacks for other games. The renderer
  is shadPS4's with added performance work (draw preparation, memos, async pipelines); behaviour
  changes there need a boot check (`python run.py --timeout 20`).
- Releases are built by `tools/package.py` and must work from the unpacked zip alone: no repo, no
  MSYS2, no Python of the player's own. Never package game files or anything built from a dump.
- Python edits on Windows: write files with LF line endings (`write_bytes`, or
  `open(path, 'w', newline='')`): text mode would turn the tree's LF into CRLF.
