# Independent Review R2 — Windows integration follow-up

Review status: **PASS**. Independent Standards and Spec blockers: **0**, both bound to code commit `b3b8adf1fbd5f121eb039b00b3a730a1f069a98b`. Final Windows native verification passed. This entry supersedes the previous provider-quota blocker for this PR. The historical Linux implementation review remains in REVIEW_R1.md.

## Fixed review points

- Original PR head: `875de770f3d98892d26bcf25341912d22fe1707c`.
- Latest master merged before validation: `265b82dd11252ece22e54d912168a785b39d704c` (local merge `cc378f691dd2bd62f5bc1b9fcba3478fa091b0ba`).
- Standards: two P2 findings in VRT prepared-identity handling (NaN JSON loss and an undocumented 64 KiB identity limit).
- Spec: the same NaN finding violates the documented prepared-warp reuse contract. Disclosed WP09/WP10/WP12/WP13 and ORT/concurrent-digest limitations were not recast as missing promised implementation.

## Integration findings and fixes

1. **Legacy producers (P1):** the new `producesFile` gate omitted existing raster/vector/table output helpers and plain string output ports paired with an explicitly marked file destination parameter. The real Python MVP suite reproduced three failed cases: execution completed, but the agent received no committed output asset. The central descriptor builder now accepts these existing schema contracts. Plain string/object reports, format-only hints and explicit null outputs remain outside the file commit channel. No Python or algorithm-id exception was introduced.
2. **VRT prepared identity (P2):** NaN and infinities use strict tagged JSON values; finite numeric sidecars remain compatible. Null/unknown values cannot alias zero. Read/write use the existing bounded 32 MiB platform sidecar limit.
3. **Windows long paths (P2):** ordinary temporary directories produced staged names beyond MAX_PATH. The first real failure was the durability open; after correcting it, the existence/read-back gates exposed the corresponding standard-filesystem boundary. File-only native conversion now covers flush, claims, rename, attributes, size, copy, cleanup, journal streams and recovery scans. Stored UTF-8 source/journal identities and lexical path helpers keep their existing spelling. Short-path, device-path and POSIX behavior are retained.
4. **Clang-Tidy:** the seven reported diagnostics were addressed: retained ownership in the fused executor capture, avoided an extra checked-path copy and JSON proxy bindings, and guarded TaskCenter callbacks without moving the RSS retry off the application event loop.

## Real RED evidence

- Initial `[identity_edge]` invocation matched zero cases: **invalid, excluded from evidence**.
- Normal-TEMP VRT diagnostics: all creation attempts failed before the target assertions; **not** counted as the NaN/size RED.
- `C:\rs31` TEMP, same VRT binary, before the VRT fix: original case passed; NaN and 1000-band cases failed (3 cases, 7 failed assertions). NaN re-warped an unchanged source; the many-band identity could not be persisted.
- Real Python MVP before descriptor compatibility fix: 9 cases, 6 passed / 3 failed; the failed NDVI/Unicode-PROJ/scientific cases reported `Python output was not delivered as an asset`.
- Windows Unicode/>MAX_PATH atomic test: both new/replace sections failed at fsync; after the Win32-open fix, extended tests reproduced false file-existence/size, group-publish rejection and sidecar verify-readback failure. These drive the actual file APIs with independently created extended-path fixtures.

Logs are local under `build-logs/pr-all-20261116/` (ignored): `pr2071-vrt-short-root-red.log`, `pr2071-longpath-red.log`, `*-longpath-full-red.log`, `red-suite/`, and `intermediate-win32-only/`.

## Test fixture corrections

Windows validation also exposed pre-existing fixture assumptions. The fixes preserve the behavioral oracle:

- Real Windows no-delete-sharing lock replaces a directory-readonly bit that does not prevent writes; original stable content and rollback-residue assertions remain.
- Unicode manifest paths enter native filesystem and GDAL APIs with the appropriate UTF-8 conversion.
- Cooperative-cancellation children throw after observing cancellation; RIO-10's successful-return-wins contract remains unchanged and is tested separately.
- Staged parents are compared as physical directories, including Windows 8.3 aliases. Journal fixtures use valid JSON escaping for Windows paths, including intentionally malformed *field types*.
- A mirror lock-reader source pin follows its existing extracted UTF-8 reader instead of the removed local variable spelling.

