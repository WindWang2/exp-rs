# PR_COLLISION_MATRIX — r22-teaching vs sibling open PRs (@2026-10-25)

| Open PR | Shared files with our planned edits | Conflict risk | Rule |
|---------|-------------------------------------|---------------|------|
| #1674 r21-E | tests/CMakeLists.txt (it appends 4 test registrations; we append ours) | LOW (both append-only at distinct points) | append one block at end; re-diff before push; if #1674 merged first, rebase |
| #1674 r21-E | src/agent/output_verifier.*, src/faultlab/*, src/verify_adapters/lab_verify_composer.* | NONE this round — we do not edit them (F3 is review-only) | read-only; findings → CROSS_TRACK_FINDINGS.md |
| #1673 r21-B | none | none | — |
| #1675 r21-C | none | none | — |
| #1676 r21-A | none | none | — |
| #1677 r21-D | none | none | — |

Our edit set (planned): src/teaching_admin/batch_assessment.{h,cpp}, src/app/teaching_admin/teaching_admin_dock.cpp,
src/teaching_admin/feedback_pack.{h,cpp}, src/teaching_admin/script_adapters.{h,cpp}, src/lab/session_state.cpp
(behavioral NaN fix only), tests/test_r22_teaching_*.cpp (+ tests/CMakeLists.txt append).
No sibling PR touches any of these except tests/CMakeLists.txt (append-only, see above).

Historical merge-loss references guarded: r15-E fail-closed loader (batch_assessment.cpp:170-182),
r18-E D3/D4 stamps (batch_assessment.cpp:590-629, 660-667) — new digest work must not weaken these.

## Update 2026-10-26 pre-push: new sibling R22 PRs appeared

| New PR | Shared files | Nature | Rule |
|--------|--------------|--------|------|
| #1708 r22-infra | `.gitignore` (both append at EOF), `src/lab/session_state.cpp` (their #1696 `<algorithm>` fix vs our #1678 behavioral fix — different hunks in the same include/Numeric areas), `tests/CMakeLists.txt` (both append) | append-only / distinct hunks; textual conflict possible on second merge, semantically trivial (keep both) | no action now; rebase + keep both sides when either merges; infra owns #1696, we own #1678 |
| #1709 r22-inference | `tests/CMakeLists.txt` (both append at EOF) | append-only | same |
| #1710 r22-data | none | none | — |

master still 911e0b17a at push time — no rebase needed yet.
