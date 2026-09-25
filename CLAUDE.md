# proc_template - developer context

A skeleton AutoBleem **scanner processor** (the launcher's `docs/scanner-processors-plan.md`, CLAUDE.md
"Scanner processors"): a plain console program in `System/Processors/<name>/` that the launcher's scan runs
before it reads the games. This repository is meant to be copied wholesale to start a new processor - see
`README.md`'s "Starting a new processor" for the checklist of what to rename/fill in.

- **No AutoBleem SDK**, on purpose: the C++ standard library plus `src/fsutil.h`/`src/fsutil.cpp` (a
  portable filesystem layer: UTF-8 paths, the protocol's `say()`/`Percent`/`Heartbeat`, atomic
  `saveFile`/`copyWithProgress`, `env()` for the protocol's environment variables) and
  `src/strutil.h`/`src/strutil.cpp` (string helpers unrelated to the filesystem). Both are dependency-free
  and safe to copy into another processor on their own.
- **`src/main.cpp`**: the protocol plumbing (`--version`, `--ismine`, `--start --games`/`--start --rom`) is
  already wired up and correct; `processOne()` is the one function meant to be replaced. Its doc comment at
  the top of the file is the protocol reference - read it before changing the control flow around it.
- **The rules every processor keeps** (and `fsutil::saveFile`/`copyWithProgress` already do the first three
  for you): every file through `<name>.part` and a rename, never an overwrite, idempotent, stay inside what
  you were given, say something at least every `Timeout=` seconds (`fsutil::Heartbeat`).
- **Tests**: `tests/selftest.cpp` runs the real built binary against a generated sample Games tree and checks
  the protocol, the output file, and idempotency (ctest; natively and again for i386 in CI). It is
  deliberately self-contained (no fsutil/strutil) so it tests the binary as an outside caller would.
- **Build**: `./make_win.sh` (MSYS2 UCRT64, the Windows folder in `dist/proc_template/`); `ci/build.sh
  native|psc|rpi|rpi64|pcusb|win|package|all` in `ghcr.io/autobleem2/autobleem-build` (`:develop` for
  develop, `:latest` for a v* tag). The toolchain files (`toolchains/`) are the standard AutoBleem processor
  set - copy them into a new processor unchanged; nothing under `toolchains/` is project-specific. The
  package is `dist/proc_template-<version>.zip`: the folder with `bin/psc`, `bin/linux-armhf`,
  `bin/linux-arm64`, `bin/linux-i386` and `bin/windows-x86_64` - the generic keys, so one build serves every
  Pi and every PC stick.
- **CI**: `.github/workflows/build.yml`, gated by the repository variable `AB_CI_ENABLED` like every
  autobleem2 repository; develop keeps the rolling `nightly` release, a `v*` tag makes a release.
- The version is `package/processor.ini`'s `Version=` (`AB_VERSION` in CI). The repository name follows the
  owner's rule: a processor's repository is `proc_<name>`.
- A second, real-world processor of the same shape (protocol split across one file instead of
  fsutil/strutil) is `autobleem2/proc_unzip` - read there for how a `Kinds=rom` processor and a vendored
  third-party library (`third_party/miniz`) look in practice. Do not alter that repository from here; it is
  a separate, independently maintained example.
