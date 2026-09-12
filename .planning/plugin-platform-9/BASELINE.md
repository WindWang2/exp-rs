# Plugin Platform 9.0 — Baseline (verified against latest origin/master)

Branch under development: `feat/plugin-platform-9`
Worktree: `/home/kevin/projects/rs-studio/exp-rs-plugin-platform-9`
Baseline SHA: `origin/master = f316dfdbb47e39ccae045b202d7d38606f6c1b0e`
("fix(issues): resolve all 30 P1/P2 issues (#853-#882)")
Baseline date: 2026-09-12

## 1. Repository / branch state

- `git fetch --all --prune` executed; remotes: `origin` (WindWang2/exp-rs), `itk-upstream`.
- Latest `origin/master`: `f316dfdbb4`. Recent 45 commits reviewed; the 8.0 wave
  (#837–#847) is fully merged, followed by two issue-fix commits (#848–#882).
- **Open PRs (4)** — all parallel 9.0 tracks, none touching plugin ownership:
  | PR | Branch | Scope |
  |---|---|---|
  | #883 | feat/scientific-algorithms-9 | raster semantics remediation |
  | #884 | feat/model-runtime-multimodal-9 | CUDA lane, NVML |
  | #885 | feat/spatial-scientist-harness-9 | typed actions |
  | #886 | feat/scientific-mlops-9 | data versions, splits |
- **Open issues: 0.** `gh issue list --state open --json number --jq length` → 0.
  The last 45 issues (#838–#882) are all CLOSED; #848–#852 (P0) and #853–#882
  (P1/P2) were fixed by the two head commits of master. None of the closed
  issues is plugin-platform-scoped (they concern terrain flow, selection
  context, vector writer, task center, data manager, workbench, help,
  cartography, operators, SAR/hydrology).
- **Remote branch triage** (`git rev-list --count origin/master..<branch>`):
  - `feat/plugin-platform-8`: 0 ahead / 32 behind → **merged residue** (PR #844).
  - `feat/plugin-isolation-runtime-5`: 0 ahead / 251 behind → **merged residue**
    (PR #830).
  - `feat/model-runtime-multimodal-9` (12), `feat/scientific-algorithms-9` (9),
    `feat/scientific-mlops-9` (9), `feat/spatial-scientist-harness-9` (6):
    genuinely ahead = the four open PRs above; none modifies
    `src/plugins/`, `src/sdk/exprs/` or plugin CLI surfaces (verified by file
    ownership below and by `git diff --name-only origin/master...origin/<br>`
    spot checks).
  - All other remote branches are 7.x/6.x/5.x residue or CI fix branches,
    already merged (0 ahead).

## 2. Plugin platform state on master (8.0 delivered, verified in code)

Ownership directories and their head files (baseline line counts):

- `src/sdk/exprs/` — Qt-free plugin SDK (authority for manifest/protocol/SDK):
  - `host_protocol.h` (protocol **1.1**, MAJOR=1 MINOR=1)
  - `ipc_frame.h` / `ipc_channel.{h,cpp}` / `ipc_envelope.*` / `ipc_stream.*`
    (bounded length-prefixed framing, E6002/E6003 typed violations, event
    queue cap 1024 with drop counter, monotonic downward `lowerFrameCap`)
  - `plugin_manifest.{h,cpp}` (manifest v1 + runtime/access/quotas/package/
    conformance fields)
  - `plugin_capabilities.{h,cpp}` (access parsing, `pathIsWithinRoot`),
    `plugin_permissions.*`, `plugin_quotas.{h,cpp}` (clamped quotas, honest
    per-field enforcement matrix in header docs)
  - `plugin_diagnostics.{h,cpp}` (stable E1xxx…E6xxx codes)
  - `plugin_package.{h,cpp}` (staged install + checksum + rollback),
    `plugin_registry.*`, `plugin_loader.*`, `plugin_validator.*`,
    `plugin_discovery.*`, `plugin_ui_schema.{h,cpp}` (validated declarative
    UI, hard caps), `plugin_ui.h` (Qt, host-locked), `plugin_record.*`
- `src/plugins/host/` — out-of-process host (launcher + worker):
  - `plugin_host_session.{h,cpp}` (FIFO `ConcurrencyGate`, kill ladder,
    poison-on-timeout, drain-kill, POSIX setpgid + group SIGKILL, Windows job
    object w/ memory + CPU-rate + ActiveProcess limits, FD_CLOEXEC hygiene,
    allocation-free fork child, RLIMIT_AS pre-exec)
  - `plugin_host_process_runtime.{h,cpp}` (restart policy: maxRestarts=3 /
    60 s window, respawn, UI describe/invoke)
  - `plugin_host_proxies.cpp` (operator/agentTool/dataProvider/modelRuntime
    proxies; one-shot bounded recovery)
  - `plugin_host_worker_main.cpp` (protocol 1.1 worker: handshake-first,
    bounded ExecutionPool ≤ 8, per-id cooperative cancel + broadcast −1,
    progress coalescing ≥ 20 ms, dataProvider/modelRuntime v1.1
    implementations, declarative UI probe via optional entry point)
- `src/plugins/framework/` — in-process runtime host, lazy adapters,
  external tool operators, UI schema host, agent tool provider, data
  provider registry, model runtime bridge, execution barrier.
- `src/cli/cli_commands.cpp` — `plugin list|validate|doctor|test|enable|
  disable|install|uninstall|inspect`; conformance kit with 13 PT_* checks
  (PT_MANIFEST/COMPAT/CONTAINMENT/LOAD/REGISTER/HOST_LAUNCH/EXECUTE/CANCEL/
  CONCURRENCY/RESTART/UI_SCHEMA/REVOKE/ROUNDTRIP).
- Tests: `tests/test_plugin_host_process.cpp`, `test_exprs_ipc.cpp`,
  `test_plugin_capabilities.cpp`, `test_plugin_ui_schema.cpp`,
  `test_plugin_ui_schema_host.cpp`, `test_plugins_runtime_host.cpp`,
  `test_plugin_manifest.cpp`, `test_plugin_execution_barrier.cpp`,
  `test_exprs_plugin_system.cpp`, `test_exprs_plugin_loader.cpp`,
  `test_plugin_host.cpp`, `test_python_plugin_{host,manager}.cpp`,
  `test_cli_commands_json.cpp`; fixtures `hello_plugin`, `isolation_plugin`.
- Docs: `docs/plugins/{README,manifest-v1,permissions,capabilities,
  host-process,isolation,declarative-ui,packaging,external-process}.md`.
- 8.0 planning evidence: `.planning/plugin-platform-8/` (FINAL_REPORT,
  CAPABILITY_MATRIX, REVIEW_LOG with dispositions).

## 3. 8.0 declared follow-ups (input requirements for 9.0)

From `.planning/plugin-platform-8/FINAL_REPORT.md`:
1. Frame-cap negotiation enforced for plugin→host frames; **per-direction
   split for host requests is a noted follow-up (A-P2-3)** → M0 of this track.
2. Windows/macOS lanes not locally executable: job-object enforcement and
   macOS behavior documented, not re-verified → keep honest not-run labels.
3. Declarative UI shell placement = workbench track seam (NOT ours).
4. `test_exprs_ipc` one transient under host load ≈ 40 → timing-sensitive
   assertions documented; re-verified below.

## 4. Baseline build evidence (this worktree, Debug, -j≤4)

Recorded in TEST_MATRIX.md as runs complete. Configure:
`cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
-DENABLE_LOCAL_BUILD_SHORTCUTS=ON` (system Qt6 at /usr/lib/cmake/Qt6,
GDAL at /usr/lib/cmake/gdal — mirrors the 8.0 lane).