## Final validation

Code/test commit: `b3b8adf1fbd5f121eb039b00b3a730a1f069a98b`. The following documentation-only commit records the results; no production or test changes followed the reviewed source snapshot.

- Windows MSVC 2022 / Qt 6.8 build: **exit 0**, 25 affected targets, resource guard, `--parallel 2`.
- Serial execution, normal Windows TEMP, Qt offscreen: **25/25 targets exit 0**; **290 discovered cases, 289 executed, 1 explicit opt-in stress skip; 225,717 passed assertions**. No zero-match run is included.
- `test_python_agent_mvp`: **9 cases / 108,794 assertions PASS**, including real bundled Python 3.12.16, fresh Unicode managed environment, isolated PROJ resources, offline NDVI mask asset/provenance delivery, scientific imports, cancellation and rejected partial outputs.
- `test_r31_t01_result_semantics`: **5 / 176 PASS** (including three actual file-producer schema forms and report counterexamples).
- `test_r31_t01_vrt_reuse`: **3 / 2,105 PASS**, normal TEMP (including NaN and 1000 bands).
- Long-path atomic, sidecar and platform suites: **19/340**, **15/90**, **13/96** respectively; source-boundary suite **10/133**, all PASS.
- RIO-10 regression: **3/20 PASS**. Execution-plane suite: **17 executed cases / 885 assertions PASS**, one 100k-task stress case requires `SICNU_EP9_STRESS=1` and was explicitly skipped.
- `git diff --check`: PASS.

Exact local commands: `build-logs/pr-all-20261116/pr2071-final-build.cmd` initializes MSVC and invokes the guarded CMake build; `pr2071-gated-final-run.ps1` runs the 25 binaries serially through `build-logs/run_gui_delivery_tests.ps1`. Environment: `QT_QPA_PLATFORM=offscreen`, keychain disabled, `PROJ_NETWORK=OFF`, native GDAL/PROJ data from the configured vcpkg prefix, bundled interpreter selected explicitly, `PYTHONHOME`/`PYTHONPATH` cleared, no TEMP override. Summary and per-binary logs: `pr2071-test-summary.json`, `pr2071-aggregate.json`, `pr2071-filesystem-final-build.log`, `<target>.log` in the same ignored log directory.

Targets, in execution order:

```text
test_atomic_fs_caller_contract test_platform_durable_sidecar
test_platform_portability test_portability_source_contract
test_r31_t01_vrt_reuse test_r31_t01_vrt_nodata
test_r30_t03_generations test_r30_t03_vrt_prepare_control
test_r31_t01_result_semantics test_r31_t01_workflow_resume
test_r31_t01_model_identity test_r31_t01_sampling_admission
test_r31_t01_output_recovery test_provider_http test_output_committer
test_task_center test_tool_call_dispatcher test_io_operators
test_io_finalize_manifest test_data_manager test_r30_t02_working_set
test_model_runtime test_execution_plane_9 test_r27_t02_job_engine
test_python_agent_mvp
```

## Remote CI and retained limits

The original remote runs did not pass: Fast Linux stopped on Clang-Tidy; the full-integration job exhausted disk during linking; Windows installation completed but the installed CLI help check failed without a diagnostic exit code. Python packaging passed on Linux and Windows. These are historical results, not a claim about the refreshed PR head. Merge uses the user's explicit local-validation criterion; the refreshed remote status must be stated accurately.

The original residual matrix, disabled ORT cancellation path, concurrent-digest oracle limitation and undeveloped work packages remain disclosed. This review does not claim live GUI/rendering acceptance or full long-path coverage of unrelated modules. Long-path recovery tests cover actual journal read/scan and cleanup; existing normal-path recovery tests retain byte-level backup rollback assertions.
