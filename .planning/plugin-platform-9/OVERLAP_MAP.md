# Plugin Platform 9.0 — Overlap map with parallel 9.0 tracks

Four open PRs (all 9.0 siblings) + several local worktrees are active.
File-level overlap of THIS track with each (based on
`git diff --name-only origin/master...origin/<branch>` spot checks and
declared ownership):

| Track | Their core dirs | Overlap with us | Risk | Protocol |
|---|---|---|---|---|
| #883 scientific-algorithms-9 | src/processing, src/operators | `rs_operator*.h` contract headers (read-only link for us) | none — we never write them | none needed |
| #884 model-runtime-multimodal-9 | src/model, model runtime core | `ModelRuntimeRegistry` seam (they own the registry; we register factories through it) | low — additive registrations only | keep our bridge consuming the existing registry API |
| #885 spatial-scientist-harness-9 | src/agent, pi | agent tool catalog (they consume tools; we contribute tool objects only) | none | keep tool-only contributions |
| #886 scientific-mlops-9 | src/dataset, src/experiment | none | none | — |
| execution-concurrency-lifecycle-9 (local worktree, no PR yet) | src/workflow, src/jobs | none (our session gate is plugin-local, not a second scheduler) | none | — |
| professional-workbench-9 | src/app, src/gui, src/ui | declarative UI **shell placement** (their seam; we own schema/event protocol only) | none by construction | document the seam in declarative-ui.md |

## Shared-file discipline

Files more than one track may plausibly touch, and our policy:

- `tests/CMakeLists.txt` — append-only block at the existing plugin matrix
  section; no reordering of others' targets. Defer to end of each milestone.
- `src/cli/cli_commands.cpp` — surgical edits confined to the
  `plugin …` subcommand section (single contiguous region).
- `CHANGELOG.md` — prepend one dated section per milestone batch.
- `src/sdk/exprs/version.h` — protocol minor bump is additive (1.1 → 1.2);
  SDK API/ABI versions unchanged (no interface break).

If a conflict appears at rebase time (another track rewrote the plugin CLI
region or the test-matrix block): re-apply ours on top of theirs, never
revert theirs; our edits are additive by construction.

## Worktree isolation

Independent worktree `/home/kevin/projects/rs-studio/exp-rs-plugin-platform-9`
with its own `build/` directory; build parallelism capped at 4 (ninja default
edges); no shared build artifacts with sibling worktrees. Master checkout in
`~/projects/rs-studio/main` stays read-only for this track (no commits, no
builds triggered from it).
