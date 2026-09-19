# fix(geospatial): make range-cache staging PID portable on MSVC

**Issue:** #1055 — `[bug][geospatial] ::getpid() breaks the MSVC build of range_cache_disk.cpp`

**Track:** ds41-range-cache-msvc (Track 15). Branch `agent/ds41-range-cache-msvc`, worktree
`../exp-rs-worktrees/ds41-range-cache-msvc`.

## Base

- Base: `origin/master @ 2caac836c4bfb7f62a317d19cf5257c8a97f8388` (fetched at start; no newer commits while working).
- No open PRs at start (`gh pr list --state open --limit 100` → empty).
- Read before working: merged PRs #1030/#1031/#1032 (and the #1022/#1023 cloud-fabric lineage that
  introduced this file), open issues #1033–#1056 (esp. #1055, #1056, #1037), remote branches
  (only `origin/master`), worktree inventory.

## Scope / non-scope

In scope:
- `src/geospatial/remote/range_cache_disk.cpp` — portable process-id idiom for staging names.
- `tests/CMakeLists.txt`, `tests/test_io_range_cache.cpp` — compile-time guard + staging coverage.
- `.goal-loop-ledger.md`, `.planning/ds41-range-cache-msvc/` — planning evidence.

Non-scope (untouched):
- `src/geospatial/remote/http_fetch.cpp` (Track 13) and other `remote/` files.
- `src/geospatial/util/atomic_fs.*` — the cross-process staging-name collision of
  `stagedPathFor` is a separate #1056 finding (that helper has no PID at all; reusing it here
  would drop the PID and append the extension after `.tmp`, breaking this file's `.tmp`
  eviction/cleanup).
- No public headers, no ABI/API change, no new includes.

## Key design

`putBlock()` names staging files `<block>.<pid>.<counter>.tmp`. Replace the unconditional
`::getpid()` with a file-local anonymous-namespace helper using the repo's documented idiom
(identical shape to `env_doctor.cpp:67`, `plugin_package.cpp:261`, `tile_checkpoint.cpp:25`):

```cpp
long long currentProcessId()
{
#ifdef _WIN32
  return static_cast<long long>( ::GetCurrentProcessId() );  // <windows.h>, already included
#else
  return static_cast<long long>( ::getpid() );               // <unistd.h>, already included
#endif
}
```

The name shape, the atomic counter, and POSIX behavior are unchanged. No new helper is shared
across files (no invented cross-module API).

### Honest finding: the issue premise is partially off on this toolchain

The issue says MSVC has no `getpid` and the build breaks. Reproduced on MSVC 14.38 with the exact
`build-dev` flags (`compile_commands.json`):

- The **unmodified** TU compiles — `warning C4996: 'getpid': The POSIX name for this item is
  deprecated`, **not** a hard error.
- `/showIncludes` proves why: `getpid` is only declared because `<mutex>` → `<thread>` → UCRT
  `<process.h>` arrives transitively. The file never includes `<process.h>` and never will — it is
  an accidental declaration.
- The defect class is real in two ways: (a) the `_WIN32` branch references a POSIX name that is
  not a Windows API and is deprecated by MSVC; (b) any include-graph change (or a TU compiled
  without the transitive `<process.h>`) turns it into the reported error, as the repo already
  experienced with #993/#997 and `.planning/plugin-platform-9/REVIEW_LOG.md` (C1).

This PR fixes the contract, not just the observed warning.

## Regression coverage

- **Compile-time guard (Oracle-1):** `sicnu_range_cache_disk_pid_guard` (tests/CMakeLists.txt,
  `if(WIN32)`) recompiles the TU with `_CRT_DECLARE_NONSTDC_NAMES=0` and is a build dependency of
  `test_io_range_cache` (never linked). With the fix reverted, the real build fails:
  `error C2039` / `error C3861: "getpid": 找不到标识符` at `range_cache_disk.cpp(370)`,
  `BUILD_EXITCODE=1`. With the fix, `BUILD_EXITCODE=0`.
- **Runtime staging case:** `concurrent disk publishes all land with no temp residue`
  (`[io][remote][range_cache][fabric9][disk][staging]`): 24 concurrent `putBlock` publishers on a
  direct `RangeDiskBlockStore` instance; all 24 publishes land, read back byte-exact, no `.tmp`
  residue, 24 `.blk` files. This is a staging smoke/concurrency case — it does **not** claim to
  regress the PID source (the compile guard is what does that).

## Verification (local, MSVC 14.38 + Ninja, Debug)

Build (shared host; `CMAKE_BUILD_PARALLEL_LEVEL=1`, `CTEST_PARALLEL_LEVEL=1`, `--parallel 1`):

```text
cmake --preset dev-default -G Ninja -DCMAKE_TOOLCHAIN_FILE=... -DCMAKE_PREFIX_PATH=... \
  -DVCPKG_INSTALLED_DIR=<main build-dev>/vcpkg_installed        # configure exit 0
cmake --build build-dev --target test_io_range_cache --parallel 1
  → BUILD_EXITCODE=0 (guard object + test built; no getpid warning)
```

Suite (run from the worktree with vcpkg debug bin on PATH, `QT_QPA_PLATFORM=offscreen`):

| Run | Command | Result |
|---|---|---|
| 1 | `build-dev\test_io_range_cache.exe` | `All tests passed (175 assertions in 22 test cases)`, exit 0 |
| 2 | same | `All tests passed (175 assertions in 22 test cases)`, exit 0 |
| 3 (after guard revert/restore) | same | `All tests passed (175 assertions in 22 test cases)`, exit 0 |
| focused | `build-dev\test_io_range_cache.exe [staging] -s` | 52 assertions / 1 case PASS (`24 == 24`, `0 == 0`) |

Compile-only checks:

```text
cl /c <TXU, exact build-dev flags>                        pre-fix: exit 0 + C4996 'getpid'
                                                          post-fix: exit 0, no getpid warning
cl /c -D_CRT_DECLARE_NONSTDC_NAMES=0 <TU>                 pre-fix: exit 2 (C2039/C3861 @ :370)
                                                          post-fix: exit 0
cmake --build build-dev --target sicnu_range_cache_disk_pid_guard
                                                          pre-fix: BUILD_EXITCODE=1
                                                          post-fix: BUILD_EXITCODE=0
```

`git diff --check` → clean.

## Known pre-existing failures / limitations

- The suite runs locally in full (no pre-existing failures observed on this target).
- No POSIX lane was executed on this host; the POSIX branch is textually unchanged
  (`::getpid()` via `<unistd.h>`, already included).
- Cross-process staging collision (the reason a PID is in the name at all) remains untested;
  it needs a multi-process harness and is out of this fix's scope.
- The 22-case suite's other warnings (u8path/fopen deprecation) are pre-existing.

## Overlap / merge order

- Another local worktree (`fix/review-issues-1033-1056`, branch `fix/review-issues-1033-1056`)
  holds an **uncommitted, unpushed** inline copy of this same one-line fix in
  `range_cache_disk.cpp`. There is no PR, commit, or remote branch from it. If that branch lands
  first, this PR's one-line file change becomes a no-op conflict; the test/guard/ledger parts do
  not overlap (that worktree touches `http_fetch.cpp`, `remote_source_validator.cpp`, and app
  files). Recommended: land whichever lands first, rebase the other.
- No other track's reserved files are touched.
