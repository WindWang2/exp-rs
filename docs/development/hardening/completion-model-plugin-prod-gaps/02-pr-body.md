# Completion 13/15 — plugin/model runtime: the three production gaps #1270 recorded, closed

## Baseline

- Branch `completion/model-plugin-production-gaps`, cut from `origin/master` =
  `e4904cd3c568e1e730396237ec7034dc9215b272` (re-verified at execution time 2026-09-23;
  unchanged at PR time).
- Scope = exactly the gaps #1270 (hardening 15/20, same module, merged same day) recorded as
  known limits / "needs design agreement": single-model detection provenance sidecar,
  `IPluginModelRuntimeV1` forward serialization, cross-process last-good snapshot sweep.
- Open PRs at start: 0. During execution siblings #1277/#1278/#1279 opened: zero file
  overlap with this branch except appends to `tests/CMakeLists.txt` (all four append
  independent test-target blocks; union-friendly, no semantic conflict, no merge order
  dependency).

## What changed

### 1. Single-model detection products publish their provenance sidecar (`src/operators`)

Master's detection lane was the only model lane with no `.prov.json`:
`verifyProductAgainstModel` reported `MissingSidecar` for every single-model detection
product, deterministically (RED captured: sidecar absent). The ensemble lanes' machinery
(`publishSidecar`, `DetectionPublishGuard`, `crsDisplayName`) moves verbatim into
`model_publish.{h,cpp}` as the ONE publish authority (no second truth source — the ensemble
lanes now call the same code), and the single-model detection route publishes under the
same contract: previous product + shapefile companions + old sidecar are parked across the
vector write AND the sidecar publish, a sidecar failure restores the verified PAIR, and a
crash can only ever leave a MISSING sidecar, never a stale one (the exact invariant
hardening 15/20 established for the raster lanes).

The sidecar is `exp-rs-prov/1` with: model identity incl. `task` intent (mirrors the scene
artifact), execution identity (backend/device/execution provider), effective detection
thresholds + class vocabulary + counts, the fed input grid, and the `format: "vector"`
output block the verifier keys on. Uniformity deltas folded in (G5):

- `DetectionTileStats` gains `inputGrids` (filled by the engine from the opened ds after
  every input gate passed, mirroring the raster engines) — the ENSEMBLE detection sidecar
  gains its previously missing `inputs` block too (primary member is the grid authority).
- The detection payload gains the `provider` execution-identity block the raster payload
  already published (Platform 9.0 M8 parity).

### 2. Plugin model runtime forward passes serialize at the bridge gate (`src/plugins`)

`model_runtime.h` promises sessions "safe to use from any thread (implementations serialize
forward passes internally)"; the built-in OpenCV DNN provider keeps an infer mutex for
exactly that; the in-process plugin adapter instead relied on plugin implementations being
re-entrant — two threads sharing a cached session could enter plugin `infer()`
concurrently (the out-of-process host path already serializes in the worker,
`host_protocol.h`). The contract is now enforced in `PluginModelRuntimeAdapter::infer`
with one mutex per adapter — the same idiom as the built-in provider, at the host-side
gate between the registry pool and plugin code, no locks scattered into callers.
This resolves #1270's recorded "design decision" in the direction the contract text
already implies.

### 3. Dev last-good snapshots are pid-attributed (`src/sdk`)

`sweepPluginSnapshots` keyed `last-good-<id>` purely on the sweeping process's live ids:
any second instance configuring a registry over the shared temp root collected the first
instance's hot-reload rollback source mid-flight. last-good now follows the grammar every
other snapshot class already uses: `last-good-<id>-<pid>`. Sweep rule: same-pid or live
foreign owner → keep; the suffix is ambiguous with legacy ids ending in `-<digits>`, so a
suffixed name is collected only when BOTH readings are dead (pid-attributed reading dead
AND legacy reading dead) — conservative, never destroys a live sibling's or a live legacy
plugin's snapshot; the suffix-less layout keeps the historical liveIds rule.
`uninstallPlugin` cleans both layouts. Crash mid-write was already safe (staging +
marker-last + rename ladder + marker/identity gates on restore) and is unchanged, now
covered by an explicit oracle. This resolves #1270's "cross-process liveness attribution
decision": ownership belongs to the creating process.

Deliberate behavior delta: a restarted process re-captures its own last-good on its first
dev-mode load instead of silently inheriting a dead sibling's copy (the reload identity
gate already refused stale reuse; ownership is now explicit and honest).

## Tests (all extend existing suites except one new narrow target)

