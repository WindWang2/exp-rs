# GOAL — ExpRS Data Plane, Runtime, Governance & Reproducibility Reliability 4.0

- Repo: `https://github.com/WindWang2/exp-rs` (local: `/home/kevin/projects/rs-studio/main`)
- Branch: `zcode/data-runtime-governance-4`
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-data-runtime-4`
- Base: master @ `58eb196baa` ("fix(tests,core,ci): teardown-order cleanup…")
- Mode: long-horizon autonomous; no dependence on online CI; one final PR into `master`.
- PR title: `fix(runtime): Data Plane, Governance & Reproducibility Reliability 4.0`

## Mission

Harden the project/data/runtime foundation so large workspaces, cache/reuse, crash
recovery, governed metadata, snapshots, lineage and worker execution are
**fail-conservative, durable, bounded and observable**. Correctness priority is
highest: no headline features while data-loss/corruption/identity defects are live.

## Primary ownership

`src/data/**`, `src/runtime/**`, `src/jobs/**`, `src/workflow/**`, governance/
runtime/cache/workflow tests, benchmark + fault-injection harnesses, relevant CLI
project/runtime surfaces. Hot spots (root CMakeLists, CHANGELOG.md, CONTEXT.md,
PROJECT.md, `src/app/main.cpp`) only when strictly necessary, minimal, documented
in DOCS_LEDGER.md.

## In-scope issue cluster (verified open at HEAD, 2026-09-06)

| Issue | P | Milestone | Summary |
|---|---|---|---|
| #746 | P1 | A | Corrupt governance DB → silent v3→v1 project downgrade (governed state lost); cached JSON bleeds across projects |
| #749 | P2 | D | Out-of-band replacement of registered non-chained input never invalidates execution cache |
| #750 | P2 | E | Crash-resume trusts any non-empty file at a Completed step's recorded path |
| #751 | P2 | A | SnapshotService WAL checkpoint is a no-op; hot-WAL DB copied raw |
| #752 | P2 | A | fromProjectJson swallows every store failure; no busy_timeout on read-only connections |
| #753 | P2 | G | ImportCenter cancel ignored during registration phase |
| #754 | P2 | F | Governance runs table hardcodes state="Completed" |
| #758 | P3 | B,C | Store hardening bundle: unchecked step/COMMIT, alias-steal, page-size counts, N+1 prepares, pool eviction vs shared objects, lineage deletion direction, const_cast UB |

Out of scope (other epics): #747/#748/#755/#756/#757 (plugin/SDK), #759 (temporal),
#760 (docs, partially addressed here via PROJECT.md refresh).

## Acceptance criteria (contract)

- No unresolved P0/P1 from the governance/runtime/cache/workflow cluster.
- Project save cannot silently destroy governed state; snapshots consistent; run
  state truthful; cache/resume cannot knowingly consume foreign/stale bytes; CAS
  object lifetime reference-safe; bulk paths prepared/batched; cancellation stops
  work; runtime bounded + observable; stress without full materialization.
- All new/changed behavior tested; targeted suites green locally; docs current;
  `git status` clean; PR opened with evidence-backed claims only.
