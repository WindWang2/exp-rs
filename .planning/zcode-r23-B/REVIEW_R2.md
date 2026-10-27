# REVIEW R2 — independent adversarial re-derivation (r23-B)

Reviewer: R2 (second, independent). Derived without reading REVIEW_R1.md.
Worktree: `exp-rs-r23-B-20261026T164130Z`, branch `agent/r23-b-data-model-runtime-20261026T164130Z`,
base `origin/master` `ad98a7a704`, HEAD `679feb773e` (reviewed HEAD as checked out; nothing newer).

## Verdict

**APPROVE-WITH-FIXES**

No P0/P1. Two P2s (one introduced by this branch — the ctest registration gap that silently
skips the W2 tests under the track's own `ctest -R r23_b` entry point; one pre-existing
adjacent gap the W2 fix did not close — the ensemble combiner's derived collapse has no head
gate). Everything else I attempted to break held up: the single transaction is complete on
every return path, the #1173 conflict code survives genuine two-connection races through both
the old and new API (40/40 pairs, zero busy-degraded outcomes), the importer's refusal set is
equivalent to master's, and probe_remote's observable contract is preserved (with one real
improvement) on the guarded transport.

---

## Findings

### P2-1 (introduced by this branch): W2 tests are invisible to `ctest -R r23_b`

`tests/CMakeLists.txt:16831` registers the derived-output-roles suite with a bare
`sicnu_discover_tests(test_r23_b_derived_output_roles)` — no `TEST_PREFIX`, unlike the
transaction block at `tests/CMakeLists.txt:16852` which passes
`TEST_PREFIX test_r23_b_run_metric_transaction::`. The generated test names are therefore
`derived output.format refuses non-class head roles` etc., which do not match `r23_b`.

Failure scenario: anyone re-running the documented evidence command
(`ctest -R r23_b`, QT_QPA_PLATFORM=offscreen, from build-r23-B) after touching
`tile_inference_engine.cpp` sees "100% tests passed, 4 tests" — the three W2 cases silently
do not run. The gate could be reverted and that command stays green. (Verified:
`ctest -N -R r23_b` lists 4 tests; `ctest -R "derived output.format"` is required to run the
W2 cases — they pass 3/3 there.)

Fix: `sicnu_discover_tests(test_r23_b_derived_output_roles TEST_PREFIX test_r23_b_derived_output_roles::)`.
Note TEST_RESULTS.md ran the W2 suite by direct binary invocation, so its recorded results are
not wrong — only the regex entry point is broken.

### P2-2 (pre-existing gap adjacent to the W2 fix): the ensemble combiner's derived collapse has no head-contract gate

`src/operators/runtime/model_ensemble.cpp:1044` takes `mode = request.outputMode` and the
collapse switch at `model_ensemble.cpp:1511-1530` (Labels=argmax, Confidence=top-1 value,
Mask=threshold) applies it to the mean of member probability stacks. Member runs are forced to
`RasterOutputMode::Probability` (`model_ensemble.cpp:523`), so the new gate in
`tile_inference_engine.cpp::run` never fires for a member, and `model_ensemble.cpp` contains
zero references to head role/confidence (grep-verified).

