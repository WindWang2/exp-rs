# R22-07 Oracles & Evidence Plan

## O1 — #1696 compile (RED: master fails / GREEN: branch compiles)
- RED: master `session_state.cpp` uses `std::sort` with only `<utility>` included → GCC/MSVC error (`'sort' is not a member of 'std'` / C2039) — CI run 39883187643 in the issue; locally provable by inspection (libstdc++ does not transitively include `<algorithm>` from `<utility>` in this TU's include set — and CI proved it on 2 compilers).
- GREEN: build `sicnu_lab_runtime` + `sicnu_verification` + CLI on branch → success (Release; host GCC Debug ICE constraint in BASELINE.md).
- macOS collision half: rename removes the case-insensitive shadow of SDK `<Availability.h>`; verified by `find src -iname` sweep (no other collisions) + reference update completeness (`git grep availability.h` empty). Real macOS compile NOT_EXECUTED locally (no macOS host) — CI tier3 is the authoritative check (not waited on).

## O2 — #1697 bundle guard (RED executed / GREEN ×2)
- RED (executed, sandbox): master script + pre-existing `out/sicnu-lab-1.0` + `--version '1.0/../../victim'` → canary outside `--out` DELETED (`CANARY_DELETED_BY_OLD_SCRIPT`); also: without pre-existing prefix, `rm -f` silently no-ops but `mkdir -p` still creates dirs outside `--out` (path jamming).
- GREEN: `tests/test_r22_infra_bundle_guard.sh` — battery of 10 hostile versions (traversal ×3, absolute, `.`, `..`, separator `\`/space, `;`, `$()`, backtick) each must: exit ≠ 0, print the refusal message, leave canary intact, not recreate paths under `out/`; legal versions (`1.0`, `v1.2_beta-3`) must pass the guard (bundle recreated, stale cleaned), `--out` with spaces works, `--incremental` preserves existing content. Run ≥ 2× (plus standalone runs during dev).
- Regression semantics: removing the guard re-deletes the canary → the test pins RED by construction (sandbox pre-creates the prefix that makes traversal resolve).

## O3 — #1698 install relocatability (RED: no RPATH / GREEN: clean-env run)
- RED (master, by construction + CMake semantics): no INSTALL_RPATH anywhere → `cmake --install` then running `bin/sicnu_geo_rs_cli` without `LD_LIBRARY_PATH` fails to load `lib/libsicnu_*.so`; `bin/sicnu_worker` absent from the tree.
- GREEN (to execute post-build): `cmake --install build-r22-infra --prefix <tmp>` → `env -i PATH=/usr/bin:/bin … <prefix>/bin/sicnu_geo_rs_cli --help` exits 0 WITHOUT LD_LIBRARY_PATH; `test -x <prefix>/bin/sicnu_worker`; `readelf -d` shows `$ORIGIN/../lib` in RPATH/RUNPATH of installed bin+lib. Repeat ×2.

## O4 — #1681 lint ordering (static + reasoning; CI executes)
- RED: issue's 349-file `ui_*.h` census; lint-before-build means generated headers absent → `--warnings-as-errors='*'` fails regardless of code. GREEN: step order in ci.yml now build→lint (YAML parse-verified; real run = CI, not waited on).
- Local lane sanity: `run_clang_tidy_changed.sh origin/master build-r22-infra` lists exactly this branch's own files via merge-base (executed ✓).

## O5 — #1699 wall-clock credibility (per-site rationale in code comments)
- agent_ops: structural separation replaces in-range threshold (async ≈0ms vs blocking ≥500ms; ceiling 200ms).
- governor/remote sites: hang-guards (500ms/10s) with behavioral oracles left structural.
- budget sites: `wallClockBudgetMs(base)` = base in Release (scale 1.0), 4x under sanitizers, ×`SICNU_TEST_TIME_SCALE`; documented as NOT a license to relax (Release still enforces base).
- GREEN: affected test binaries compile & the touched cases pass (where host toolchain allows); Release semantics unchanged.

## O6 — shared-build-lock / resource policy (cross-track reusable check)
- Every build executed under `flock -x /tmp/exp-rs-r21-build.lock`, `CMAKE_BUILD_PARALLEL_LEVEL=4`, `-j4` max; recorded `free -h`/exit codes in TEST_RESULTS.md — reusable recipe for all seven tracks (nothing track-specific in the invocation).
