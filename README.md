# proc_template

A skeleton AutoBleem **scanner processor** - copy this repository to start a new one. It builds a working
(if pointless) processor out of the box: `src/main.cpp` has the protocol wired up, `src/fsutil.h` and
`src/strutil.h` have the utility functions most processors end up needing, and `processOne()` in main.cpp is
the one function meant to be replaced with your own logic. Everything else - the CMake build, the cross
toolchains, CI, the self-test - already works.

For a second, real-world example of the same shape, see
[`autobleem2/proc_unzip`](https://github.com/autobleem2/proc_unzip) - "the example to copy" before this
template existed, kept as-is; read it if you want to see the protocol done as one self-contained file
instead of split into fsutil/strutil.

## Starting a new processor

1. Rename the repository `proc_<yours>` (the owner's convention: a processor's repository is `proc_<name>`).
2. Rename the `proc_template` target: `CMakeLists.txt`'s `add_executable(proc_template ...)` and the
   `PROC_EXE`/`proc_template_selftest` bits under it, `package/processor.ini`'s `Exec=`, `make_win.sh`,
   `ci/build.sh`'s `BIN=`, `ci/proc_check.sh`'s `BIN=`, and `.github/workflows/build.yml`'s artifact/cache
   names.
3. Fill in `package/processor.ini`: `Name=`, `Description=`, `Author=`, `Kinds=`, `Match=`, `Systems=`,
   `Timeout=`.
4. Replace `src/main.cpp`'s `Extensions` and `processOne()` with your real logic; delete `isMine()` and the
   `--rom` handling in `main()` if this is a `Kinds=games-folder` processor (most are) - keep them if it is
   `Kinds=rom`.
5. Update `tests/selftest.cpp`'s sample file name/extension to match, and add real checks for what your
   processor actually produces.
6. Vendor any third-party code your processor needs under `third_party/` (proc_unzip's `third_party/miniz`
   is the pattern to copy: one library, its own LICENSE file, included with `target_include_directories`).

## What it does, as shipped

| it is given | it does |
|---|---|
| `--start --games <Games>` | every `*.example` file under the tree gets a `.out` file next to it (placeholder logic - see `processOne()`) |
| `--ismine --rom <file> --system <name>` | "mine" (exit 0) for a `.example` file - only reached for a `Kinds=rom` processor |
| `--start --rom <file> --system <name>` | the same `processOne()`, for one file |

## The protocol, in short

```
proc --version                      one line: "#<Name> V<version> - <description>", exit 0
proc --ismine --rom <file> --system "<name>"     exit 0 = mine, 1 = not mine (Kinds=rom only)
proc --start --games <dir>          the whole Games tree (Kinds=games-folder)
proc --start --rom <file> --system "<name>"      one ROM (Kinds=rom)
```

While it works it prints, one flushed line at a time: `#Starting - <title>` first, `#<stage>` for each new
stage, `0`..`100` for the percent, `n/m` for a counter, `#WARN - <text>`, and at the end `#DONE` (exit 0) or
`#ERROR - <text>` (exit 1). The environment carries `AB_PROCESSOR_PROTOCOL=1`, `AB_ROOT`, `AB_GAMES_DIR`,
`AB_ROMS_DIR`, `AB_RDB_DIR`, `AB_TMP`, `AB_PLATFORM`, `AB_PLATFORM_KEYS`, `AB_LANGUAGE` and `AB_VERSION`
(read any of them with `fsutil::env()`). The launcher's `docs/scanner-processors-plan.md` is the whole
design.

The rules: atomic (`<name>.part`, then rename - `fsutil::saveFile`/`copyWithProgress` do this for you),
idempotent (a second run on its own output does nothing), never overwrite, stay inside what you were given,
say something at least every `Timeout=` seconds (`fsutil::Heartbeat`).

**Check yours before you publish it** with the launcher's `tools/proc_check.py`:

```
python tools/proc_check.py path/to/yourproc --games sample/Games
```

It runs your processor on scratch copies of the samples and reports every rule it can see broken: the
protocol, `*.part` files left behind, writes outside the target, a second run that still has work to do, and
a run stopped in the middle that cannot finish when started again.

## The utility headers

- **`src/fsutil.h`** - a portable filesystem layer: every path is a UTF-8 `std::string`, `/`-separated, on
  every system (Windows gets the wide UTF-16 API underneath). `say()`/`Percent`/`Heartbeat` for the
  protocol's output, `listDir`/`findFiles`/`makeDirs`/`removeFile`/`removeDirRecursive` for the disk,
  `saveFile`/`copyWithProgress` for the atomic-write rule, `env()` for the protocol's environment variables.
  Read the doc comment on each function - most have a short usage example right there.
- **`src/strutil.h`** - string helpers that are not about the filesystem: `trim`, `split`/`join`,
  `startsWith` (paired with `fsutil::endsWith`), `replaceAll`, `humanSize` (for a `#WARN` line), and
  `tryParseUInt` (a command-line argument or `.ini` value should be rejected outright, not silently read as
  0).

Both are plain, dependency-free C++14 - copy either file into another processor on its own if that is all
you need.

## Building

- Every target at once, as CI does it: `ci/build.sh all` in the `ghcr.io/autobleem2/autobleem-build:develop`
  image - `dist/proc_template-<version>.zip` with the binaries for the console, both Pis, the PC stick and
  Windows.
- Windows by hand: `./make_win.sh` in an MSYS2 UCRT64 shell - builds, runs the self-test, and lays out
  `dist/proc_template/`.
- Anything else: `cmake -S . -B build && cmake --build build && (cd build && ctest)`.

Every push to `develop` builds the rolling `nightly` release; a `v*` tag makes a release. CI also runs the
launcher's `tools/proc_check.py` over the native build (`ci/proc_check.sh`).
