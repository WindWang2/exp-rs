# R18-B Cross-Track Findings

## CT-1 (CRITICAL, for maintainers/all tracks): merge a8d5cd510 (#1516) reverted merged work from other tracks

Recon for R18-B (2026-10-19) established with git evidence:

- `git cat-file -e f326d8ca9:tests/test_r15_C_chunk_admission.cpp` → present
- `git cat-file -e a8d5cd510:tests/test_r15_C_chunk_admission.cpp` → missing
- `git diff --stat f326d8ca9 a8d5cd510` → −3893 lines: ALL of merged r15-C (#1515: 14 src fixes, 6 test files, 4 modified test files, .planning/r15-c docs) plus r15-a planning docs and part of an r15-a source fix.

Affected domains:

1. **Model runtime (r15-C)** — restored by R18-B in this branch (my domain).
2. **`src/processing/algorithms/image_fusion.cpp` −28 lines (r15-A territory)** — NOT touched by R18-B (R18-A's ownership). R18-A/maintainers should check whether the r15-a HPF/image_fusion fix is missing from master and restore from f326d8ca9 if so.
3. **`.planning/r15-a/*.md` and `.planning/r15-c/*.md` docs** — the r15-C set is recovered inside `.planning/r18-b-model/references/` for the audit trail; the r15-a set is left to R18-A.

Root-cause hypothesis: the r15-D branch merged master with a "keep both track ignore rules" gitignore-driven resolution, which appears to have removed files matching its ignore patterns from the merged tree — silently reverting two already-merged tracks.

Recommendation: auditors should run, on future merges that touch `.gitignore`,
`git diff --stat <merge>^1 <merge>` and require deletions to be explainable.
