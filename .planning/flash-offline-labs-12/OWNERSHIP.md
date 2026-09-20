# OWNERSHIP — flash-offline-labs-12

## Track-owned (primary implementation surface)

- `tools/sample_foundry.h`, `tools/sample_foundry.cpp` — Foundry Catalog (subset/seed/size/scene).
- `tools/generate_sample_data.cpp` — only if it fronts the foundry catalog.
- `data/labs/**` — labspecs, grading rules, packs, pipelines, data-specs (append-mostly; edits to
  calibrate specs allowed, they are track-owned per prompt).
- `packaging/**` — bundle layout, launchers, VERIFY scripts, OFFLINE_BUNDLE.md.
- `scripts/*lab*`, `scripts/*bundle*`, `scripts/offline_smoke.sh` — batch classroom runner, bundle
  build/verify, fixture/doc generation.
- `docs/labs/**` — LABSPEC 2 / Grading 2 / classroom documentation.
- `docs/adr/` — new ADR(s) for LabSpec 2 / catalog / classroom runner (next free number).
- `tests/` — new/updated tests for the above (test_sample_fixtures.cpp and new test files).
- `.gitignore` — append-only whitelist entry for `.planning/flash-offline-labs-12/`.
- `.planning/flash-offline-labs-12/**` — this track's planning notes.

## Read-only (consume, do not modify)

- `src/contracts/**` — Scientific Verification Track property; consume public verification outputs only.
- `src/experiment/**`, `src/cli/lab_report_runner.*`, `src/cli/cli_commands.cpp` — lab engine/CLI
  belong to the teaching-lab/experiment owners. If Grading 2 needs engine changes there, prefer
  implementing rule validation in track-owned code (scripts/loader in tools/) or open an issue;
  only make minimal, justified edits if a shared-file integration is unavoidable, and flag in PR body.
- Core scientific operators (`src/operators/**`, algorithm implementations) — call, never modify.

## Shared append-only

- Root `CMakeLists.txt` / `tools/CMakeLists.txt` / `tests/CMakeLists.txt` — add targets append-only.
- `.gitignore` — append-only.
- `docs/adr/` — new files only; never renumber existing ADRs.

## Conflict hot spots (from merged-PR history)

- #1111 touched `packaging/bundle/*` launchers (`--out=` contract) — re-read before editing.
- #1107 touched foundry determinism — read `tools/sample_foundry.cpp` current state before edits.
- `tests/test_env_doctor.cpp` is dirty in the MAIN checkout (not this worktree) — if master moves,
  rebase may surface conflicts there; keep this track's edits to it minimal.
