# Completion 13/15 — plugin/model runtime production gaps: goal + oracle ledger

Branch: `completion/model-plugin-production-gaps`, worktree
`../exp-rs-completion-model-plugin-prod-gaps`, cut from `origin/master` =
`e4904cd3c568e1e730396237ec7034dc9215b272` (re-verified 2026-09-23 at execution: identical,
open PRs = 0, open issues = 0).

Build: `/home/kevin/toolchain/cmake-dist/bin/cmake -S . -B build-pmg -G Ninja
-DCMAKE_MAKE_PROGRAM=/home/kevin/pwb-sdks/root/usr/bin/ninja
-DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr -DCMAKE_BUILD_TYPE=Debug` (configured OK).
Builds: `cmake --build build-pmg --target <narrow> --parallel 2`. Tests run with
`LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`.

## Recon conclusions (all verified at source on e4904cd3c)

### G1 single-model detection provenance sidecar — REPRODUCED (main deliverable)
- Single-model detection route `model_execution_service.cpp:294-318`: `DetectionTileEngine::run`
  → `writeDetectionVector` (detection_tile_engine.cpp:643) → payload → return. NO `.prov.json`
  anywhere in the lane; `provenance_verify.cpp:135-144` therefore reports MissingSidecar
  deterministically for every single-model detection product.
- Ensemble detection lane (model_ensemble.cpp:844-888) already has the full contract:
  `DetectionPublishGuard` (:162, parks previous product + shapefile sidecars + prov sidecar
  under `.ensemble-prev~`), `publishSidecar` (:90, staged write + rename + fault point
  `ensemble.publish_sidecar`), `buildEnsembleProvenance` (:259) + fusion block.
- Verifier contract (provenance_verify.cpp): schema `exp-rs-prov/1`; `output.format=="vector"`
  short-circuits the grid check (:35-39); model.identity_tag/content_digest checked when
  expectation set; staleness requires sidecar mtime >= product mtime (publish order: product
  first, sidecar last).
