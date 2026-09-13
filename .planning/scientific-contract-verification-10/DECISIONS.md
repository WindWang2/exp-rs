# DECISIONS — scientific-contract-verification-10

## D-1 Scientific contract authority = in-tree C++ registry keyed by operator id, cross-checked against live code

**Options considered**:
1. Stamp scientific semantics into every operator's `schema()` JSON (like `stampDeterminismGrade`) and scan it out.
2. JSON sidecar under `data/contracts/` per operator.
3. Central C++ table `src/contracts/scientific_contract.cpp` keyed by operator id.

**Decision: option 3, with mechanical cross-checks against options-1-style code stamps.**
Reasons: (a) dimensions like numeric-domain/mask-semantics have *no existing authority anywhere* — this creates the first truth for them, not a parallel second truth; (b) a central table is one file owned by this track → zero merge friction with 9 concurrent operator tracks; (c) completeness is mechanically enforceable (live registry ids ⊆ table keys and table keys ⊆ registry ids, tested against the real registry, not a snapshot); (d) where an authority DOES already exist (determinism grade stamped in schema; NoData sentinel written by code), the table must *agree* with it and the test fails on disagreement — converging instead of forking.

## D-2 qa_mask fail-closed semantics

`rs:qa_mask` unreadable QA words (NaN / negative / declared-NoData) now produce **masked=1** (fail-closed) instead of 0 (clear). Rationale: a quality gate must fail toward "excluded", never toward "trusted"; silent cloud/snow leakage poisons downstream science. `mask=all` for SCL additionally masks SclNoData. Bit-exact behavioral change is the point; tests updated, CHANGELOG entry added. Generic-bitmask source: negative/NaN/NoData → masked as well.

## D-3 class_mapping encoding rule

Manifest validation: class_mapping target values must be ≤ 254 when output stays Byte *or* the engine must escalate. Engine rule (single authority): Labels encoding is chosen from `max(classMapping)` when class_mapping is non-empty (1+max > 255 → UInt16/NoData=65535), else from class count — matching the existing `classPixelCounts` domain allocation. Catalog additionally refuses `class_mapping` targets > 65534 outright (no encoding can represent them).

## D-4 NMS bounded + cancelable

`dedupDetections`/`nonMaxSuppression` gain a `std::function<bool()>` cancel predicate checked every iteration block (cheap: every 256 inner iterations), throwing the typed cancel error; plus a uniform-grid bucket index (cell size = max box extent) so comparisons stay near-linear in practice for typical distributions. `maxDetections` semantics stay "accumulation refusal line" (already enforced pre-dedup); dedup remains whole-raster (tile-overlap resolution needs global view).

## D-5 pi bridge convergence without merging the two files

The two bridges are loaded by different hosts (mcp_bridge.ts by new integrations; exp-rs-spatial.ts by Pi/jiti per `pi/README.md`). Full merge is an architectural change owned by no current track; Track 10 does the *minimum drift repair*: (a) overflow branch in both files kills the child (lazy-respawn takes over); (b) startup-deadline success-path cancel backported to exp-rs-spatial.ts via try/finally shape; (c) a node-based structural anti-drift test (`pi/test/bridge_parity.test.mjs`) asserting both files implement overflow-kill + deadline-cancel, so the next fix cannot land on one side only.

## D-6 F-OPS-4 plumbing shape

`WarpOptions` gains `sourceCrsOverride` (empty = keep file CRS, never guess); `warpRaster` emits `-s_srs` only when non-empty; `IoReprojectOperator::run` passes the declared param through. `io:clip` keeps its existing fold-into-targetCrs behavior (bounds are in source CRS; unchanged semantics) — only reproject's dead param is repaired.

## D-7 Planning files are force-added

`.git/info/exclude` (shared local git dir) carries `.planning/` which overrides whitelist `check-ignore` semantics for fresh files; 348 planning files of prior tracks are tracked. Track convention: `git add -f .planning/<slug>/*.md` + `.gitignore` whitelist block (three-line pattern per goal-template runbook step 2). Recorded here once; `check-ignore` exit code not used as the gate (runbook step 2's intent — files actually tracked — is asserted via `git ls-files` in EVIDENCE.md).
