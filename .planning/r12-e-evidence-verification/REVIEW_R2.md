# R12-E Independent Review — Round 2

Reviewers: REVIEWER-C (adversarial correctness, fresh) and REVIEWER-D (integration/platform/docs, fresh) — neither participated in implementation nor round 1. Target: 28abef3582..HEAD incl. R1-response commit 717f5537f4. Closure verifier for the residual P1: REVIEWER-C (resumed) on commit e067a523b2.

## Verdicts
- Reviewer-C: 0 P0, **1 P1** (fault_transforms.cpp:338-343/:539-544 isIntegral→asInt64 escape, same class as R1 P1), 2 P2 → after e067a523b2: **P1 CLOSED, 0 P0/P1 remaining, no new P2** (explicit closure review).
- Reviewer-D: 0 P0, 1 P1 **process-shaped** (R1 docs fixes uncommitted at review time — closed by the final docs commit), 2 P2 → code-level: "round-2 clean."

## Round-2 findings and dispositions
| # | Sev | Finding | Disposition |
|---|---|---|---|
| C-P1 | P1 | fault_transforms epoch_index/permutation isIntegral→asInt64 throws on UInt64>INT64_MAX across never-throws boundary | FIXED e067a523b2 (isInt64 gates + RED tests); closure verified by Reviewer-C: "Class is closed in both modules" (exhaustive asInt/asUInt/asInt64/asUInt64 grep clean) |
| C-N2/D-2 | P2 | requireSceneNumber accepted negative / out-of-int-range integer doubles (toInt UB) | FIXED e067a523b2 ([0,INT_MAX] gate) |
| C-TOCTOU | P2-low | scenario size probe TOCTOU: unbounded slurp after capped stat | FIXED e067a523b2 (cap+1 sentinel read, boundary pinned) |
| D-docs | P1-process | R1 doc fixes uncommitted | CLOSED by final docs commit (this file set) |
| D-3 | P2 nit | PERFORMANCE.md understated D7 validation cost | FIXED (docs commit) |
| D-4 | nit | ORACLE_LEDGER typo + missing resolution column | FIXED (docs commit) |

## Round-2 verified-sound (post-fix)
- Mirror depth-cut: unresolved parent at bound benign in both load orders; resolving parent at bound refused; no false negatives; revalidation idempotent.
- Roll-up: uncovered results promote per policy, recorded on result.promoted, serde-consistent; no double-count; uncovered-only-Pass still surfaces kSpecInvalid.
- Preflight authority routing: no precision-12 writer remains on any digest/projection path (only canonicalFindingText tie-break, non-digest, documented).
- Scene gate: check order finite→integer-valued→range; INT_MAX exact-representable boundary pinned; valid-input path byte-identical.
- Bounded read: cap enforced on bytes actually read (gcount), exactly-4MiB accepted / +1 refused pinned.
- Portability: no POSIX-only constructs; seekg/tellg standard; locale guards untouched; no QT_NO_KEYWORDS assumption.
- Hygiene: zero TODO/FIXME/debug output/commented-out code in the diff.

## Round-2 exit criterion
**MET: 0 P0 / 0 P1** (Reviewer-C closure review, post e067a523b2). Remaining P3 observations (canonicalFindingText tie-break precision, D13 absent-evidence silence, authority ≥2^53/NaN narrowing pre-existing) recorded in DECISIONS.md as follow-ups.