Failure scenario: an ensemble manifest whose raster members declare typed heads
(role=embedding, or confidence=distance/logit) plus an operator-level
`rs:segment format=mask|labels|confidence` (raster derived mode is only reachable through
`request.outputMode` for ensembles — the ensemble path never consults the manifest's
output.format). Members write raw stacks, the combiner means them and thresholds across
semantics — exactly the fabricated product the new gate's own citation forbids
("consumers must not threshold across semantics", model_catalog.h:160-163). This is reachable
today (members' head contracts are unconstrained at ensemble parse/build), pre-existing on
master, and not introduced by this diff — but the fix's stated contract is violated by a
sibling path it did not cover.

Fix: at ensemble run (or build), refuse derived request modes when any raster member's
head-0 role is not segmentation/classification or its confidence is distance, or logit with
mode != Labels — the same predicate as the single-model gate.

### P3-1 (pre-existing, restored faithfully; comment overstates): explicit `--device cpu` can be repaired to the Intel GPU plugin

`openvino_provider.cpp:247-250` claims "An operator-side --device override never reaches the
factory … so it is honored through the verdict, never re-derived here." The verdict does reach
the factory — but only as `effective.runtime.gpu` (`model_runtime.cpp:1153-1155` sets gpu and
resolvedCudaIndex; `runtime.device` keeps the MANIFEST token). The repair at
`openvino_provider.cpp:224-226` fires on `!gpu && !hw.cudaAvailable && model.gpu &&
!manifestPinsCpu(model)` — with manifest gpu=true, manifest device "auto", operator override
`--device cpu`, host without CUDA but with an OpenVINO GPU plugin, selectDevice flips gpu=true
and compiles on "GPU" despite the operator's explicit CPU request. `manifestPinsCpu` cannot see
the override.

Failure scenario: deterministic mis-execution device for an explicit user pin on exactly the
hosts #1576 targets (no CUDA, Intel iGPU). Byte-identical to the original 9b42bc93f7 fix
(verified by `git show 9b42bc93f7:...`), so this is inherited, not new — but the NEW comment
asserts a guarantee the repair branch does not provide. Fix: propagate the effective request
kind (or the effective device token) into the factory's ModelInfo so `manifestPinsCpu` tests
the actual request, not the manifest string.

### P3-2 (test-coverage gap): nothing pins the importer to the combined API

`reproduction_bundle_import.cpp:429-433` dispatches to `upsertRunWithMetric` when a metric is
present. No test observes that dispatch: reverting the importer alone to
`saveMetricRecord → upsertRun` (keeping the new API in the library) passes the entire suite —
`test_experiment_consistency_r4`'s A5 trigger case asserts only the end-state no-orphan
invariant, which master's compensation leg also satisfies, and the three new transaction
cases exercise the store API directly, not the importer. The crash-window improvement in the
importer itself is therefore unpinned. Fix: a test that fails when the importer uses the
two-call sequence (e.g. arm the `experiment_store.commit` fault point for the second commit
and assert the import's atomic outcome, or assert on a test-only seam recording the dispatch).

### P3-3 (informational, no regression): probe treats a size-guard-truncated body as fetched

`probe_remote.cpp:156` — `fetched = !result.body.empty()`; `sizeGuardHit`/`truncated` are not
consulted. This matches the old `nDataLen > 0` semantics under CPL MAX_FILE_SIZE (partial
bytes preserved, `http_fetch.cpp:14-16` notes), and reachability from a cut answer is
legitimate, so behavior is unchanged vs master. If the probe ever needs to distinguish "usable
answer" from "cut answer", `fetched && !result.truncated` is the tightening point. No action
required.

### P3-4 (informational, benign): double rollback on commit failure

`Impl::commit` (`experiment_store.cpp:120-128`) already executes ROLLBACK when COMMIT fails;
callers (`upsertRunWithMetricImpl` and refactored `saveMetricRecord`, plus pre-existing
`upsertRunImpl`) then call `m_impl->rollback()` again — a second ROLLBACK with no active
transaction returns SQLITE_ERROR, which is swallowed (errorOut=nullptr) and the failure result
is still returned correctly. This is the documented convention ("a failed COMMIT can leave the
transaction active — roll back explicitly", `experiment_store.cpp:1096-1099`). Enumerated every
begin-path in both functions: each path either commits or rolls back exactly once (modulo this
benign idempotent second rollback); no path begins a transaction and returns without one.

---

## Adversarial angles — what I tried and what held

### Concurrency (live probe, not just reading)

Throwaway C++ probe at `/tmp/r23b_conc_probe.cpp` (not in the repo), compiled against the
built libs from build-r23-B (`libsicnu_experiment.a` + dataset/contracts/data/platform +
`libsicnu_runtime.so` + sqlite3/Qt6::Core/jsoncpp), driving two `ExperimentStore` instances on
the SAME db file (two sqlite connections, WAL, busy_timeout 5000) plus a same-instance
two-thread case. Probe journey itself surfaced two store contracts I initially got wrong and
verified are pre-existing master behavior (not defects): `from == to` status re-upserts are
illegal (`isValidRunTransition`, experiment_types.cpp:82-86), and terminal statuses need a
finished stamp to round-trip (experiment_types.cpp:576-581). After correcting the probe to
legal transitions (Created→Running vs Created→Failed with finish stamp):

- A1 same-content race, two connections: both succeed; exactly one metric row; run present.
- A2 different-content race, two connections: exactly one winner; loser is
  `experiment.conflict` ("metrics for run run-diff exist with different content"); the
  refused 0.9 content never lands; the winner's status advance lands.
- B old-API race (`saveMetricRecord` only, two connections): loser is `experiment.conflict` —
  the #1173 doctrine holds through the refactored single-record path.
- C same-instance two threads: per-Impl mutex serializes; no deadlock; one metric row.
- D 40 pairs of genuine two-connection races: ok=40, conflict=40, bad_transition=0,
  **other=0** — no busy-timeout miss ever degraded a conflict into `store_write_failed`
  ("cannot begin transaction").

Derived positive evidence (race the author probably did not run): on master, two concurrent
identical imports can leave the WINNER's metric deleted — the loser's `saveMetricRecord`
returns no-op success (same content), `metricsInstalled=true`, its `upsertRun` then fails
(`created → created` bad_transition), and the compensation leg `deleteMetricRecord(runId)`
removes the row the winner just committed. HEAD's single transaction makes that impossible
(the loser's failure rolls back only its own empty transaction; probe A1/D confirm the
winner's rows survive). The fix is stronger than its stated crash-window claim.

`m_lifecycle` is `QReadWriteLock{Recursive}` (experiment_store.h:280), so the public→Impl
double read-lock in `upsertRunWithMetric` is safe and mirrors `upsertRun`.

### Transaction completeness

Every return path of `upsertRunWithMetricImpl` and the refactored `saveMetricRecord`
enumerated (guards before BEGIN: closed/read-only/invalid/protocol/hash/runId-mismatch;
after BEGIN: run-write fail → rollback; metric-write fail → rollback with the original
diagnostic, conflict code preserved; alreadySameContent → rollback + success (single-record
API) / commit run re-save (combined API — correct, the metric row already exists with
identical content); fault point → rollback; commit fail → Impl-internal rollback + benign
second rollback). No begin-without-commit/rollback path; no internal rollback inside
`writeMetricInTxnLocked`/`writeRunInTxnLocked` (caller-owned by design). The combined impl
mirrors `upsertRunImpl`'s structure including the same fault-point name.

### W2 gate bypass sweep

- `engine.run()` manifest-derived mode and `options.outputMode` override flow through the
  same `mode` variable (tile_inference_engine.cpp:1113-1115) — both gated.
- `runMultiInput` refuses ALL derived modes (tile_inference_engine.cpp:3009-3011).
- `classifyScene` has its own pre-existing distance-confidence gate + requires
  output.classes + single head (tile_inference_engine.cpp:2552-2572).
- Detection lane (`DetectionTileEngine`) consumes boxes, not raster modes; the request-level
  detection path doesn't touch outputMode.
- `rs_inference_operator.cpp`: no format/outputMode — probability only.
- `benchmark_runner.cpp`: no outputMode/format references.
- Bypass found: the ensemble combiner (P2-2 above).

### W2 false-rejection sweep

Head vocabulary is closed at manifest parse: unknown roles/confidences are manifest errors
(`ModelHeadContract::validate`, model_catalog.cpp:1512-1524), empty confidence defaults to
"probability" at parse (model_catalog.cpp:323-325), so the gate's allowlist
(segmentation/classification; probability-or-logit+Labels) cannot strand a legal-but-unforeseen
string — nothing legal exists outside it. Legacy flat manifests (no heads[]) skip the gate;
`logit + labels` stays legal (argmax is order-preserving); Probability mode (the
regression/embedding default) is untouched. `role=detection` requires the output.detection
contract at parse, and derived collapses of detection heads were always garbage — refusing is
correct, and the new tests pin all three families.

### Importer equivalence vs master

Gate order up to and including the duplicate-ingest guard is unchanged; `alreadyPresent`
returns before any store mutation (re-verified in the current file). The metric parse and
secret-scan refusals are unchanged and still precede any write. Every master gate survives
inside `upsertRunWithMetric` (protocol validate, content-hash gate, metric conflict, run
transition/identity/round-trip/existence, store errors). No bundle master refuses now imports
and none that imported now refuses — the refusal SET is equal. Two observable deltas, neither
a defect: (a) within the install step, run gates now run before metric gates (master: metric
first) — both refuse; (b) metric-side refusals now surface as
"run install failed: <experiment.metrics_hash_mismatch …>" instead of
"metric record install failed: …" — no test or consumer matches the old text
(grep-verified; the surviving `test_experiment_consistency_r4` case asserts "run install
failed", which still matches). The metric is re-keyed to the new run id before the combined
call (reproduction_bundle_import.cpp:388), so the new runId-match gate can never falsely fire
from the importer.

### probe_remote vs http_fetch

`httpFetchStatus` under the process policy auto-installs the private-network per-hop guard
(`effectiveOptions`, http_fetch.cpp:906-917) — the probe's claim is accurate, and the probe's
own fail-closed first-hop check is retained. Status-0 tolerance, the
404/410-before-≥400 mapping, `declaredLength` precedence (Content-Range over plain
Content-Length), and "a truncated 200 is not range support" are all preserved
(probe_remote.cpp:159-187). `fetched` (`nDataLen > 0` → `!body.empty()`) and size-guard
truncation semantics are equivalent on both transports (both honor range + byte budget;
guarded transport keeps partial bytes, http_fetch.cpp:382). Improvement found: header
precedence — the old single loop could let a later `Accept-Ranges: none` line override a
Content-Range proof of range support depending on header order; the new code resolves
Content-Range first. `probeBytes` is never 0 (min 1), so the `maxResponseBytes == 0 → 8 MiB
default` quirk of HttpFetchOptions cannot change probe behavior. process_network_policy.h
diff is comment-only.

### Provider restores

- master's `tensorrt_provider.cpp:148` returns `m_artifactPath`, a member that does not exist
  on master (only `mArtifact` exists) — master's TU cannot compile with TRT enabled; HEAD's
  one-liner is the restore. Verified.
- `selectDevice`/`manifestPinsCpu`/`joinDevices` restored byte-identical to 9b42bc93f7
  (git-show diff), correctly wired at openvino_provider.cpp:56-61, with the artifact identity
  (`mArtifact = path`, line 66), `mutable m_healthMutex` (line 278) fixing the const
  `health()` probe, and honest `memoryEstimate` from the real artifact. Semantics of the
  repair verified against the resolution pipeline (see P3-1 for the one overstatement).
- The stub-compile gate is a real discriminator: it fails on the master TU (undeclared
  members/dangling call) and passes on HEAD (verified passing in ctest). Its limits are
  inherent: syntax-only, so device-selection SEMANTICS are untested (no functional test can
  exist without an Intel GPU plugin — acceptable residual).

### Test quality (would they catch reverts?)

- `test_r23_b_derived_output_roles.cpp`: discriminating by construction — the
  nonexistent-input oracle means a gate revert drops every gated case into
  "failed to open input raster" and the substring assertions fail; the permissive cases pin
  no false rejections. Would catch a W2 revert (if run — see P2-1).
- `test_r23_b_run_metric_transaction.cpp`: the sqlite-trigger case fails if the metric write
  escapes the transaction; the conflict case pins typed refusal codes AND that a refused
  combined call changes nothing; the fork A/B case documents the pre-fix window. Would catch
  an atomicity revert of the store API. Would NOT catch an importer-only revert (P3-2).
- `test_r23_b_provider_stub_compile.sh`: catches re-breaking the provider TUs (that is its
  job); headers in tests/fixtures/provider_stubs cover the exact API surface used.

### Hygiene

- `git diff origin/master...HEAD --check`: clean (exit 0).
- Conflict-marker scan (`^(<{7}|={7}|>{7})`) across all 33 changed files: zero hits.
- Whitelist audit of the full stat: production changes confined to
  src/operators/runtime/{openvino_provider,tensorrt_provider,tile_inference_engine}.cpp,
  src/experiment/{experiment_store.h,experiment_store.cpp,reproduction_bundle_import.cpp},
  src/geospatial/remote/{probe_remote.cpp,process_network_policy.h (comment-only)};
  tests/CMakeLists.txt append-only (single hunk at the file tail); three new test files plus
  stub fixtures; .planning/zcode-r23-B/ markdown; .gitignore append. Nothing outside the
  whitelist.

---

## Evidence table (all fresh, run by R2; QT_QPA_PLATFORM=offscreen, cwd build-r23-B)

| Command | Result | Exit |
|---|---|---|
| `ctest -R r23_b` | 4/4 passed (transaction ×3, stub compile) — W2 cases not matched (P2-1) | 0 |
| `ctest -R "derived output.format"` | 3/3 passed | 0 |
| `./tests/test_model_runtime` | 110,440 assertions / 26 cases, all passed | 0 |
| `./tests/test_experiment_consistency_r4` | 298 / 11, all passed | 0 |
| `./tests/test_r21_b_end_to_end` | 4,142 / 2, all passed | 0 |
| `./test_io_remote_range` | 25 / 6, all passed | 0 |
| `./tests/test_offline_mode` | 34 / 10, all passed | 0 |
| `./tests/test_dataset_core` | 512 / 19, all passed (first invocation 514/19 — one env-sensitive pair; two immediate reruns both deterministically 512/19, matching TEST_RESULTS.md) | 0 |
| `./tests/test_r22_data_security_consistency` | 94 / 9, all passed | 0 |
| `./test_io_vector_contract` | 100,339 / 14, all passed | 0 |
| `/tmp/r23b_conc_probe` (two stores × two connections + 40-pair stress + same-instance threads) | ALL PASS (A1/A2/B/C/D as described above) | 0 |
| `git diff origin/master...HEAD --check` | clean | 0 |

TEST_RESULTS.md claims reconcile with the fresh runs line-for-line (modulo the dataset_core
514-vs-512 first-run noise noted above).

## Required before merge (P2s)

1. Add `TEST_PREFIX test_r23_b_derived_output_roles::` at tests/CMakeLists.txt:16831 (one line).
2. Head-contract gate for the ensemble derived collapse (model_ensemble.cpp:1044/1511-1530) —
   or an explicit, tested decision record that ensembles are out of scope for the head
   contract, so the gap is at least named in the code that cites the contract.

---

## R2 confirmation (post-fix)

Confirmation reviewer (independent of both prior rounds). Range reviewed:
`679feb773e..d0e25e5099` (HEAD `d0e25e5099`, "close R2 findings"). Every hunk of the
fix diff read; all evidence below run fresh by this reviewer (QT_QPA_PLATFORM=offscreen,
cwd build-r23-B, binaries verified current with HEAD — libs and test binaries relinked
23:02, after the newest source change 23:02:11; `strings` confirms the gate/test symbols
in the built artifacts).

### Verdict: **NEW FINDINGS** (all six original findings closed; the P2-2 fix commit
### introduced one new P2)

Original findings:

- **P2-1 CLOSED.** `tests/CMakeLists.txt:16831` now passes
  `TEST_PREFIX test_r23_b_derived_output_roles::`. Fresh `ctest -R r23_b
  --output-on-failure --parallel 1`: **7/7 passed**, including all three
  `test_r23_b_derived_output_roles::` cases (tests #1127-1129) plus the three
  transaction cases and the provider stub compile. The documented entry point no longer
  silently skips the W2 suite.
- **P2-2 CLOSED as specified — but over-broadly (see N1).** The gate logic now lives in
  exactly one predicate, `ensureDerivedModeMatchesHeadContract`
  (tile_inference_engine.cpp:1048, declared tile_inference_engine.h:106);
  `TileInferenceEngine::run` delegates to it (tile_inference_engine.cpp:1169) and
  `runEnsembleInference` calls it (model_ensemble.cpp:1063) BEFORE any member forward
  (`runMembersBounded` at model_ensemble.cpp:1125; members are forced to Probability at
  :523, so only the ensemble seam can see the derived request). Vocabulary matches
  `model_catalog.h` exactly: roles closed at parse by `ModelHeadContract::validate`
  (model_catalog.cpp:1511-1515: segmentation/classification/detection/embedding/
  uncertainty/auxiliary), so the predicate's segmentation|classification allowlist is the
  exact complement of the refused set — nothing legal escapes and nothing illegal slips
  through; confidences probability/logit/distance with logit+Labels allowed
  (order-preserving argmax). Legacy no-heads ensembles stay permissive
  (`heads.empty()` early return). Fresh runs: `./tests/test_model_ensemble` **125
  assertions / 16 cases all passed**; `./tests/test_model_runtime` **110,440 / 26 all
  passed**; `./tests/test_r23_b_derived_output_roles` **27 / 3 all passed** — no
  regression from the predicate extraction. New ensemble cases (embedding refused with
  no product written; segmentation head permissive) pin the seam.
- **P3-1 CLOSED.** `openvino_provider.cpp:247-253` no longer claims the override is
  "honored through the verdict"; it now states the repair "can still serve an explicit
  cpu override on OpenVINO's GPU plugin (faithful to 9b42bc93f7; the override-vs-repair
  seam is a known follow-up, recorded by the r23-B R2 review)". Honest; recorded as R13.
- **P3-2 CLOSED (recorded).** RISK_REGISTER R14 records the unpinned importer dispatch
  with rationale (behavioral discrimination would need in-process death injection;
  cost exceeds value).
- **P3-3 CLOSED (recorded).** R15, informational, master-equivalent semantics.
- **P3-4 CLOSED (recorded).** R16, benign idempotent second rollback per repo convention.

New finding from the fix diff:

### N1 (P2, introduced by d0e25e5099): the ensemble gate is unconditional — typed non-class heads now refuse even the DEFAULT Probability request

`runEnsembleInference` calls the shared predicate at model_ensemble.cpp:1063 OUTSIDE any
`mode != Probability` / `derived` guard, and the predicate refuses
`role ∉ {segmentation, classification}` (and `confidence == "distance"`, or `"logit"`
with `mode != Labels`) **regardless of mode**. The single-model call site does not have
this problem — it sits inside `if ( mode != RasterOutputMode::Probability )`
(tile_inference_engine.cpp:1162-1169), which is why "Probability mode (the
regression/embedding default) is untouched" held for the engine. The ensemble seam now
violates that same invariant.

Failure scenario (empirically confirmed): a raster ensemble manifest declaring
`output.heads[0]` — typed heads are pre-existing Platform 7.0 vocabulary
(model_catalog.h:146-163; this branch's diff to model_catalog is empty, and the fix's own
test registers exactly this manifest shape) — with role `embedding` (or detection/
uncertainty/auxiliary, or confidence `distance`, or `logit`) and a raster request with
the DEFAULT `outputMode = Probability` (model_execution_service.h:50) now throws:

  `derived output.format collapses head 0's class planes, but head 'classes' of model
  'ens-probe-emb' declares role 'embedding' — declare a segmentation/classification head
  or write the probability stack and derive from it afterwards`

— for a request that involves NO derived output.format at all, with a message that
misdescribes the request. Probe (throwaway TU = tests/test_model_ensemble.cpp + one
appended case, compiled/linked against the HEAD build libs, not in the repo): the same
manifest+request ran at `679feb773e` (model_ensemble.cpp had zero head references
pre-fix — grep), and the identical manifest with role=segmentation runs green at HEAD
(the fix's own permissive section proves the plumbing); only role=embedding + default
Probability refuses, and no output is written. Blast radius: ANY raster ensemble whose
manifest declares a typed head that is not a clean segmentation/classification
probability head can no longer run AT ALL — not even weighted_mean Probability (publish
the mean stack), not even weighted_vote (a `logit`-headed classification ensemble —
pre-softmax outputs are a normal shape — is refused in its default request). This is
exactly the class the track's own RISK_REGISTER R3 names ("W2 门过紧拒绝合法清单"),
and it is a regression vs master on a pre-existing legal path.

Fix (minimal, either form): early-return in the predicate when
`mode == RasterOutputMode::Probability` (after the `heads.empty()` check), or guard the
ensemble call site with `mode != RasterOutputMode::Probability` (mirroring the engine).
Add the missing permissive ensemble case (typed non-class head + default request) to pin
it — the current test only pins Labels-mode refusal and segmentation+Labels permissiveness.

Hygiene re-verified on the fix range: `git diff 679feb773e..d0e25e5099 --check` clean;
zero conflict markers added; whitelist holds (only src/operators/runtime/*, tests/*,
.planning/zcode-r23-B/*); no test deleted or weakened (tests diff is append-only plus
the one-line TEST_PREFIX strengthening); the predicate's refusal messages gained
`of model '<name>'` but the W2 assertions are ContainsSubstring("declares role '...'" /
"declares confidence '...'") and still match (27/3 green). Minor, not a finding: the
ensemble gate fires after `acquireEnsembleSessions`, so sessions (VRAM) are reserved
before the refusal — the same ordering as the sibling static refusals (weighted_vote
mask/confidence, variance band, maskThreshold), i.e. the pre-existing pattern, not a
regression.
