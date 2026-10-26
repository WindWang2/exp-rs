# R22-07 Tasks / Milestones

## G0 — #1696 three-platform build fix ✅ (source complete; build evidence in TEST_RESULTS.md)
- [x] `<algorithm>` include in `src/lab/session_state.cpp` (minimal 1-line).
- [x] `availability.h` → `verification_availability.h` rename + all 12 references (10 `#include`, 2 doc comments).
- [x] Case-collision audit: `find src -iname` over known system-header names → only `availability.h` collided; no other rename needed (documented; no new scanner shipped — see ORACLES.md note).
- [x] Linux compile evidence (Release, host constraint documented); macOS/MSVC compile = NOT_EXECUTED locally (CI authoritative; fix shape is platform-mechanical).

## G1 — #1697 offline bundle path safety ✅
- [x] `.sh`: parse-time whitelist (`check_version`: alnum first char, `[A-Za-z0-9._-]` only) + post-fallback revalidation + realpath containment assertion before any deletion (prefix-boundary-safe compare).
- [x] `.cmd`: same whitelist via PowerShell env-var probe (injection-proof), called at parse time and after fallback; `:check_version` subroutine with errorlevel propagation.
- [x] `tests/test_r22_infra_bundle_guard.sh`: traversal battery (10 inputs) + legal versions + spaces-in-`--out` + incremental leg; RED-pinning (guard removal deletes canary — layout includes the pre-existing prefix that makes traversal resolve).
- [x] RED evidence executed against master script (CANARY_DELETED_BY_OLD_SCRIPT).
- [~] C++ `bundleBuilderRequest` validation → CROSS_TRACK (out of whitelist).

## G1 — #1698 install tree ✅ (source complete; smoke evidence in TEST_RESULTS.md)
- [x] Root `CMakeLists.txt`: `CMAKE_INSTALL_RPATH` = `$ORIGIN/../lib;$ORIGIN` (Linux) / `@loader_path/../lib;@loader_path` (macOS), set before any `add_subdirectory`.
- [x] `sicnu_worker` `install(TARGETS … RUNTIME DESTINATION bin)`.
- [x] ci.yml install smoke extended (worker presence; clean-env run).
- [x] Local: `cmake --install` → prefix; `env -i` CLI `--help`; `ldd`/`readelf` RPATH proof; worker executable check.

## G3 — test credibility ✅
- [x] #1681: ci.yml lint step moved after build (generated `ui_*.h`/`*.pb.h` exist when tidy runs).
- [x] #1682: macOS/Windows PR-lane additions (guard test / install smoke) + opt-in macOS test workflow (workflow_dispatch, no auto spend). Windows compiled-test lane deferred (dependency-stack reuse needed) → CROSS_TRACK.
- [x] #1699: 9 assertion sites fixed across 7 files — structural split (agent_ops: 500ms worker/200ms ceiling), hang-guards (governor 500ms; remote 10s with structural oracles kept), scaled budgets (`sicnu_test_time_budget.h`: 4x under sanitizers, `SICNU_TEST_TIME_SCALE` env; Release scale = 1.0 unchanged). Lower-bound assertion (`>= 300`) untouched (stable by construction).

## G6 — #1683 hygiene (partial by scope) ✅
- [x] Top-level `permissions: contents: read` in ci.yml.
- [x] qca v2.3.10 / qtkeychain v0.14.0 clone pin to commit SHA (fail-closed `rev-parse HEAD` check; SHAs resolved via GitHub API 2026-10-25).
- [x] clang-tidy lane: merge-base diff base (loud shallow fallback), CI fetch depth 500 for resolvability.
- [x] Lane header-filter narrowed to changed headers only (aligns gate with its documented "changed files only" contract; closes unchanged-legacy-header blocking loophole).
- [~] Actions SHA pinning, root legacy scripts → CROSS_TRACK (out of scope).

## G2/G4/G5 — dependency closure, local smoke driver, build-time perf
Deferred beyond this PR's verified scope (no artificially claimed work): the pieces with real verification behind them shipped above; remaining items need their own RED evidence (see FINAL_REPORT "未执行/后续").

## Verification gates (all)
- RED → GREEN → GREEN×2 for the executable oracles (bundle guard, install smoke, compile).
- Dual independent review (REVIEW_R1.md / REVIEW_R2.md).
