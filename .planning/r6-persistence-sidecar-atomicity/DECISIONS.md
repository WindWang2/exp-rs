# R6 DECISIONS

## D1 — One sidecar write authority lives in `src/platform/`, not `atomic_fs`

`sicnu::platform::sidecar` (`src/platform/durable_sidecar.{h,cpp}`, new `sicnu_platform`
STATIC lib) is THE authority for sidecar-class durable writes: prepare → validate →
write temp (O_EXCL, same dir) → durability boundary (fsync via portable) → publish
(Windows ReplaceFileW / MoveFileExW WRITE_THROUGH; POSIX rename + best-effort dir
fsync) → last-good rotation → verify (optional read-back).

Why not extend `atomic_fs`: `sicnu_sdk` (plugin index/discovery writers) must stay
dependency-light and cannot link geospatial (GDAL/Qt). platform is the only layer
every writer can reach. `atomic_fs` remains the dataset-GROUP publish authority
(multi-file GDAL datasets, #1403-fresh code — not destabilized); its header and the
authority's header bind the two-layer contract: NEW sidecar writers must use
`platform::sidecar`; dataset group publishes use `atomic_fs`. Re-unification is
future work, documented, not silently forked.

## D2 — Deterministic fault seams: injected hooks, registry-free

The authority takes `WriteHooks`/`ReadHooks` (named-phase failure injection +
payload override) instead of `SICNU_FAULT_POINT`. Reasons: (a) `sicnu_runtime` is a
SHARED lib precisely because FaultRegistry must be process-unique — linking it from
`sicnu_platform` would drag the runtime DLL into the plugin-SDK deployment surface;
(b) a static platform lib without globals makes duplicated code harmless. Seams are
the same KIND as FaultRegistry (deterministic, named phases, test-only arming, RAII);
call sites that already live in runtime-linked modules keep/adopt SICNU_FAULT_POINT
for caller-level semantics. No sleeps, no random — every crash point is a named phase.

## D3 — #1394 item 2: drop `saveMissionTimelineToSidecar` entirely

Zero production callers (verified by census + grep). ADR 0166 makes the legacy
timeline channel import-only; `loadMissionTimelineFromSidecar` stays (read path).
API removal > test-only marking: the write path stops existing, enforced by the
compiler.

## D4 — Classification sidecar: converge + last-good + version gate

`RsPostProcess::saveClassMetaData` routes through the authority (temp+fsync+publish +
`.last-good` rotation). `loadClassMetaData` resolves main → last-good → typed corrupt,
and NOW enforces the `version` field it previously wrote but never checked (forward
refusal = fail-closed; old-schema = accepted, it is version 1 and the schema is
additive). The main-window silent-skip call sites are unchanged (recovery happens
inside the analysis-layer reader, so no app-layer change is needed for durability).

## D5 — Migration set (convergence scope, ranked by census risk)

saveClassMetaData (#1), plugin saveUserIndex (#2), plugin_discovery storeIndex (#3),
cli_batch_runner writeResultIndex (#4), workflow_checkpoint Windows lane (#5),
harness evidence atomicWrite + writeProvenanceSidecarIfAbsent + provenance_projection
writeCompileSidecar (#6), output_committer group publish → atomic_fs::publishStagedMembers
(#7, processing already links geospatial), study/studio/registration/lab-batch
QSaveFile quartet (#8), range_cache putBlock (#9 — EXEMPT after review: cache-only, loss benign, deliberate lock-scope design),
session_journal (#10), mission_context_store sidecar write + mission_runtime_store
rotateLastGood (the reference family adopts its own generalized authority), 
teaching_admin batch checkpoint. Each migration: same-file behavior preserved,
durability raised, typed error mapping preserved/adapted.

## D6 — Crash-window doctrine (WP-C)

Every crash point gets a documented state: before-temp / mid-temp / after-temp /
post-fsync / pre-publish / post-publish / pre-lastgood / post-lastgood. Invariants:
main is old-or-new at every instant (rename atomicity; Windows ReplaceFileW
transactional); temp residue is inert (O_EXCL pid-tagged names, never read by any
reader); last-good may lag one generation but never leads main. "Process killed
mid-phase" is tested deterministically by phase faults + post-fault state
assertions (the QProcess/SIGKILL injector exists only for the workflow lane and is
not duplicated for sidecars — phase-fault equivalence documented per case).

## D7 — Out of scope

#1387 digest-authority fork (separate open issue); sqlite store internals beyond
adjacent audit of #1405; ADR 0166 mission-runtime STAC dual client and
cli_commands Divergent Change (#1394 items 3/4 — different domains); no new
persistence directions.
