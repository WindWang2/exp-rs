# R1 Independent Review — r23-B (data model runtime)

- **Reviewer**: R1 (independent; no authorship of this work)
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r23-B-20261026T164130Z` (branch `agent/r23-b-data-model-runtime-20261026T164130Z`, base `origin/master` ad98a7a704)
- **Diff reviewed**: `git diff origin/master...HEAD` — every hunk, plus surrounding production code (callers, contracts, vocabularies). Note: the task brief said "6 commits"; the branch carries **5 commits** (4 code + 1 docs) — cosmetic mismatch with the brief, not a defect.
- **Date**: 2026-10-27

## Verdict

**APPROVE** — no P0/P1/P2 defects found. Two P3 documentation/consistency nits (below) do not block merge. All claims in the track's planning docs were independently reproduced, including both RED gates.

## Findings

### P3-1 — Stale comment describes the retired compensation leg
- **File**: `src/experiment/reproduction_bundle_import.cpp:333-337`
- **Problem**: The comment above `run.setExperimentId(...)` still says: "The A5 protection installed below is the COMPENSATION leg: the metrics record is written before the run by design, and a failed run install deletes it again — no orphan metric row survives." Both claims are now false: metrics are *validated* at 6b and installed *in the same transaction* as the run; `deleteMetricRecord` is no longer called by the importer.
- **Failure scenario**: A future maintainer reading step 6 believes metrics are written first and compensated on failure; they "restore" the old sequence or add a compensation leg, re-opening the crash window this change closed.
- **Suggested fix**: Replace the comment with one line pointing at 6b ("metrics ride the run's transaction; see 6b / ExperimentStore::upsertRunWithMetric"). Pure comment edit; no code change.

### P3-2 — `upsertRunWithMetric()` public wrapper omits the sibling's lifecycle guard
- **File**: `src/experiment/experiment_store.cpp:955` vs `:932` (`upsertRun`)
- **Problem**: `upsertRun()` takes `QReadLocker lifecycleGuard(&m_lifecycle)` in the *public* wrapper before the trace check; the new `upsertRunWithMetric()` does not, and relies solely on `upsertRunWithMetricImpl()`'s guard. As written this is **safe** (the wrapper touches no store state — only `run.runId()/experimentId()` and the trace clock — and Impl holds the guard across all store access; it also avoids the sibling's recursive read-lock). But the asymmetry invites drift if someone later adds store access to the wrapper.
- **Failure scenario**: None today; latent only.
- **Suggested fix**: Optional — either add the guard for symmetry with `upsertRun`, or leave a one-line comment saying the guard intentionally lives in Impl only. No behavioral change either way.

### P3-3 — Duplicate-header tie-break semantics changed (harmless, worth knowing)
- **File**: `src/geospatial/remote/probe_remote.cpp:112-135` (header reads)
- **Problem**: `HttpFetchResult::headers` keeps the **first** occurrence of a duplicated response header ("First occurrence wins", `http_fetch.h`), while the old `papszHeaders` loop let the **last** occurrence win for `content-type`. Observable only against an origin emitting contradictory duplicate `content-type` headers — pathological and unspecified by RFC either way. `content-range`/`content-length` precedence and the `accept-ranges` override protection are correct (and the new code *fixes* an order-dependent bug in the old loop: a `content-range` answer followed by `Accept-Ranges: none` used to clear `acceptsRanges` to false despite the strongest proof; now guarded by `!rangeRewritten`).
- **Suggested fix**: None required; note for the record.

### P3-4 — TEST_RESULTS.md cites test number #1151 for the stub gate
- **File**: `.planning/zcode-r23-B/TEST_RESULTS.md` (W1 table)
- **Problem**: In the current build the stub gate is ctest **#1165** (numbering is configuration-dependent). Cosmetic doc drift only; the test name is correct and it passes.
- **Suggested fix**: Drop the number or mark it build-specific.

### Non-findings (verified clean)

1. **Provider restore (ca1413a4ef)** — `selectDevice`/`manifestPinsCpu`/`joinDevices` are text-identical in logic to 9b42bc93f7; the deviation (keeping the R15-C `mArtifact` member instead of renaming to `m_artifactPath`) is the correct merge resolution: master's R15-C consumers (`memoryEstimate`, `.bin` sibling lookup, `deviceName`) read `mArtifact`. Master's TUs are genuinely broken (independently reproduced, see evidence table) and the branch TUs compile clean. The `mutable std::mutex m_healthMutex` fix is required by the const `health()` override. TRT `artifactPath()` now returns the member the constructor actually initializes (`mArtifact`); master returned an undeclared `m_artifactPath`.
2. **Derived-output gate (09f5f67e75)** — Placement correct: inside the single-input `run()` derived-mode block only. `runMultiInput` refuses derived formats outright (tile_inference_engine.cpp:3009); the detection lane (`DetectionTileEngine`) performs no derived collapse; scene classification is not a derived-format consumer; ensemble members are forced `RasterOutputMode::Probability` (model_ensemble.cpp:523) so the gate cannot fire under the combination, which owns its own product gates. All request lanes (`rs_model_task_operators`, `model_execution_service`) funnel into `engine.run()`. Role vocabulary matches `model_catalog.h:153` exactly (segmentation/classification pass; detection/embedding/uncertainty/auxiliary refused); confidence vocabulary matches `:159` (distance always refused; logit refused except Labels, where argmax is order-preserving). Legacy flat manifests (`heads.empty()`) keep historical permissiveness — no false rejections; `test_model_runtime` (110440 assertions) fully green confirms no collateral. The catalog enforces `heads[0].name == tensor_names[0]` (model_catalog.cpp:850), so `heads.front()` is the head the engine consumes after the `tensorNames.size() > 1` refusal.
3. **`upsertRunWithMetric` (2252536b)** — Lock/lifecycle discipline identical to siblings (guard in Impl, `m_impl` check, `m_impl->mutex`, `isReadOnly`, field checks, `protocol.validate()`, hash gate — all before `begin()`). Every path after `begin()` ends in rollback or commit: run-write failure, metric failure, fault point, commit failure. `saveMetricRecord` behavior preserved **exactly**: same diagnostic codes on all five failure modes (`store_query_failed` for SELECT/INSERT prepare, `experiment.conflict`, `store_write_failed` for INSERT step/commit), identical no-op-success for content-identical re-saves (rollback the empty txn, report success), hash gate untouched. No double-locked or missing-locked path. The same-run guard (`stored.runId != run.runId()`) is a sound addition — a foreign-keyed metric would escape the transaction's atomicity promise.
4. **Bundle importer** — parse/secret gates still refuse before any store mutation; the hash gate now fires inside `upsertRunWithMetricImpl` *before* `begin()` (still zero mutation). Warning text `"run install failed: %1"` retained (the only test dependency, `test_experiment_consistency_r4.cpp:487`, greps exactly this). Empty `metrics.json` (`{}`) → plain `upsertRun`, missing/unparsable `metrics.json` → refuse: both unchanged. `deleteMetricRecord` has **no remaining production callers** (only the new test uses it, as documented manual-cleanup API).
5. **probe_remote (ef154e3ae8)** — Transport switch verified: `httpFetchStatus` installs the private-network per-hop guard + DNS pinning automatically under the process policy (`http_fetch.cpp:906-942`); whole-request timeout budget → typed `GeoError(Timeout)` on both transports (matching the old "timed out" text sniff); retries honored (CPL `RETRIES` / guarded `attemptsLeft`, `http_fetch.cpp:771`); `maxResponseBytes` maps the old `MAX_FILE_SIZE` double-bound. Status mapping (404/410 → NotFound before >=400 → NetworkError; reachability; empty-declared-length refusal) is byte-identical to master. First-hop fail-closed check retained. The `effectiveUrl` comment is honest: `displayUrl` is the *requested* (redacted) URL, not the post-redirect target, so the field stays empty.
6. **Tests/registrations** — `tests/CMakeLists.txt` strictly append-only (master tail context "end R22-04 registration" preserved, hunk `@@ -16779,3 +16779,75 @@` adds only); link surfaces match siblings (`test_model_runtime`-style for the roles test, `test_experiment_consistency_r4`-style light Qt+SQLite for the txn test); `.gitignore` entry mirrors the r22-infra pattern; stub headers are declaration-minimal and cannot mask real defects (`-fsyntax-only`, no runtime). One registration nit, not a defect: the derived-roles cases' *names* don't contain `r23_b`, so `ctest -R "r23_b"` does not select them (they run under their own names / `[r23b]` tag); the three cases pass when run directly.
7. **Hygiene/ownership** — `git diff origin/master...HEAD --check` clean; zero conflict markers in the diff; every changed file is inside the B whitelist (src/operators/runtime/**, src/experiment/**, src/geospatial/remote/{probe_remote.cpp, process_network_policy.h comment}, tests/** append, .planning/zcode-r23-B/**, .gitignore append).

## Test-run evidence (all executed by R1 in build-r23-B, QT_QPA_PLATFORM=offscreen)

| Check | Command | Expected | Actual | Verdict |
|---|---|---|---|---|
| r23-B ctest set | `ctest -R "r23_b" --parallel 1` | all pass | 4/4 passed (stub gate #1165 3.58s + 3 txn cases 0.03s each) | PASS |
| derived-roles cases | `./tests/test_r23_b_derived_output_roles` | 27/3 | All passed (27 assertions / 3 cases) | PASS |
| model runtime family | `./tests/test_model_runtime` | 110440/26 | 110440 assertions / 26 cases | PASS |
| experiment family | `./tests/test_experiment_consistency_r4` | 298/11 | 298 / 11 | PASS |
| r21 e2e | `./tests/test_r21_b_end_to_end` | 4142/2 | 4142 / 2 | PASS |
| probe contract family | `./test_io_remote_range` | 25/6 | 25 / 6 | PASS |
| offline family | `./tests/test_offline_mode` | 34/10 | 34 / 10 | PASS |
| **W2 RED (gate load-bearing)** | disable `if (!m_model.output.heads.empty())` block (tile_inference_engine.cpp:1138) → rebuild target → run | 6 failures | 2/3 cases failed, **6/27 assertions failed**, every illegal combination fell through to "failed to open input raster" | RED confirmed |
| W2 GREEN restore | `git checkout -- src/operators/runtime/tile_inference_engine.cpp` → rebuild → run | green | All passed (27/3); working tree clean after | GREEN restored |
| **W1 RED (independent)** | master's two provider TUs, `-fsyntax-only` against `tests/fixtures/provider_stubs` | broken | OV: `m_artifactPath` not declared (l.50), `selectDevice` not declared (l.60), const lock of non-mutable mutex (l.128); TRT: `m_artifactPath` not declared (l.148) | RED confirmed |
| W1 GREEN | branch TUs, same command | clean | 0 errors both | PASS |
| diff hygiene | `git diff --check`; grep `<<<<<<<\|=======\|>>>>>>>` | clean | clean / no markers | PASS |
| ownership | `--name-only` vs B whitelist | whitelisted only | all 33 files whitelisted | PASS |

## Summary

The four production changes do what they claim, with semantics verified against their referenced authorities (9b42bc93f7 for the providers, model_catalog.h vocabularies for the gate, sibling store methods for the transaction, http_fetch.h for the transport). The test additions are genuinely load-bearing (both RED states reproduced independently by this reviewer). The only follow-ups are comment/doc-level (P3-1 above all).
