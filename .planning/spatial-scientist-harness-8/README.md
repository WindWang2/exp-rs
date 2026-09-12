> Reconstructed 2026-09 from ADR 0144 + commit evidence; the original planning files were never committed (gitignore whitelist omission). Do not treat as contemporaneous artifacts.

# spatial-scientist-harness-8 — planning records (reconstructed)

The harness-8 track never had committed planning files. The author wrote
GOAL/PLAN/BASELINE-style records locally during the track, but `.gitignore`
whitelists `.planning/*` per track and the line
`!.planning/spatial-scientist-harness-8/` was missing, so `git add` silently
skipped them. Commit `ef9f1c45` ("docs(harness): 8.0 records ...") even
announces "baseline, ownership, architecture, capability matrix, milestones"
in its message, but its diff contains only 7 non-planning files.

What exists here now is a minimal, honest reconstruction from authoritative
evidence only:

- `GOAL.md` — derived from ADR 0144 Context/Decision
  (`docs/adr/0144-harness-8.md`).
- `PLAN.md` — ADR decisions mapped onto the commits that actually landed
  (merged as PR #842, 2026-09-11).
- `FINAL_REPORT.md` — outcome from PR #842, the CHANGELOG 8.0 section, the
  eval corpus, and successor ADR 0145; plus an explicit list of what is NOT
  reconstructable.

Deliberately NOT fabricated: `BASELINE.md`, `PERFORMANCE.md`,
`REVIEW_LOG.md`, `MILESTONES.md`, `OWNERSHIP.md`, `ARCHITECTURE.md`,
`CAPABILITY_MATRIX.md`, `DOCS_LEDGER.md`, `TEST_MATRIX.md` — those would
assert contemporaneous artifacts that never existed in-repo.

References: ADR 0144 · PR #842 · CHANGELOG "Pi Spatial Scientist Harness
8.0 (goal series, ADR 0144)".
