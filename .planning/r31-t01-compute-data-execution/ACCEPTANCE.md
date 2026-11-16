# Acceptance

Base at track start: `85ce917bea0ece4df6548a677b97a72ecde8659b`.
Upstream at PR preparation: `fa5495fd70` (origin/master, #2069).
Branch: `zcode/r31-t01-compute-data-execution-20261115T135418Z`.

Every entry below is tied to a command, an exit code and a real test-case
count recorded in `TEST_LOG_INDEX.md`. 0-match filters, NOT_BUILT,
DISCOVERY_FAILED and environment skips are never counted as passes.

| Gate | Status | Evidence |
|---|---|---|
| Configure/build | **DONE** | `cmake --preset dev-default` exit 0 (real Ninja files, OpenCV prefix pinned); final build of 42 affected targets on the rebased HEAD exit 0 (`final_verify.log`, `BUILD_OK`, 0 `error:` lines). |
| E2E-1 reports vs artifacts | **DONE** | `test_r31_t01_result_semantics`: RED on the WP02 baseline (exit 42; value reports committed as assets — commitCalls 1 vs 0, fabricated assetId, autoLoad fired, 19 files written beside the input, catalogGeneration 3 vs 1) → GREEN (exit 0; 4 cases / 99 assertions). `test_io_operators`, `test_io_finalize_manifest`, `test_output_committer` exit 0. |
| E2E-2 source→VRT→task/delivery | **DONE (implemented scope)** | `test_r31_t01_vrt_nodata` (source NoData vs fill, GDAL per-pixel/mask/NoData oracle) exit 0; `test_r31_t01_vrt_reuse` (identity sidecar rejects same-grid/different-band/dtype/NoData/rotation/content) exit 0; `test_r30_t03_warp_oracle`, `test_r30_t03_generations`, `test_r30_t03_vrt_prepare_control`, `test_virtual_raster_create`, `test_data_manager` exit 0 after the sidecar-aware glob fix. Old-reader generation retention (WP09) NOT_EXECUTED. |
| E2E-3 ROI→samples→classification/postprocess/evaluation | **DONE (WP11 scope)** | `test_r31_t01_sampling_admission` exit 0 (pre-admission typed BudgetExceeded, sample identity unchanged); `test_r30_t01_sampling_budget`, `test_r30_t01_science_e2e`, `test_r30_t01_evaluation_limits`, `test_r30_t01_postprocess_windows` exit 0. WP12/WP13 NOT_EXECUTED. |
| E2E-4 model identity→forward→cancel/unload/reload | **DONE (CPU/HTTP scope)** | `test_r31_t01_model_identity` exit 0 (config isolation, directed unload, live holders after release); `test_provider_http` exit 0 (8 cases incl. new dispatcher/cancel/timeout); `test_r30_t02_session_identity`, `test_r30_t02_cancel_isolation`, `test_r30_t02_real_e2e`, `test_r30_t02_working_set`, `test_r30_t02_plan_run_contracts`, `test_model_runtime`, `test_model_runtime_stress` exit 0. ORT in-forward cancel NOT_EXECUTED (`SICNU_WITH_ONNX_RUNTIME=OFF`). |
| E2E-5 workflow→checkpoint→resume→cache delivery | **DONE** | `test_r31_t01_workflow_resume` exit 0 (relative checkpoint pinned before CWD change); `test_r31_t01_output_recovery` exit 0 (Error→restore→cache hit converges to Ready under a stable AssetId; unchanged hit keeps the revision; manifest-gated marker hits); `test_r30_t04_cache_delivery`, `test_r30_t04_workflow_resume_guards`, `test_r30_t04_host_lane`, `test_r30_t04_wait_boundaries`, `test_chunk_adoption_11`, `test_chunk_resume_11`, `test_r29_t02_published_claim`, `test_workflow_run_lock_r4`, `test_d17_workflow_pipeline_e2e`, `test_r14_C_d17_terminal_persist` exit 0. |
| Affected legacy regressions | **DONE** | Final 42-target run on the rebased HEAD (`final_verify.log`): **42/42 exit 0 — 343 cases / 118,692 assertions**. The two R30 VRT suites that failed on the pre-rebase run (WP08 sidecar in the glob) are green after the test-only fix. |
| Negative paths | **DONE (covered scope)** | Cancellation/failure/restart/reopen covered by the suites above (published-claim demotion, cache-hit recovery, kind refusal, timeout, dispatcher refusal, cancel-then-recover, budget refusal before allocation). The WP16 concurrent-interleaving oracle is NOT_EXECUTED (no deterministic in-process seam; disclosed). |
| Independent R1 | **BLOCKED** | Every `Agent` launch failed with a provider quota limit (monthly usage). A SELF-review was performed and recorded in `REVIEW_R1.md`; it is explicitly NOT the independent review. Retry before/at PR time. |
| Fix + independent R2 | **BLOCKED** | Depends on R1. The self-review findings S1 was fixed; S2–S4 are disclosed/accepted with evidence. |
| Final upstream/rebase/diff check | **DONE** | `git fetch origin --prune`; origin/master advanced to `fa5495fd70` (#2068/#2069, Pi P2/P3); file-overlap check showed only additive `task_center` changes; #2061 still open (active-exclude holds); clean rebase (19 commits, no conflicts); `git diff --check` clean; full affected-test re-run on the rebased HEAD green (42/42). |
| PR delivery | PENDING | See `PR_BODY.md` at push time. |

Online CI is intentionally not awaited and will not be described as passed.
