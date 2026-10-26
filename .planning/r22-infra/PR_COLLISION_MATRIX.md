# R22-07 PR Collision Matrix — re-evaluated at start & before push

## Baseline: master @ 911e0b17 (2026-10-25)

## Open R21 PRs file intersection (live `gh pr view --json files` at start)

| PR | Track | Intersection with r22-infra writes | Risk & handling |
|---|---|---|---|
| #1673 (r21-B) | data/model-runtime | `tests/CMakeLists.txt` (their own append block), **`tests/test_data_source_providers.cpp`** (new symlink/canonical test cases) | tests/CMakeLists: both strictly append-only at different tails → textual conflict at most, trivially resolvable. test_data_source_providers: my change is the line-503 wall-clock region (comment + `< 2000`→`< 10000`); theirs adds new TEST_CASEs elsewhere in the file → semantic disjoint, line-adjacent risk low. Re-checked before push. |
| #1674 (r21-E) | teaching/verify | `tests/CMakeLists.txt` only | append-only, disjoint blocks. |
| #1675 (r21-C) | pi/mcp | none | — |
| #1676 (r21-A) | scientific | none | — |
| #1677 (r21-D) | qt/qgis | none | — |

## Merged R20 reference PRs (#1668–#1672)

No file intersection with this track's writes (verified: R20 touched analysis/processing/app/planning lanes; the only shared file is `tests/CMakeLists.txt` which is append-only by repo convention).

## #1662 124-issue sweep

Sweep-derived R17-infra issues (#1681–#1699 cluster) had NO fixes landed on master — verified by direct source reads (ISSUE_TRIAGE.md); this track does not resurrect any swept-away script.

## Merge-loss guards this branch re-verified before push

1. `git fetch origin master` + SHA recheck; rebase if moved.
2. Re-run `gh pr list` + per-PR files; if any R21 PR started touching my files (esp. `test_data_source_providers.cpp`, `tests/CMakeLists.txt` tail), re-evaluate region overlap.
3. Full-file re-read of the two append-only registry edits before commit.
