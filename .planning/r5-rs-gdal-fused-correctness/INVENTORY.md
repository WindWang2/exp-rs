# R5 Track 02 — RS/GDAL fused execution, operator correctness, NoData/buffer lifecycle

## Execution-time inventory (refreshed 2026-09-28, before any code change)

- **BASE_SHA**: `a726d17a6224632d929e782e996351732632f272` (origin/master, identical to the
  2026-09-27 planning snapshot — snapshot had drifted 0).
- **Worktree**: `/home/kevin/project/exp-rs-r5-rs-gdal-20260928`,
  branch `hardening/r5-rs-gdal-fused-correctness-20260928`.
- **Open PRs at start**: #1365 (R5 track 04, experiment/agent-harness persistence —
  no file overlap with this track's src/processing, src/operators/rs paths).
- **Recently merged relevant PRs**: #1354 (R4 CI red-zone), #1347 (perf/memory observatory),
  #1340 (R4 operator correctness audit — 7 NoData/sentinel defect fixes; the follow-up debt
  this track owns), #1338 (io/processing correctness).
- **Seed issue**: #1356 `test_fused_chain` stable SEGFAULT in "fused NDVI→threshold
  bit-identical" (P1, memory-safety, env: GCC 16.2.1 / Qt 6.11.2 / GDAL 3.13.3 / Debug).
  Status at execution time: OPEN, 0 comments, unassigned.
- **Adjacent issues (not this track)**: #1355 GDAL_DRIVER_PATH pin → Track 05; #1357/#1358
  heap-corruption/race in test_capability_surface_parity / test_d17 → other tracks.
- **Machine/build facts**:
  - CachyOS, GCC 16.2.1, Qt 6.11.2 (system), no system cmake, no system GDAL.
  - GDAL 3.13.3 dev root at `/home/kevin/pwb-sdks/root/usr` (libgdal.so.39.3.13.3,
    self-contained deps when LD_LIBRARY_PATH includes its lib dir). cmake 3.30.5 portable
    at `/tmp/cmake-3.30.5-linux-x86_64/bin`; ninja 1.12.1 at
    `/home/kevin/pwb-sdks/root/usr/bin/ninja`.
  - Only GDAL 3.13.3 exists on this machine (no 3.10). Version-matrix scope: document
    isolation evidence; container runtimes (docker/podman) unavailable.
  - Catch2 v3.7.1 fetched from local cache `/home/kevin/bld-pm/_deps/catch2-src`
    (matches the repo-pinned GIT_TAG exactly; no network fetch).
- **Build/test entry**: `ctest -R test_fused_chain` inside `build/`;
  `QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib:/usr/lib`.
  All builds `-j2` max per parallel-track contract.

## Master P0 discovered at configure time (this track fixes it, minimal scope)

Fresh `cmake` configure of master a726d17a6 with `ENABLE_TESTS=ON` FAILS:
`tests/CMakeLists.txt:14728: Parse error. Expected a command name, got unquoted argument
with text "======="`. Root cause: merge commit `7bb6398c0` ("Merge ... into tmp-merge-1345",
parent of PR #1345) committed an UNRESOLVED conflict region: a stray `=======` line at
14728 and a glued comment line at 14543 (lost newline). Verified via `git show 7bb6398c0^1`
vs `^2` vs result: no other content lost (the 4 "dropped" parent-2 lines are older
revisions superseded by parent-1 lines that master keeps). This blocks EVERY fresh build
of every parallel R5 track; fixed here as a 2-line surgical restore of the pre-conflict
text so this track (and siblings) can build.

## Scope boundaries (not this track)

- `GDAL_DRIVER_PATH` ctest env pin (#1355) → Track 05.
- New GPU backends / new algorithm families → forbidden (existing operators only).
- Dataset governance / experiment storage → other tracks.
- #1357/#1358 (capability surface / d17 teardown races) → different modules.