RED method: implement nothing, run the new oracles on pristine `e4904cd3c`, record the
failing assertions, then implement and re-run. Full table in
`docs/development/hardening/completion-model-plugin-prod-gaps/00-goal-and-oracle.md`.

| Oracle | RED on master | GREEN |
|---|---|---|
| test_model_tasks → single-model detection sidecar verifier-accepted | FAILED (:807 sidecar absent) | ✅ |
| test_model_tasks → detection sidecar failure restores previous product+sidecar PAIR | FAILED (:875) | ✅ |
| test_ensemble_detection → ensemble detection sidecar records the input grid | FAILED (:666 inputs absent) | ✅ |
| test_plugin_model_bridge (new) → concurrent infer on one shared session serialized | 3× deterministic: overlap detected | ✅ |
| — mutation oracle: lock removed → test fails again; restored → passes | ✅ kill proven |
| test_exprs_plugin_loader → live sibling's pid-attributed last-good survives another process's sweep | FAILED (:1525 master deleted it) | ✅ |
| test_exprs_plugin_loader → dead owner collected; legacy + own-pid rules unchanged | FAILED (:1574) | ✅ |

Suites (narrow targets only, `-j2`; no full-app build was performed):

- test_model_tasks 14 cases (12692 assertions), test_ensemble_detection 17, test_exprs_plugin_loader 29,
  test_exprs_plugin_system 16, test_plugin_model_bridge 2 — key suites run TWICE
  consecutively, both passes green.
- Regression battery, one pass: test_model_ensemble 12, test_ensemble_parallel 6,
  test_model_failure_matrix 16, test_provider_fallback 3, test_model_runtime_8 17,
  test_model_runtime_9 16, test_detection_nms_10 5, test_device_planner 9,
  test_eo_platform_10 21 — all green.

## Honest limits

- The serialization oracle widens the race window with sleeps; it is deterministic in
  practice (4 callers, spin gate, 120 ms hold) and it kills the exact mutation
  (lock removal), but it is not a scheduler-proof interleaving oracle.
- The bridge mutex serializes forwards per session; throughput for one shared plugin
  session is intentionally unchanged from the documented contract (built-in providers
  behaved this way all along). A manifest listing the SAME model as several ensemble
  members now runs those members' forwards serially at the adapter — they share one
  cached session, and that is exactly what the contract text always required.
- pid reuse (a dead owner's pid recycled to an unrelated process) can keep a stale
  last-good one sweep longer — the same accepted exposure the ~staging-/upgrade-
  grammars documented when pid attribution was introduced.
- A legacy (pre-attribution) `last-good-<id>` snapshot is kept by the sweep while the
  plugin is live but is not READ by reload (which consults the pid-attributed path
  only): after upgrading the tooling, the first dev hot reload reports "no
  last-known-good snapshot" until the plugin loads once under the new build.
  Dev-only and self-healing.
- The detection publish guard parks the previous product for the duration of one run;
  a `kill -9` inside that window leaves the product absent with a hidden backup until
  the next run adopts the orphan back (now a tested, self-healing path). The ensemble
  lane has the same window shape.
- The ensemble provenance sidecar does not yet record `model.task` or an ensemble-level
  content digest (the single-model detection sidecar records both); the ensemble
  `inputs` block carries the primary member's preprocess note. Parity nits only — no
  field is a lie.
- Full-app/QGIS link and the online CI run are deliberately left to Prompt 16 / merge
  queue; every changed target compiles and its suite is green locally (Debug, Ninja).

## Independent review

One full adversarial review round (fresh-eyes reviewer, round 1 verdict
PROCEED-WITH-FIXES: 0×P0, 3×P1, 2×P2, 6×P3). All P1/P2 closed with regression tests:
detection publish guard parks companions first and rolls back per failure, adopts
crash orphans, and cleans the backup family unconditionally (the old companion helper
classified the backup path by suffix and leaked every shapefile republish); the sweep's
dead-owner keep conditions are pinned by a new oracle; the bridge generation-refusal
test now really unloads. Re-review round 2 raised one P2 (vacuous adoption oracle) +
one P3 (recovery order), both fixed in 0daf08ce2; round 3 traced the counterfactuals
and returned **READY**. Final tally: P0=0, P1=0, P2=0 open. Residual P3s are recorded
in the ledger and the notes above.

## Rollback

Three independent conventional commits, each revertable: models sidecar, plugins bridge
lock, sdk pid attribution. No schema/format change consumers depend on beyond the sidecar
the lane SHOULD have been publishing; the only on-disk layout delta is the pid-suffixed
last-good directory name (legacy names still swept).
