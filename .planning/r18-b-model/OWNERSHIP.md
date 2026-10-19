# R18-B Ownership — write set / read-only set

## Write (this track)

- `src/operators/runtime/**` — all providers, engines, planner, publish, ensemble, wire, blob
- `src/runtime/gpu/**` — gpu_plane (inference-specific scheduling/resource control)
- `src/runtime/chunk/**` — inference-specific scheduling/admission/recovery
- `src/operators/framework/model_catalog.*` — manifest/catalog contract fixes (model-specific)
- `models/**` — existing catalog/manifests contract fixes only
- `src/operators/rs/rs_inference_operator.*`, `rs_model_task_operators.*` — model-only operators (NOT traditional science algorithms — those are R18-A's)
- Tests: `tests/test_model_runtime*`, `test_gpu_plane`, `test_onnxruntime_provider`, `test_r14_B_runtime_providers`, `test_model_*`, `test_r15_C_*`, `test_r18_B_*` (new), plus this track's registered blocks

## Read-only / forbidden

- `src/processing/algorithms/**`, `src/analysis/**` (R18-A)
- non-inference `src/operators/rs/**` (R18-A)
- `pi/**`, `src/workflow/**`, `src/app/**`, `src/teaching/**`, `src/verification/**`
- PR #1513's file set: `src/geospatial/**`, `src/experiment/**`, `src/data/**`, `src/dataset/**`, `src/cli/**`, its test files, and its `tests/CMakeLists.txt` tail block
- `.github/workflows/**` (CI), `scripts/dev/**` resource guards
- Shared `tests/CMakeLists.txt`, root `CMakeLists.txt`, `src/agent/CMakeLists.txt`, `src/operators/CMakeLists.txt`, common manifests, root README — default read-only; if registration of a NEW test file is unavoidable, append a minimal independent block at the model-runtime section, re-scan for conflicts before commit, and flag it in the PR body.

## Cross-track protocol

Findings outside this write set go to `.planning/r18-b-model/CROSS_TRACK_FINDINGS.md` and the PR body — never fixed here.

## Restoration note (2026-10-19)

Recon discovered merge a8d5cd510 (#1516, r15-D "keep both track ignore rules")
silently REVERTED the entire merged r15-C track (#1515): all 14 src fixes, 6
new test files, 4 modified test files (-3893 lines), while zero later commits
touched those files. R18-B (the model-runtime domain successor) restores that
content verbatim from the #1515 merge commit f326d8ca9 as its first commit.
Verification note (R1 review): confirm restoration fidelity with the TWO-STEP
form — `git diff f326d8ca9 a6ac18bcd -- <the 35 restored paths>` (empty ⇒
byte-faithful) and `git log --oneline f326d8ca9..2d2881631 -- <same paths>`
(empty ⇒ no master interaction). A single `git diff f326d8ca9 HEAD -- src/`
sweeps in ~60 unrelated post-r15-C master files and over-reports.

This adds ONE framework file beyond the nominal write-set —
`src/operators/framework/chunked_run.cpp` — which r15-C changed for the chunk
admission census (C11/C12); it is inference-admission-specific and no other
track has touched it since. Flagged in the PR body.
