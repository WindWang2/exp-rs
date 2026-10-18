# R16-C Ownership

## Track T05 — student lab session & learning state (`R16-C-T05`)

- Writes: `src/teaching/**`, `src/lab/**`, `src/lab_pack/**`, new test files
  named `tests/test_*r16_c_t05*.cpp` (+ minimal append registration in
  `tests/CMakeLists.txt`), docs under `.planning/r16-c-t05/`.
- Forbidden: `src/teaching_admin/**`, `src/grader/**`, `src/help/**`,
  `src/app/**`, `src/processing/**`, any other module.

## Track T06 — teacher batch assessment & classroom management (`R16-C-T06`)

- Writes: `src/teaching_admin/**`, new test files
  `tests/test_*r16_c_t06*.cpp` (+ minimal append registration), docs under
  `.planning/r16-c-t06/`.
- Forbidden: `src/teaching/**`, `src/lab/**`, `src/grader/**`, `src/app/**`.

## Shared contracts (read-only for both, cross-track seam)

- `src/platform/durable_sidecar.*` — single atomic write authority; both tracks
  CALL it, neither modifies it.
- `src/teaching_admin/json_util.h` belongs to T06; T05 does not include it.
- Typed-error / digest / status-authority seams stay as-is: a track may only
  consume the existing API. If a genuinely required new seam is discovered,
  the minimal-narrowest adaptation must be justified in that track's
  IMPLEMENTATION.md with a contract test.

## Process rules

- Two tracks never share a git index: separate worktrees, separate branches,
  separate build dirs (`build-r16-c` inside each worktree).
- Integration (`hardening/r16-c-teaching-classroom`) only receives traced
  cherry-picks; conflicts resolved by master's real semantics, never
  `--ours/--theirs` blanket drops.
- Reviewers are sub-agents that did not implement the code under review.
