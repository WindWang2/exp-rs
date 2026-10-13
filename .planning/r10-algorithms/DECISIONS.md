# DECISIONS — R10-A (automatic defaults, minimal-blast-radius rationale)

| # | decision | rationale |
|---|---|---|
| D1 | Worktree `exp-rs-r10-algorithms` off `origin/master@8a0864276`; master untouched | prompt §0/§1; branch/dir names were free (verified) |
| D2 | `resources/icons` symlink→junction is a LOCAL build fix, excluded from every commit | same workaround the r9 worktrees use; committing it would rewrite a tracked symlink as a Windows junction |
| D3 | RED tests appended to EXISTING per-domain test files instead of new targets | zero churn in the shared hot file tests/CMakeLists.txt; fixtures live next to their domain |
| D4 | Commit order: tests-only commits first (RED evidence), fix commits after (GREEN evidence) | clean RED→GREEN attribution without stashes |
| D5 | AN-1 fix upgrades `ignoreNaN` semantics to non-finite (covers ±Inf) instead of adding a second flag | same discipline as multires/otb/scaler (`!isfinite`); a new opt-out flag would keep the default path unsafe |
| D6 | PR-1 fix PARSes the documented string bounds form (rather than only rejecting it) | the schema is the operator's published contract (capability sidecars feed agents); honor it, and fail typed when unparseable — mirrors io:clip fail-closed style |
| D7 | PR-8 style: tolerant numeric parsing at operator call sites (accept numeric strings that schema_validator accepts) | validator acceptance is the published contract; tightening the validator instead would break legitimate callers. No new dialect: reuse in-file helpers (getInt etc.) |
| D8 | PR-2 fix: stage-beside-target + atomic publish for runGdalWarpOnDataset (and pansharpen/mosaic cleaners); reuse existing StagedRasterOutput/atomic_fs machinery if linkable, else minimal local staging | #617 contract "no half-output AND no destroyed old product"; keep per-operator change small |
| D9 | AN-4 fix: fixed default seed in RsClassifierKMeans ctor (optional param) + theRNG().state seeding before cv::kmeans | keeps public API compatible; restores the pipeline's documented determinism policy at the backend |
| D10 | AN-2 fix: working-copy (sequential update) semantics for sieve, matching gdal_sieve | frozen-snapshot reads are what allow the label swap; gdal parity is the reference |
| D11 | Batch-2 prepared as patch by S2, applied only after RED confirmation | preserves RED→GREEN discipline while parallelizing authorship |
| D12 | `src/processing/framework/*` (except task_center.*) treated as in-scope for A7 fixes (algorithm_preflight, atomic_algorithm_adapter) | prompt's A7 covers the registry→validate→execute chain seam; no open PR owns these files; task_center explicitly excluded |
| D13 | A1-D1 (capability sidecar array semantics): root cause in src/agent/harness (hard-excluded) → in-scope part = atomic_algorithm_adapter consumption + sidecar data + gate test; generator fix goes to INTERFACE_HANDOFF.md | no cross-track file edits |
| D14 | #1467 folds double-implementation + processing/CMakeLists misplaced sources: analysis-side consolidation only if low-risk; core-file moves need the core owner → partial handoff | src/core files are not owned here |
