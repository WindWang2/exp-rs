# Platform / Sanitizer Evidence — Verification 7.0 (task I)

Host: Windows 10 x64, VS2022 MSVC 14.38, Qt 6.8.0 (x64), vcpkg deps.
Track evidence policy: no CI; every claim below is a local command + exit code.

## ASan evidence (MSVC /fsanitize=address)

Scope: the Qt-free verification core (`sicnu_runtime` sources:
fault_registry, trace, diagnostic_report). Full-tree ASan instrumentation of
vendored QGIS (~3000 TUs) is out of budget for this track; the target scope is
where this track's new memory-sensitive code lives.

- Program: `tests/asan_smoke_core.cpp` (standalone; no Catch2, no Qt).
- Build (vcvars environment):
  `cl /nologo /std:c++20 /utf-8 /EHsc /Zi /fsanitize=address /MTd /I src
   /Febuild\asan_smoke.exe /Fobuild\ %TEMP%\asan_smoke.cpp
   src\runtime\observability\fault_registry.cpp
   src\runtime\observability\trace.cpp
   src\runtime\observability\diagnostic_report.cpp`
- Run: `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1 build\asan_smoke.exe`
- Covered: concurrent id generation (4 threads × 200), ring-sink events,
  file-sink writes + rotation drain, concurrent fault arm/fire/disarm with
  exact NextN budget check, diagnostic envelope encoding.
- Result: **ASAN SMOKE OK: 800 ids, 128 ring events** (exit 0; no
  AddressSanitizer reports). During bring-up, ASan correctly flagged a data
  race in the smoke fixture itself (unlocked shared vector) — fixed in the
  fixture; the traced module produced no reports.
- Script: `.planning/verification-observability-7/asan_smoke.cmd`.

Notes: `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` is required when
launching ASan-instrumented binaries from MSYS/Git-Bash shells (interceptor
setup against msys DLLs); from cmd/vcvars it is unnecessary.

## TSAN

Not available on MSVC (no ThreadSanitizer for MSVC). Concurrency evidence for
this track relies on: deterministic stress assertions
(`test_concurrency_stress`, including the exclusive-policy overlap invariant
`maxOverlap ≤ 1` and the NextN exact-budget check under 8 threads), plus the
ASan run above. GCC/TSAN remains available on Linux lanes (preset
`sanitizer-debug` for non-MSVC covers ASan+UBSan).

## UBSan

The `sanitizer-debug` preset pairs `-fsanitize=undefined` with ASan on
GCC/Clang only; MSVC has no UBSan equivalent (partial: /RTC + /sdl are
runtime checks already implied by the Debug build). Documented as a lane gap,
not a blocker.

## Windows (MSVC) test-port gaps closed by this track

- `test_fault_matrix` — publish/pool/checkpoint failure paths now exercised
  on Windows (previously POSIX-only via fork/waitpid).

## Pre-existing Windows-only failures (NOT introduced by this track)

| Test | Symptom | Root cause (documented, owners informed via PR) |
|---|---|---|
| test_output_committer (2 cases) | sidecar publish check; "unwritable stable dir" commit succeeds | Windows directory-permission model does not honor Qt's Read/WriteOwner stripping on folders |
| test_workflow_recovery | ownerInfoLine lacks "pid" | QLockFile holds the lock file with a share mode that makes the follow-up metadata rewrite fail silently (diagnostics-only field) |
| POSIX-gated suites | fork/popen fixtures | test_fault_injection, test_exprs_external_process, test_cli_commands_json, test_workflow_cache_e2e, test_remote_source_cache, test_execution_benchmarks (test_fault_matrix now provides portable equivalents for the checkpoint/pool/publish portion) |

## Env-governance notes (Windows lane)

Test exes need on PATH: Qt bin (`C:\deps\Qt\6.8.0\msvc2022_64\bin`),
qca/kc bin, build root (project DLLs + gdald), `build/src/runtime`
(sicnu_runtime.dll), vcpkg `x64-windows/debug/bin`. `QT_QPA_PLATFORM=offscreen`.
`ctest` startup performs Catch2 discovery for ALL registered tests, so a
missing DLL on any single executable aborts the whole run — run executables
directly or keep PATH complete (script: `test_track7.sh` in this folder).
