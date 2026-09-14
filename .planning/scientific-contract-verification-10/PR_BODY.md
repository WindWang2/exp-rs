# Scientific Correctness, Contract & Verification Platform 10.0

**Baseline**: `origin/master` @ `7d78059d1a` ("Merge PR #958 …"). Branch
`zcode/scientific-contract-verification-10`, worktree
`../exp-rs-scientific-contract-verification-10`. **Local evidence only; no
online CI dependency** — every claim below maps to a command + exit code in
`.planning/scientific-contract-verification-10/EVIDENCE.md`.

## Architecture (what this track adds, and what it deliberately does not)

Predecessor Contract Platform 9 (`src/contracts/`, ADR-quality docs in
`docs/verification/CONTRACT_PLATFORM_9.md`) is **extended, not duplicated**.
Track 10 adds the scientific semantic layer:

- `src/contracts/scientific_contract.{h,cpp}` — one machine-readable record
  per first-party `rs:` operator (114) covering the dimensions that had NO
  existing authority: numeric domain, scale/offset policy, NoData semantics,
  categorical encoding + class-id range, time alignment, wavelength policy,
  seed policy, cancellation granularity, atomic publication, provenance,
  refusal codes. Closed vocabularies; family templates + per-operator
  overrides; every record carries a machine-checked evidence anchor; schema
  `exp.scientific_contract.v1`. Dimensions that already had an authority
  (modality, band roles, io, determinism grade) are NOT duplicated — the
  drift gates bind the existing sources to each other.
- The contracts project into the contract graph (`scientific_contract_for`
  edges) and the regenerated snapshot pins them: 976 nodes / 389 edges /
  0 findings.

## Major deliverables

1. **All 7 whole-repo-review findings fixed** (re-verified live on the
   baseline before touching anything; none was superseded):
   - F-OPS-4 (P1) `io:reproject` srcCrsOverride now reaches the warp as
     `-s_srs` — a CRS-less input is actually transformed instead of being
     re-tagged with the target CRS.
   - F-OPS-1 (P2) Labels `class_mapping` drives the output encoding
     (Byte→UInt16 at 256 product classes; manifest refuses targets ≥65535).
   - F-OPS-3 (P2) `rs:qa_mask` fails CLOSED on unreadable QA samples
     (NaN/negative/declared-NoData → masked; SCL out-of-domain words and
     `mask=all` class 0 included); result gains `unreadableSamples`.
   - F-OPS-5 (P2) detection dedup/NMS: exact-result uniform-grid bucketing
     (differential-tested against the dense reference) + cancellation probe
     every 256/4096 iterations.
   - F-OPS-2 (P3) `TensorBlob::fromMat` handles non-continuous N-D Mats.
   - F-PI-1/2 (P2) pi bridges: desync kills the child (SIGKILL escalation),
     deadline cleanup parity, generation-aware stdout/exit handling, framing
     buffer reset on respawn (a replay bug found *while testing the fix*),
     structural parity test so the next fix cannot land on one side only.
2. **Cross-projection drift gates** (`test_drift_projection_10`, 1885
   assertions): schema determinism stamps ≡ capability sidecar grades;
   sidecar io parameters ≡ live schema (both directions); LabSpec
   `operator_id` references resolve live.
3. **Verification lanes** (`test_science_verification_10`, 1187 assertions):
   NDVI scale-invariance (metamorphic, with the closed-form 1/3 pinned),
   byte-identical replay, isodata seed determinism, bounded deterministic
   CRS-refusal fuzz (targetCrs + the newly plumbed srcCrsOverride; published
   outputs must be readable and carry a projection), provenance verification.
4. **Completeness repairs the drift gates exposed**: capability sidecars +
   `data/agent/capabilities/preprocess.json` entries for the three PR #956
   CN-satellite import operators; knowledge pages + contract snapshot
   regenerated via the tools (conscious-diff ritual); the D8 suite's and
   gen-meta's hardcoded registry size (111 → monotone floor) fixed.

## Compatibility

