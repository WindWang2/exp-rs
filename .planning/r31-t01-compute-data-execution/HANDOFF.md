# Handoff

No cross-track request has been needed yet.

## Standing compatibility contracts

- T02 consumes existing JobEngine→TaskCenter→dispatcher/Result APIs. T01 must preserve #2063 Python approval, staging/path, identity, owner-thread, conflict and duplicate-commit behavior; no second committer is permitted.
- T03 consumes backend snapshots/events/APIs. No T01 change requires an unmerged UI or mapspec/layout implementation.
- Shared schemas, identities, error codes, cancellation and output contracts remain backward compatible; new JSON fields should be optional.

## Current blocker ownership

None. If another owner's file becomes necessary, record exact path, current/expected contract, minimal input, owner, compatibility option and verification here, then continue independent T01 work rather than expanding ownership silently.

## R31-T01 handoff (2026-11-16)

- Draft PR: https://github.com/WindWang2/exp-rs/pull/2071 (base `master` @ `fa5495fd70`, head `zcode/r31-t01-compute-data-execution-20261115T135418Z` @ `b95ff9b8`).
- Delivered and verified: WP02–WP08, WP11, WP15–WP18 (CPU/HTTP) + WP06/WP14/WP19 closed with suite evidence; WP09/WP10/WP12/WP13 NOT_EXECUTED with evidence in `RESIDUAL_MATRIX.md`.
- Blocking gate: independent R1/R2 review. Every `Agent` launch failed with the provider's monthly usage limit (3 attempts across two turns). A self-review is in `REVIEW_R1.md` (finding S1 fixed; S2–S4 disclosed). Retry the review when the quota resets; the PR stays Draft until an independent reviewer signs off.
- Verification on the final HEAD: 42-target build exit 0; 42/42 suites exit 0 (343 cases / 118,692 assertions); WP02 RED→GREEN recorded (exit 42 with 18 failed assertions → exit 0 with 99).
- Local-only artifacts stay in `.scratch/` (upstream snapshots, logs). Nothing outside this worktree was modified.
