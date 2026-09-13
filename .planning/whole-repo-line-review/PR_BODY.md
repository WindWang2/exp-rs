# review: whole-repository line-by-line audit dossier

## What this is

Whole-repository six-lens code-review dossier for `exp-rs` @ `27b9aa0a63` (origin/master; the GOAL-pinned `efc5c52f` + 23 later commits all covered via an explicit delta-pass). **Review-only track**: the diff contains NO changes under `src/` or `tests/` — only `WHOLE_REPO_REVIEW.md`, `review/`, `.planning/`.

## Deliverables

- `WHOLE_REPO_REVIEW.md` — executive summary: severity matrix, top risks, coverage, false-positive accounting, honest method/depth disclosure.
- `review/COVERAGE_LEDGER.csv` — 2,998 first-party files **100% reviewed / 0 skipped**, 15 excluded rows all reasoned (vendored/non-code), incl. the 551 `src/ui` assets and Tier B vendored trees.
- `review/findings/{operators,pi}.md` — 7 findings (1×P1, 5×P2, 1×P3), every one with verbatim code quotes, trigger condition, evidence grade.
- `review/issues/F-*.md` — 7 issue drafts (filed locally per standing agreement; no `gh issue create` from this track).
- `review/tests/F-*.cpp` — Catch2 assertion drafts for every P1/P2 (+ the P3), intentionally NOT wired into CMake.
- `review/DEDUPE.md` — per-finding comparison against all 250 closed issues (#595–#945) and the historical audit findings: zero duplicates.
- `.planning/whole-repo-line-review/` — GOAL/PLAN/DECISIONS/EVIDENCE/REVIEW_LOG (method notes, considered-and-dropped records, both subagent adjudications).

## Findings (top 3)

1. **P1 — F-OPS-4**: `io:reproject`'s `srcCrsOverride` is a dead parameter: the refusal gate demands it, but `WarpOptions` has no source-CRS field and `warpRaster` never writes `-s_srs` — a CRS-less input reprojects as identity and gets **labeled with the target CRS** (silent georeferencing corruption). The sibling `io:clip` consumes the same parameter functionally, proving drift.
2. **P2 — F-OPS-1**: Labels output `class_mapping` product classes are not bounds-checked against the output encoding chosen from the *pre-remap* class count — product class ≥255 clamps into the 255 NoData sentinel; a whole class silently disappears while palette/counts still claim it.
3. **P2 — F-OPS-3**: `rs:qa_mask` fail-open: unreadable QA samples (NaN / negative sentinel / declared NoData) convert to QA word 0 = "clear" (SCL class 0 is never masked, even with `mask=all`) — the quality gate fails in the wrong direction. Deliberately preserved by #699's UB fix; this issue targets the semantics.

Plus: detection NMS O(n²) outside the cancellation checkpoints (F-OPS-5), the pi bridge desync-zombie and the startup-deadline fix that never got backported to the file Pi actually loads (F-PI-1/2), and a latent `TensorBlob::fromMat` ND-ROI zero-data path (F-OPS-2).

## Verification protocol

- Two-pass adjudication: main-agent self-review + read-only subagent V (false-positive sweep: 0/6 retractions, 2 evidence refinements folded in) + subagent C (coverage audit: ledger honest; its bookkeeping findings all remediated; its thin-evidence flags spot re-read).
- False-positive rate: 0 retracted / 7 submitted (plus 4 candidates dropped in round 1, recorded in REVIEW_LOG).
- All historical remediations sampled (#694, #727, #746–#758, #774, #787–#811, #848–#882, #893–#945) verified in place; delta commits (#936–#945) reviewed hunk-by-hunk with no new findings.

## Not done here (by contract)

- No fixes to `src/` / `tests/`; no remote issue creation; no CI triggered; worktree retained until merge.
