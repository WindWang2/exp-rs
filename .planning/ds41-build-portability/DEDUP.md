# DEDUP — ds41-build-portability (Track D2)

Live check 2026-09-20; base `origin/master` = `adf8f9895`.

## Open PRs / open issues

- 0 open PRs, 0 open issues. No live overlap to shrink around. If a new PR
  appears mid-track that covers a subtask, shrink to the adjacent gap (per task
  rules) and record it here.

## Remote branch residue (git-derived, not prompt-derived)

`git rev-list --count origin/master..origin/<b>` / `..origin/master`:

| branch | ahead | behind | verdict |
|---|---|---|---|
| agent/ds41-http-fetch-strict | 2 | 80 | merged via #1100/#1110 lineage; no build-scope delta |
| agent/ds41-pipeline-drag-lifetime | 4 | 80 | merged via #1101 lineage; no build-scope delta |
| agent/flash-data-transaction-integrity | 4 | 80 | merged via #1105 lineage; no build-scope delta |
| agent/flash-geo-fabric-integrity | 4 | 80 | merged via #1100 lineage; no build-scope delta |
| agent/flash-lab-foundry-determinism | 6 | 80 | merged via #1107 lineage; no build-scope delta |
| agent/flash-mcp-containment-routing | 5 | 80 | merged via #1106 lineage; no build-scope delta |
| agent/flash-processing-atomic-errors | 5 | 80 | merged via #1104 lineage; no build-scope delta |
| agent/flash-workflow-integrity | 8 | 80 | merged via #1113 lineage; no build-scope delta |
| agent/glm53-desktop-lifecycle | 12 | 80 | merged via #1101 lineage; no build-scope delta |
| agent/glm53-plugin-sdk-trust | 7 | 80 | merged via #1103 lineage; no build-scope delta |
| fix/ci-master-unblock | 2 | 80 | **Protobuf CONFIG→MODULE +13/-1 CMakeLists.txt — already landed via #1108** |
| fix/r2-ci-protobuf-multimode | 1 | 80 | same landed content (duplicate of the above) |
| fix/review-issues-1033-1056 | 1 | 80 | scripts/gen_samples.* — already landed via #1111 (#1088) |

`git diff --stat adf8f979..<branch> -- cmake/ scripts/ docs/development
CMakeLists.txt CMakePresets.json vcpkg.json` confirmed: besides the three rows
above, **zero** build-scope increments on all branches. Conclusion: nothing to
cherry-pick; start clean from `origin/master`.

## Known overlap hotspots & how this track avoids them

1. **#1108 Protobuf/GDAL CI portability** — landed. This track must NOT re-patch
   `CMakeLists.txt`'s Protobuf block or `src/geospatial/io/*`. Our portable-feature
   work builds *tests* around the landed pattern (WP3) and generalises the
   CONFIG/MODULE fallback into a reusable module without changing #1108's outcome.
2. **#1019 runtime env-doctor** — landed. Our Dependency Doctor is configure-time
   only; docs cross-link, code does not duplicate runtime probes.
3. **Windows -j2 scripts (`_env.cmd`, `setup.cmd`)** — landed (ADR 0147/D7). Our
   WP4 wrapper is the missing POSIX counterpart plus failure-log collection; the
   Windows scripts stay authoritative on Windows and are *reused*, not rewritten.
4. **SicnuLabProfile / SicnuTestEnv** — landed CMake includes; WP2 presets
   reference them instead of forking them.

## Track-scope uniqueness statement

Grep evidence (live): no `CMakePresets.json` offline/lab preset; no POSIX build
cap wrapper (`CMAKE_BUILD_PARALLEL_LEVEL` appears only in `scripts/windows/_env.cmd`
and docs); no `docs/development/` directory; no configure-time dependency summary
or actionable per-package guidance; no compile-check feature probes with tests.
Each gap maps 1:1 to a work package in `ORACLES.md`.
