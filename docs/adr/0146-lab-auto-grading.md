# ADR 0146: Lab Auto-Grading — Known-Answer Teaching Scores

Date: 2026-09-12 · Status: Proposed (D4) · Owners: D4 track

## Context

The platform verifies itself against closed-form known answers
(`KNOWN_ANSWER_MATRIX.md`: NDVI gain invariance, Horn exactness on linear
surfaces, the 300 K Planck round-trip, Welford closed forms). Teachers,
meanwhile, grade lab submissions by eye. D4 extends the known-answer tradition
into a teaching grader: submissions get a 100-point score with per-assertion
evidence, and deliberately wrong products (wrong stretch, wrong band pair, no
calibration, missing mask, wrong sign, all-NoData, wrong CRS) must reliably
score below the pass line while reference solutions score 100.

The grader's first virtue is **错就是错** — being wrong must cost points,
deterministically and explainably.

## Decision

1. **One scoring model**: `score = 100 − Σ weight(failed assertion)`, floored
   at 0. Assertions declare `{id, kind, weight, severity, params, derivation}`;
   weights sum to exactly 100 (explicit, never normalized). `severity=blocking`
   failures cap the score at `passing_score − 1` and force a fail verdict. No
   partial-credit curves — explainability beats smoothness.

2. **One grading seam**: `OutputVerifier::gradeArtifact(labIdOrRulesPath,
   artifactPath, options) → LabGradeResult {verdict, score, deductions[],
   evidence[], summary, digest}`. The CLI `lab` command is a thin shell; the
   D7 `--batch` feature wraps the same seam. The pre-existing binary
   `OutputVerifier::verify` PASS/FAIL path is untouched for its current
   callers.

3. **Rules as data**: one `data/labs/grading/<lab_id>.rules.json` per lab
   (`sicnu.lab.rules/1`). Tolerances are per-assertion and explicit; closed
   forms from the known-answer matrix are preferred and their derivation is
   written inline. Rules validation failures are usage errors, never silent
   zero-scores.

4. **Evidence is mandatory**: every deduction carries
   `{assertion_id, observed, expected, delta, message}`; every assertion
   produces an evidence record (passed or failed). An evidence-less deduction
   is a P0 defect by definition.

5. **Bounded memory**: grading reads through `RasterReader::readWindow` /
   `iterateTiles` under an explicit `maxBytes` budget (default 64 MiB,
   CLI-overridable). No kernel materializes a whole raster. Budget below one
   pixel is a typed unverifiable error (#808 contract reused, not reinvented).

6. **Deterministic reports**: identical inputs produce byte-identical report
   bodies and digests. Wall-clock time lives only in the header
   (`generated_utc`) and is excluded from the digest; evidence doubles are
   rounded to 12 significant digits; JSON keys serialize in sorted order.

7. **Wrong answers are graded, not excused**: wrong CRS, wrong band count and
   wrong values are blocking deductions (exit 1). Exit 3 "unverifiable" is
   reserved for artifacts that cannot be graded at all (unreadable, zero
   bands, budget below one pixel); usage mistakes (unknown lab, invalid rules,
   missing path) are exit 2. Exit codes coincide numerically with
   `exprs::ExitCode` {0,1,2,3}.

8. **Metrics are reused, not reimplemented**: classification OA/kappa/marginals
   go through `sicnu::experiment::ConfusionMatrix`; windowed reads through the
   `RasterReader` contract; NoData semantics through the canonical metadata.

## Consequences

- Teachers author JSON rules with inline derivations instead of adapting
  binary tools; the corpus manifests (`tests/fixtures/lab/*_corpus.json`)
  pin grader behaviour against committed wrong-answer fixtures, so grader
  regressions are caught by `ctest -R lab_grading`.
- The gain-invariance kernel encodes the known-answer lesson that pure gains
  cancel in ratio indices while per-band gain mismatches and offsets do not —
  "forgot calibration" is therefore a *documented* pass for NDVI band pairs
  and a fail for anything offset-sensitive.
- D1 ground truth and D2/D3 `grading_ref` files were not merged when this ADR
  was written; the committed synthetic scene fixtures stand in for them and
  the rules' truth paths are replaceable without touching the grader.
- Batching (D7) and CSV export stay out of D4; the seam is documented in
  `docs/labs/GRADING.md`.

## Alternatives considered

- Partial-credit curves around each threshold (rejected: unexplainable score
  jumps, and the mission forbids curve fitting).
- Comparing submissions pixel-by-pixel against a reference raster (rejected:
  brittle to representation choices; statistics + shape + interval assertions
  grade the *answer*, not the bytes).
- A general expression language for assertions (rejected: the nine kernels
  cover the known-answer families; anything else invites non-derivable
  thresholds).