- Fix shape: move `publishSidecar` + `DetectionPublishGuard` into a new shared authority
  `src/operators/runtime/model_publish.{h,cpp}` (single truth source; ensemble lane keeps
  using the same code via the header; guard backup suffix becomes a constructor parameter —
  ensemble keeps `.ensemble-prev~`, single-model uses `.det-prev~`); add
  `buildDetectionProvenance` (mirrors `buildProvenanceDocument` tile_inference_engine.cpp:410
  + scene doc's `model.task` + detection thresholds); wire the guard+publish into the
  single-model detection route exactly like the ensemble lane.
- `DetectionTileStats` gains `inputGrids` (GridProvenance filled by the engine from the opened
  ds) so both single AND ensemble detection sidecars can carry an `inputs` block (the ensemble
  detection sidecar prov currently has NO inputs block — buildEnsembleProvenance reads
  `combined.inputGrids` which the detection lane never fills).

### G2 IPluginModelRuntimeV1 forward serialization — REPRODUCED
- Contract: `model_runtime.h:9` ("hands out shared_ptr sessions that are safe to use from any
  thread (implementations serialize forward passes internally)") and `:244` ("possibly
  concurrently — implementations serialize"); `docs/inference/runtime-architecture.md:47`.
- Built-in provider satisfies it: `opencv_dnn_runtime.h:88 std::mutex m_inferMutex`
  ("cv::dnn::Net is not safe for concurrent infer() on the same object").
- The plugin adapter `PluginModelRuntimeAdapter::infer` (plugin_model_runtime_bridge.cpp:77-128)
  does NOT serialize: concurrent holders of one shared session enter plugin `infer`
  concurrently. The out-of-process host path serializes in the worker
  (host_protocol.h:15) — only the in-process bridge violates the contract.
- Design decision (this track): enforce the documented contract in the adapter (the host-side
  session gate between the registry pool and plugin code) — one mutex per adapter, mirroring
  the opencv idiom. Not scattered: it is exactly the seam the contract names.
- Coverage gap: the bridge has ZERO test coverage today (no test file references
  storePluginModelRuntimeFactory/registerPluginModelRuntime; `sicnu_plugins_framework` is
  linked only by app/cli). New narrow test target `test_plugin_model_bridge` (one append block
  in tests/CMakeLists.txt): fake IPluginModelRuntimeV1 with in-flight counter + overlap
  detector; two threads infer on ONE registry-acquired session → max in-flight == 1.

### G3 last-good cross-process liveness — REPRODUCED
- `sweepPluginSnapshots` (plugin_snapshot.cpp:790-794): `last-good-<id>` removed iff id not in
  THIS process's `liveIds`; sweep runs at every `PluginRegistry::configure()`
  (plugin_registry.cpp:172) with liveIds = process-local registry/host-process/in-flight ids.
- Every other grammar is pid-attributed and cross-process-safe: `~staging-<pid>` (:713),
  `~old-<pid>` (:723), `upgrade-<id>-<pid>` (:750, incl. #1157 dead-owner restore).
- Concrete defect: app instance B configures a registry (same temp root) while instance A has
  dev plugin X loaded → B's liveIds lacks X → B deletes A's `last-good-X` mid-flight; A's next
  failed hot reload has no rollback source ("no last-known-good snapshot" diagnostic).
- Fix shape (the deferred "liveness attribution decision", following the dominant grammar):
  last-good becomes `last-good-<id>-<pid>`; sweep rule = same-pid keep / foreign-live-pid keep
  / dead-pid collect (mirrors ~staging-). Legacy `last-good-<id>` (no pid) keeps the current
  liveIds rule. `lastGoodSnapshotPath` (plugin_registry.cpp:431) appends own pid; uninstall
  (1851) keeps removing the own-pid path; foreign-pid snapshots age out via sweep when their
  owner dies. Crash mid-write already safe: staging + marker-last + rename ladder + sweep
  (plugin_snapshot.h:11-18); reload verifies marker + identity gate (plugin_registry.cpp:1150-1192).
- Oracles: (a) live foreign pid's `last-good-<id>-<pid>` survives B's sweep (RED on master:
  removed — master parses the whole suffix as the id); (b) dead pid's snapshot collected;
  (c) legacy `last-good-<id>` liveIds rule unchanged; (d) dev-mode reload rollback still works
  end-to-end with the pid-suffixed path (existing suite must stay green).

### G4 VRAM/reservation pairing — ALREADY CLOSED on current master (dropped per track oracle)
Existing oracles pin every pairing: throwing factory releases the reservation
(test_device_planner.cpp:394); failed acquire (nullptr) releases (model_runtime.cpp:957-969);
#1160 session-held reservation past eviction (test_device_planner.cpp:213); admission +
pressure eviction (:152); acquire fault at the service boundary with zero residue
(test_model_failure_matrix.cpp:743); cancel between batches (:427); provider crash mid-run
leaves no partial output (:328); pool bounded under concurrent acquires (:563); idle eviction
(:638). No unproven pairing gap found at source (model_runtime.cpp:940-1012). Multi-session
fault tests exist. → no new implementation; cite in PR notes.

### G5 ensemble/single output uniformity — deltas found (folded into G1 work)
Field parity matrix (payload P / durable prov D): single raster P+D complete; single
multi-input P+D complete; single scene doc complete post-#1270 (incl. model.task at
tile_inference_engine.cpp:2677); ensemble lanes complete post-#1270.
Remaining gaps, both closed by this track:
1. single-model detection payload lacks the `provider` execution-identity block the raster
   payload has (model_execution_service.cpp:349-359 vs :299-316);
2. detection sidecars (both lanes) lack the `inputs` grid block (DetectionTileStats has no
   inputGrids) — fixed via the G1 stats extension;
3. single-model detection prov adds `model.task` + effective conf/nms thresholds (run
   identity; overrides at model_execution_service.cpp:273-280 are part of the product
   identity).

## Oracle binding table

| # | Oracle (file → TEST_CASE) | Gap | RED on master | GREEN |
|---|---------------------------|-----|---------------|-------|
| 1 | test_model_tasks → "a single-model detection product publishes a verifier-accepted provenance sidecar" | G1 | ✅ FAILED :807 `REQUIRE(fileExists(sidecar))` → false (lane publishes no sidecar; 10 other assertions green = harness sound) | ✅ pass |
| 2 | test_model_tasks → "a detection sidecar failure restores the previous vector product and its provenance" | G1 | ✅ FAILED :875 `REQUIRE(fileExists(sidecar))` → false | ✅ pass (one test bug fixed: the hostile stage-dir assertion asserted the test's own obstacle) |
| 3 | (inside oracle 1: payload provider block parity) | G5 | (asserted after the sidecar REQUIRE — master fails earlier) | ✅ pass (`provider.execution_provider/runtime_version` on payload + sidecar) |
| 4 | test_ensemble_detection → "detection ensemble fuses member boxes…" inputs assertions | G5 | ✅ FAILED :666 `REQUIRE(provenance["inputs"].isArray())` → false (30 other assertions green: fusion block intact) | ✅ pass (suite 17/17) |
| 5 | test_plugin_model_bridge → concurrent infer serialized | G2 | ✅ 3× deterministic: `CHECK_FALSE(overlapped)` → `!true` | ✅ pass; **MUTATION ORACLE**: mutex removed → test fails again cleanly (kill proven); mutex restored → green |
| 6 | test_exprs_plugin_loader → live sibling's pid-attributed last-good survives sweep | G3 | ✅ FAILED :1525 exists → false (master deleted a live sibling's snapshot) | ✅ pass |
| 7 | test_exprs_plugin_loader → dead owner collected; legacy+own-pid rules | G3 | ✅ FAILED :1574 own-pid dir removed on master | ✅ pass |

## Implementation rounds (each verified immediately)

- Round 1 (G1+G5): new `model_publish.{h,cpp}` (publishProvenanceSidecar w/ caller-named
  fault point, DetectionPublishGuard w/ per-lane backup suffix, crsDisplayName,
  buildDetectionProvenance); model_ensemble.cpp + tile_inference_engine.cpp consume the
  shared authority (their static copies deleted — single truth source); DetectionTileStats
  +inputGrids (engine fills from the opened ds after all gates); ensemble detection lane
  copies the primary member's grids; service detection route: guard + sidecar publish +
  provider payload block. Verified: test_model_tasks 14/14 (12692 assertions),
  test_ensemble_detection 17/17.
- Round 2 (G2): adapter `m_inferMutex` + contract citations in the bridge. Test-side fix
  during the round: the initial std::barrier was destroyed while workers unwound (UB crash
  in the TEST, not the product) — replaced with an atomic spin gate; a single-threaded
  warmup isolates cold-start paths from the race. RED re-proven 3× deterministic; mutation
  oracle (lock removed → red, restored → green).
- Round 3 (G3): last-good pid attribution — sweep conservative both-readings rule,
  registry writes `last-good-<id>-<pid>`, uninstall cleans attributed + legacy layouts,
  header docs updated; two pre-existing tests updated for the new grammar (one now seeds
  BOTH layouts). Verified: loader 29/29 (247 assertions) + completion13 2/2, plugin_system
  16/16.

## Regression battery (pass 1, all fixed tree)

test_model_ensemble 12 ✅, test_ensemble_parallel 6 ✅, test_model_failure_matrix 16 ✅,
test_provider_fallback 3 ✅, test_model_runtime_8 17 ✅, test_model_runtime_9 16 ✅,
test_detection_nms_10 5 ✅, test_device_planner 9 ✅, test_eo_platform_10 21 ✅.

## Discipline

RED first (stash/revert binding file → incremental ninja → record failing assertions → pop).
One attributable change per round. No second truth source. Narrow targets only. Key oracles
twice consecutively before PR. Ledger updated every round.


## Independent adversarial review — round 1 (fresh-eyes reviewer)

Verdict: PROCEED-WITH-FIXES. P0=0, P1=3, P2=2, P3=6. All P1/P2 closed in commits
84b150b31 (guard hardening + regression test), 48e95281c (sweep keep-condition pin),
24e1b69fb (real generation oracle); re-review round 2 dispatched on the exact delta.

- P1-1 ctor could throw AFTER parking the main file (previous product invisible):
  companions+prov park first with per-failure rollback, main parks last. CLOSED.
- P1-2 detectionSidecars classified the BACKUP path by suffix (never .shp) so the
  companion backup family was never pre-cleaned/disarmed — leaked on every
  successful shapefile republish: detectionSidecarsFor(final, suffix) derives
  backup names from the final base. CLOSED (+P3-7: companion set extended to the
  writer's group family .qpj/.sbn/.sbx/.qix so a republish cannot leave a stale
  CRS override).
- P1-3 crash mid-run orphaned the backup family and left the product missing with
  no recovery: ctor adopts the crash orphan back before parking; disarm cleans
  unconditionally. The one-run park window itself is kept deliberately (closing it
  means moving the guard inside engine.run()); documented in the PR body. CLOSED
  as a self-healing exposure.
- P2-4 sweep dead-owner keep conditions unpinned: new TEST_CASE (dead owner kept
  while id live / collected when unknown). CLOSED.
- P2-5 bridge "generation" test never unloaded: now closes+reopens the barrier and
  asserts the typed refusal. CLOSED.
- P3-6 stale .prev~ comment corrected. CLOSED. P3-7 folded into P1-2 fix. CLOSED.
- P3-8 (legacy last-good invisible to reload until first re-load; dev-only,
  self-healing), P3-9 (same-model ensemble members serialize per contract —
  throughput-visible), P3-10 (ensemble sidecar lacks model.task / ensemble-level
  digest; primary-member preprocess note), P3-11 (dtor best-effort silence,
  consistent with the raster lane): documented, deliberately not changed.

## Review-fix regression evidence

test_model_tasks 15/15 (12710 assertions; new residue+orphan-recovery test),
test_ensemble_detection 17/17, test_plugin_model_bridge 2/2 (real generation
oracle), test_exprs_plugin_loader 30/30 (new dead-owner pin), test_model_ensemble
12/12, test_ensemble_parallel 6/6, test_model_failure_matrix 16/16,
test_provider_fallback 3/3, test_model_runtime_8 17/17, test_model_runtime_9 16/16,
test_detection_nms_10 5/5, test_device_planner 9/9, test_eo_platform_10 21/21,
test_exprs_plugin_system 16/16.


## Re-review round 2 + round-2 fixes

Round 2 verdict: PROCEED-WITH-FIXES. P0=0, P1=0, P2=1 (N-1: the adoption oracle was
vacuous — a succeeding post-crash run cannot distinguish adoption from
delete-and-republish), P3=1 (N-2: restore order was main-first, leaving a torn-restore
window). All round-1 closures verified with mutation checks by the reviewer.

Round-2 fixes (commit 0daf08ce2):
- N-1: the post-crash run now FAILS at the sidecar publish; the test asserts the
  adopted pair is back byte-identical. Counterfactual: adoption deleted → output
  stays ABSENT after the failed run → test fails. CLOSED.
- N-2: destructor + adoption restore companions/prov FIRST, main LAST (crash
  mid-recovery leaves the main parked = the state the adoption branch re-enters
  through). CLOSED.

## Final verification (final tree, after all review fixes)

Key suites, two consecutive passes, both green:
- test_model_tasks 15/15 (12719 assertions)
- test_ensemble_detection 17/17 (149)
- test_plugin_model_bridge 2/2 (23)
- test_exprs_plugin_loader 30/30 (249)
Plus green re-runs after the last change: test_model_ensemble 12, test_ensemble_parallel 6,
test_model_failure_matrix 16, test_eo_platform_10 21.


## Re-review round 3 — FINAL VERDICT: READY

Reviewer verified 0daf08ce2 with counterfactual traces: N-1 caught under all three
breakage hypotheses (adoption deleted / prov forgotten / main move forgotten), N-2
converges for every crash suffix of park/restore/adopt. Final tally: P0=0, P1=0, P2=0
open (5 raised, all closed), P3=0 open (2 closed in code, 5 documented/accepted).
