# resource-discipline.md — the -j1/-j2 local build policy

## The rule

The project's resource discipline (ADR 0147, D7) is non-negotiable for local
builds:

| quantity | bound |
|---|---|
| compiler/build parallelism | default **1**, hard cap **2** — `-j3+` is refused, never silently run |
| inherited `CMAKE_BUILD_PARALLEL_LEVEL` / `CTEST_PARALLEL_LEVEL` above the cap | clamped to the cap (a stale environment must not defeat the policy) |
| ctest parallelism | always **1** |
| GUI tests | always `QT_QPA_PLATFORM=offscreen` |

Every lane the wrappers expose enforces this: `scripts/build.sh` (POSIX) and
`scripts\windows\build.cmd` (Windows — which additionally loads the MSVC
developer environment through `scripts\windows\_env.cmd`).

## The wrappers

```sh
# POSIX (Linux/macOS, and Git Bash on Windows)
scripts/build.sh selftest                 # hermetic: cap matrix + path/space/Unicode quoting
scripts/build.sh doctor                   # dependency report from a configured tree
scripts/build.sh configure [--build-dir D] [--preset P] [--base-cache CACHE]
scripts/build.sh build [--build-dir D] [--jobs N] [target ...]
scripts/build.sh test  [--build-dir D] [-R regex]
scripts/build.sh smoke [--build-dir D] [--base-cache CACHE]   # clean-tree configure
scripts/build.sh preset-check             # CMakePresets.json assertions
scripts/build.sh dep-fixture-test         # dependency-doctor contract gate
```

```bat
rem Windows (same subcommands)
scripts\windows\build.cmd selftest
scripts\windows\build.cmd build --build-dir build-dev sicnu_geo_rs_cli
```

Key behaviours:

* **Nothing downloads dependencies.** `--base-cache <CMakeCache.txt>` seeds
  only *toolchain locations* (generator, compilers, Qt/vcpkg/QCA/keychain
  prefixes, bison/flex) from an existing configured tree and reuses its vcpkg
  installed tree read-only (`-DVCPKG_MANIFEST_MODE=OFF`), so the clean-tree
  gate is offline and repeatable. Every dependency is re-discovered fresh —
  a stale cache can never pass for a working configure.
* **Logs.** Each configure/build/test run tees to
  `build-logs/<name>/run.log`; failures also write
  `build-logs/<name>/SUMMARY.txt` (error/failed-line extract + tail).
* **Refusal, not silent clamping, of explicit intent.** `--jobs 3` exits 2
  with the cap printed. Raise the cap only with `SICNU_BUILD_JOBS_CAP` — and
  know that you have left the documented discipline.

## CMakePresets

`CMakePresets.json` carries the same policy declaratively: build presets pin
`jobs` (dev 1, CI 2, sanitizer 1) and test presets pin
`CTEST_PARALLEL_LEVEL=1` + `QT_QPA_PLATFORM=offscreen`. `scripts/build.sh
preset-check` asserts this; `cmake --list-presets` parses it.

## Why the caps exist

Deep Qt/QGIS/GDAL translation units exhaust memory well before the CPU is
saturated: the highest-value setting on a workstation is *serial compiles with
early failure*, not throughput. Compile one target, read its errors, then move
on — the wrappers exist so this is the path of least resistance.
