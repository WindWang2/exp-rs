# Plugin Platform 9.0 — Ownership & conflict avoidance

## Owned by this track (full write authority)

- `src/sdk/exprs/` — plugin SDK: manifest, protocol, IPC framing/channel,
  capabilities, permissions, quotas, diagnostics, package, registry, loader,
  validator, discovery, ui schema, record, version, host_protocol.
  (Exception: `src/sdk/exprs/external_process.*`, `path_policy.*`,
  `workflow_*` are shared SDK surfaces — touch only when a plugin-facing
  contract requires it, minimal diffs.)
- `src/plugins/host/` — launcher session/runtime/proxies + worker binary.
- `src/plugins/framework/` — in-process runtime host, adapters, barriers,
  external tool operator, UI schema host, agent tool provider, data provider
  registry bridge, model runtime bridge.
- `src/plugins/processing/`, `src/plugins/layer_tree/` — sample plugins
  (touch only if a contract change forces it).
- `docs/plugins/**` — plugin documentation.
- `tests/test_plugin*.cpp`, `tests/test_exprs_ipc.cpp`,
  `tests/test_exprs_plugin_{loader,system}.cpp`, `tests/fixtures/*plugin*`
  and their CMake registrations in `tests/CMakeLists.txt` (narrow, appended
  blocks only).
- CLI `plugin` subcommand section inside `src/cli/cli_commands.cpp`
  (surgical edits only; that file is shared with other tracks).

## Shared seams (append/minimal-increment, defer to milestone end)

- `tests/CMakeLists.txt` — append new targets/fixtures in the existing
  "plugin/SDK/CLI test matrix" block.
- `src/cli/cli_commands.cpp` — only the `plugin …` subcommand section.
- `CHANGELOG.md` — one entry per milestone, batched.
- `src/sdk/exprs/version.h` — bump only additive SDK axes (protocol minor).
- Top-level `src/plugins/CMakeLists.txt`, `src/sdk/CMakeLists.txt` — only
  when adding new files, minimal.

## Not owned (must not modify)

- `src/app/`, `src/gui/`, `src/ui/` — workbench UI placement (Track 7 /
  professional-workbench-9). Declarative UI shell placement stays their seam.
- `src/workflow/`, `src/jobs/` — scheduler / TaskCenter / JobEngine.
- `src/agent/`, `pi/` — Pi agent loop (agent tools only *contribute* tools).
- `src/model/`, model runtime core — we bridge through existing registries.
- `src/geospatial/` — geospatial I/O authority.
- `src/processing/`, `src/operators/` — read-only dependencies
  (`rs_operator.h`, `rs_operator_context.h` contract headers are linked, not
  modified; the plugin ABI mirrors `processing::PortDescriptor` but does not
  define it).

## Parallel-track conflict check (2026-09-12)

Active open PRs: #883 scientific-algorithms-9, #884 model-runtime-multimodal-9,
#885 spatial-scientist-harness-9, #886 scientific-mlops-9. `git diff
--name-only origin/master...origin/<branch>` for each shows zero overlap with
the owned directories above. Local worktrees
`exp-rs-model-runtime-9`, `exp-rs-scientific-*`, `exp-rs-exec-concurrency-9`
exist; the execution-concurrency-9 worktree (`feat/execution-concurrency-
lifecycle-9`) touches `src/workflow`/`src/jobs`, not `src/plugins`.
Decision: proceed; shared-file edits (`tests/CMakeLists.txt`,
`cli_commands.cpp`, `CHANGELOG.md`) are deferred to milestone-end minimal
increments to keep rebase surface small.

## Authority boundaries preserved (no second runtime anywhere)

- Pi remains the only agent loop; agent tool contributions stay tool-only.
- `WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor/Operator`
  remains the only execution chain; plugin operators enter through the
  existing `AtomicAlgorithmRegistry`/`RSOperatorRegistry` lazy adapters.
- QGIS stays the only rendering authority; declarative UI remains
  schema/event IPC, never QWidget IPC.
- Existing Dataset/Experiment stores and registries stay authoritative;
  this track adds no new store and no new scheduler.