- `WarpOptions` gains `sourceCrsOverride` (defaulted empty) — no aggregate
  initializers exist (grep-verified), no caller break.
- `nonMaxSuppression`/`dedupDetections` gain defaulted `CancelProbe` params —
  additive; the single production caller updated.
- Labels rasters with `class_mapping` products may switch Byte→UInt16: that
  is the *repair* of silent data loss (consumers reading class ids already
  handle NoData sentinels per band).
- `rs:qa_mask` semantics change is the point of F-OPS-3 (fail-open →
  fail-closed); the `#699` tests were updated to the new, documented
  behavior, and the CHANGELOG entry states it.
- pi bridge behavior: a desynced child is now replaced instead of turning
  the session into a zombie; recovery is the existing lazy-respawn path.

## Tests (all green at HEAD unless stated)

| Suite | Result |
|---|---|
| test_scientific_contract_10 | 1048 assertions / 5 cases |
| test_drift_projection_10 | 1885 assertions / 3 cases |
| test_science_verification_10 | 1187 assertions / 5 cases |
| test_io_operators (+F-OPS-4) | 92 / 7 |
| test_qa_mask (+F-OPS-3) | 117 / 8 |
| test_tensor_blob (+F-OPS-2) | 173 / 7 |
| test_model_tasks (+F-OPS-5) | 12595 / 10 |
| test_model_runtime_8 (+F-OPS-1) | 2358 / 15 |
| contract-9 regression trio + capability_contract_9 + algorithm_meta_drift + io_raster_contract + model_manifest7 + catalog_v2 + capability_knowledge | all passed |
| node --test pi/test/ | 10/10 |
| test_capability_drift | 19/21 — the 2 remaining failures reproduce identically on origin/master (cartography:diff_templates/explain/export knowledge; harness.optical_ndvi_landsat recipe summary) and belong to the cartography/harness tracks; this branch strictly improves the suite (fixes the CN-operator part) |

## Performance / resource

- All builds `-j2` (CMAKE_BUILD_PARALLEL_LEVEL=2), tests serial; no
  `-j$(nproc)` anywhere. qgis_core/gui vendored builds dominate cost.
- NMS: bucketing touches O(n) pairs for bounded-extent candidate sets and
  degrades — never below the status quo ante — to the dense pass for
  pathological giant-box inputs (documented in code); cancellation now
  bounded to ~256-iteration quanta.
- Ladder lanes L0–L2 recorded in
  `.planning/scientific-contract-verification-10/ladder_l0_l2.json`.

## Review findings

Two read-only adversarial subagents (architecture/science; concurrency/test
trust) produced 22 findings — 1 P0, 4 P1, 8 P2, 9 P3. **All 22 fixed** with
dispositions and evidence in `.planning/scientific-contract-verification-10/
REVIEW_LOG.md` (commit "fix(review): round-1 adversarial review
dispositions"). The registry honesty pass (reviewer-verified against the
implementations) is the substance: the classification family now declares
the pipeline's real escalation/NoData convention; spectral detection is
continuous Float32; connected components' Float32/2²⁴ reality is stated;
rs:infer/rs:segment follow the F-OPS-1 escalation.

## Known limitations

- Determinism-grade stamping remains partial on master (17 stamp sites);
  the sidecar grades of stamp-less operators are bound only when a stamp
  exists — the 80+ unstamped sidecar claims are recorded as coverage debt
  owned by the operator tracks (REVIEW_LOG + EVIDENCE).
- `test_capability_drift` retains the two pre-existing master failures named
  above (cross-track ownership).
- READINESS refresh at this HEAD: L0–L2 lanes executed here; L3+ lanes are
  not-built/not-run in this worktree and are reported as such — never as
  passing.

## Follow-ups

- Operator-owner tracks: stamp determinismGrade + reconcile the 80+ sidecar
  bit_exact claims (the drift gate now binds every published stamp).
- cartography/harness tracks: the three cartography tool knowledge entries +
  the `harness.optical_ndvi_landsat` recipe summary.
- Full-ladder L3–L7 refresh on a machine with the optional deps.
