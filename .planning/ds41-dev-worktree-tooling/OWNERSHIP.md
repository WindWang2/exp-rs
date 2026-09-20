# OWNERSHIP — ds41-dev-worktree-tooling (Track D5)

## Writable by this track (exclusive scope)

| Path | Purpose |
|---|---|
| `scripts/dev/**` | New dev-orchestration tools (Python 3, stdlib only) and their unit tests |
| `docs/development/**` | New agent developer-experience guide (this directory does not exist yet) |
| `.planning/ds41-dev-worktree-tooling/**` | This track's planning notes (markdown only, via the `.gitignore` whitelist added in the first commit) |

## Read-only

| Path | Owner / authority |
|---|---|
| `src/**`, `tests/**` (C++ suite), `CMakeLists.txt`, `CMakePresets.json` | Product tracks; `ds41-build-portability` owns build/CMake changes |
| `.agents/**`, `docs/agents/**` | Agent-prompt conventions owned by the prompt-hygiene reviewers; this track reads them and may only add the new `docs/development/**` guide, never edit them |
| `scripts/**` outside `scripts/dev/**` | Track-specific build/lab tooling owners |
| `.planning/<other-track>/**` | Other tracks' planning content (noted where their `.gitignore` blocks are adjacent to ours) |

## Shared, append-only (minimal integration)

| File | This track's change | Conflict handling |
|---|---|---|
| `.gitignore` | Append one 4-line whitelist block for `.planning/ds41-dev-worktree-tooling/` (same pattern as `ds41-range-cache-msvc` and `ds41-build-portability`) | Both tracks append at EOF; on rebase keep both blocks, no semantic conflict |

## Hard boundaries (from the brief)

- Tools must **never** delete branches, close/merge PRs, force-push, or write to a `master`
  checkout. WP3/4/6 are fail-closed by construction and covered by tests.
- Default no change to product source: the whole track is `scripts/dev/**` + `docs/development/**`
  + planning notes + one append-only `.gitignore` block.
- Only the main agent may run builds; the tools themselves perform no C++ compilation. The
  resource guard is a wrapper around external commands and is proven with stub commands, so no
  QGIS/OTB/ITK rebuild is part of any Oracle.
