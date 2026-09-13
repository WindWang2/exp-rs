# BASELINE — scientific-contract-verification-10

## Git baseline

- `origin/master` @ track start: `7d78059d1a6d316d606656759a506d17bc5e3b55`
  ("Merge pull request #958 from WindWang2/zcode/prompt-command-hygiene-review")
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-scientific-contract-verification-10`
- Branch: `zcode/scientific-contract-verification-10` (created off `origin/master`)
- No pre-existing branch/worktree with this name existed (checked `git branch -a`,
  `git worktree list`); no residue handling needed.

## Direct predecessor tracks (reused, not rebuilt)

| Track | Merged | What it left behind | Reuse decision |
|---|---|---|---|
| `feat/unified-contract-verification-9` (PR merged pre-#895) | yes | `src/contracts/` scanner lib (text_scan, operator_param_scanner, contract_descriptor, contract_graph, command_ref_scanner, error_code_scanner, graph_assembly), `contract_inventory` snapshot tool, `data/contracts/contract_graph.snap.json`, tests `test_contract_{platform,projection,command,diagnostics,capability}_9`, `benchmark_contract9`, `scripts/verification_ladder.py`, `scripts/collect_readiness.py`, `docs/verification/CONTRACT_PLATFORM_9.md` | **Extend**. Track 10 adds the scientific semantic layer + new drift gates on top. No parallel second platform. |
| `zcode/whole-repo-line-review` (PR #957) | yes | `WHOLE_REPO_REVIEW.md`, `review/findings/`, `review/issues/F-OPS-1..5`, `F-PI-1..2`, `review/tests/F-OPS-*.cpp` assertion drafts, `review/DEDUPE.md` | **Digest**. 7 findings re-verified on `7d78059d1a` (all still live — see FINDINGS_STATUS below); drafts promoted into real tests. |
| `zcode/verification-baseline-green` (PR #954) | yes | `docs/verification/READINESS.{md,json}`, `scripts/collect_readiness.py` | **Refresh**. READINESS bound to `f0f6869b27…`; stale vs `7d78059d1a` → rebuild at final HEAD. |

## Findings status at baseline (re-verified on 7d78059d1a, not copied from review)

| Finding | Sev | Status on master | Evidence (worktree paths) |
|---|---|---|---|
| F-OPS-4 `io:reproject` srcCrsOverride dead param | P1 | **LIVE** | `src/operators/io/io_operators.cpp:306` reads it only for the refusal gate; `WarpOptions` (`src/geospatial/convert/raster_convert.h:50-62`) has no source-CRS field; `warpRaster` (`raster_convert.cpp:196-204`) writes only `-t_srs`. `io:clip` (:363-386) consumes the same param functionally → drift, not design. |
| F-OPS-1 Labels class_mapping vs output encoding | P2 | **LIVE** | `model_catalog.cpp:829-845` validates non-negative+injective only; `tile_inference_engine.cpp:866-869` picks writeType/NoData from model class count; `:1601-1613` writes productClass raw → ≥255 Byte-clamped to NoData sentinel. |
| F-OPS-3 `rs:qa_mask` fail-open | P2 | **LIVE** | `rs_qa_mask_operator.cpp` convertSample returns 0 (=clear) on NaN/negative/declared-NoData; SCL "all" set (:126-134) omits SclNoData(0). |
| F-OPS-5 detection NMS O(n²) uncancellable | P2 | **LIVE** | `detection_postprocess.cpp:150-163` nested loop, no cancel predicate; `detection_tile_engine.cpp:423` dedup runs before `context.throwIfCancelled()`. |
| F-OPS-2 TensorBlob::fromMat ND non-contiguous | P3 | **LIVE** | `tensor_blob.cpp:163-173` fallback iterates `mat.rows` (=-1 for dims>2) → zero iterations, all-zero tensor. |
| F-PI-1 pi bridge desync zombie | P2 | **LIVE** | `pi/mcp_bridge.ts` + `pi/exp-rs-spatial.ts` onStdout overflow branch clears buffer/rejects pending but never kills child / sets `exited` → lazy-respawn condition `(!this.child \|\| this.exited)` never true. |
| F-PI-2 startup-deadline fix not backported | P2 | **LIVE** | `pi/mcp_bridge.ts:144-172` has try/finally `cancelStartupDeadline()`; `pi/exp-rs-spatial.ts:178-198` arms the deadline with no success-path cancel → 30s event-loop pin per spawn. Reverse drift: spatial has fastCrashCount circuit breaker + spawn-race cleanup that mcp_bridge lacks. |

## Duplication exclusion table (what Track 10 must NOT redo)

- Contract scanner library, graph, snapshot tool → exists (`src/contracts/`), extend only.
- Determinism grade stamping (`stampDeterminismGrade`, ADR 0124) → exists in operator schema vocabulary; scientific contract layer must cross-check, not duplicate.
- Capability sidecar store + `test_algorithm_meta_drift`; capability knowledge + `test_capability_drift` → exist; Track 10 adds gates only where a projection is *unguarded* today.
- Known-answer corpus / fuzz suites (`test_contract_fuzz_*`, `test_known_answer_corpus*`) → exist; Track 10 adds scientific metamorphic / replay / seed-determinism lanes, not re-runs.
- Readiness ladder + collector → exist; refresh at final HEAD only.

## Shared-file conflict map (10 concurrent 10.0 tracks)

| File | Risk | Mitigation |
|---|---|---|
| `src/operators/io/io_operators.cpp` | other tracks may touch | narrow edit in run()/schema of Reproject only |
| `src/geospatial/convert/raster_convert.{h,cpp}` | geospatial-fabric tracks | additive field + branch only |
| `src/operators/framework/model_catalog.cpp`, `src/operators/runtime/tile_inference_engine.cpp` | model-runtime track | validation-only + encoding-selection edits, no API changes |
| `src/operators/rs/rs_qa_mask_operator.cpp` | algorithms track | semantic change localized to convertSample + SCL set + tests |
| `src/operators/runtime/detection_postprocess.{h,cpp}`, `detection_tile_engine.cpp` | algorithms track | additive cancel predicate param |
| `src/operators/runtime/tensor_blob.cpp` | runtime track | one branch fix |
| `pi/*.ts` | pi/agent tracks | both files already diverged; Track 10 converges + adds anti-drift test |
| `tests/CMakeLists.txt` | every track (append-only norm) | append-only registrations, separate integration commit |
| `.gitignore` | every track | single whitelist block append, first commit |

## Host / build baseline

- 16 cores / 62 GB RAM; ccache present (`/usr/sbin/ccache`).
- Reference build in main checkout: `build/` = Release + Ninja + ccache launcher.
- Track build: `cmake --preset ci-fast` in worktree (`build-ci-fast/`), `CMAKE_BUILD_PARALLEL_LEVEL=2`, tests `-j1`.
