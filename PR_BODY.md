# fix(plugin): harden host-worker trust, IPC lifetime and process contracts

Baseline: `origin/master` = `2caac836c4bfb7f62a317d19cf5257c8a97f8388` (re-fetched at start; master had not moved). Branch `agent/glm53-plugin-sdk-trust`, worktree `../exp-rs-worktrees/glm53-plugin-sdk-trust`.

## What was read before writing

- Latest `origin/master` (tip `2caac836c`, PR #1032) and the last 60 commits; all remote branches/worktrees; `gh pr list` open + merged.
- Open issues #1036, #1037, #1038, #1039, #1040, #1041, #1056 in full.
- Deep review roll-up #1037 (PRs #1030–#1032) to confirm none of its confirmed findings touch this boundary.
- Open PRs #1057/#1058/#1059 (geospatial HTTP, range-cache, app pipeline) are disjoint from every file here.

Dedup result: no open PR covered any sub-problem of this track, so all work packages were implemented (no skips).

## Scope

Writable: `src/plugins/**`, `src/sdk/exprs/plugin_*`, `src/sdk/exprs/ipc_stream.*`, `src/sdk/exprs/external_process.*`, `src/sdk/exprs/json_reader.h` (new), their tests, and plugin docs. Reserved files (`src/app/**`, `src/agent/mcp_server*`, Geo/Data/Processing) were not modified; the only cross-layer seam used is the existing `UiInvokeDelegate` contract (header shape unchanged, response shape documented and normalized in the renderer).

## Issue mapping

| Issue | Fix |
|---|---|
| #1036 | `IIpcStream` is the single owner of both pipe ends; idempotent single-winner `close()`; `IpcChannel::close()` marks → joins the reader → releases the stream under `mWriteMutex`; `mCloseMutex` serializes concurrent closers; reader self-close path handled. Regression: steady-state descriptor delta == 0 and session handle-count flatness across shutdown/kill/respawn. |
| #1038 | New `exprs/json_reader.h` typed accessors; guards applied to manifest/package/workflow/index readers and the worker registration report; non-object/ wrong-typed input becomes a typed diagnostic (19-case manifest suite, workflow hostile cases, non-object sbom install case). |
| #1039 | Host re-validation in `describeUiSchema` (typed E5005 + details, validated copy only) and in `attachPluginSchema`; new `maxTotalControls` (512) closes the compounding per-group cap; renderer depth/control budget + guarded field reads; exception-safe delivery/GUI paths. Adversarial worker fixture covers deep nesting, compounding tree, control flood and wrong types. |
| #1040 | `exprs::uiStateFromInvokeResponse` defines the canonical `{ok, response:{state}}` extraction; renderer uses it; real-worker runtime test and Qt widget test on the production shape; hostile state values ignored. |
| #1041 | POSIX `waitpid(WNOHANG)` liveness + bounded `postExitDrainGraceMs`; no retroactive timeout after exit; surviving group reaped with `kill(-pid)` only; child resets SIGTERM/SIGINT/SIGPIPE before exec. |
| #1056 (plugin/IPC subset) | Not applicable: the listed items live in `python_worker_provider.cpp`, `http_provider.cpp`, `tile_spec.h` — outside this track's owned files. |

Additionally fixed (found by local validation + independent review, both reproduced on the pristine master-built binary where applicable):

- `unloadPlugin` erased the session-entry map while holding that entry's mutex (Debug-CRT abort `mutex destroyed while busy`, silent UB in Release).
- Crash-respawn ladder never executed against the recovered worker (reported "restart policy exhausted" with a live worker); now phase-counted (execute → recover once → execute → typed E6005), which also removes an intermittent full-suite failure.
- POSIX host `SIGPIPE` kill path on a write racing worker death; writer/close descriptor race (closing under an in-flight `writeAll`).
- Worker-controlled `outcome.result[...]` reads in proxies are type-guarded.

## Key design notes

- Trust model: the worker is remote input even when "our" code runs inside it; every worker→host payload is re-validated (handshake, registration report, UI schema, UI responses). See `docs/plugins/trust-boundary.md`.
- Descriptor ownership: stream owns both ends; channel defines release ordering; session never closes parent-side ends itself.
- Type strictness (#1038): known fields are type-checked exactly; wrong types are typed refusals, not coercion — documented in `docs/plugins/manifest-v1.md`.

## Verification

Build (Debug, Ninja, `-j1`, `CMAKE_BUILD_PARALLEL_LEVEL=1`; the shared-host cap was respected; one sibling agent briefly used this track's `build-dev` and was stopped before it could corrupt state):

```
cmake --preset dev-default ... && build_one.cmd --target <suite targets>
```

Windows suites (Debug, real `exprs_plugin_host_worker`, isolation + hostile fixtures, `QT_QPA_PLATFORM=offscreen`), each suite run twice consecutively — both passes green:

| Suite | Result |
|---|---|
| test_plugin_host_process | 288 assertions / 26 cases |
| test_plugin_ui_schema_host | 36 / 4 |
| test_exprs_ipc | 117 / 23 |
| test_exprs_external_process_win | 46 / 8 |
| test_plugin_manifest | 332 / 9 |
| test_exprs_workflow_schema | 23 / 6 |
| test_plugin_ui_schema | 25 / 8 |

`test_plugin_host_process` was additionally repeated 6× to clear the crash-respawn flake.

POSIX suites (WSL, g++ `-std=c++20`, pinned Catch2 from `C:/deps/catch2-src`, no Windows toolchain), each suite run twice consecutively — both passes green:

| Suite | Result |
|---|---|
| test_exprs_external_process (incl. new `[liveness]`) | 49 assertions / 11 cases |
| test_exprs_ipc | 117 / 23 |
| test_plugin_manifest | 334 / 9 |
| test_exprs_workflow_schema | 23 / 6 |
| test_exprs_plugin_system (package install) | 104 / 13 |

## Oracle checklist

- [x] hostile-schema / wrong-type / nested-group / oversized-control tests return typed errors, host never crashes (hostile worker fixture + renderer suite + manifest/workflow suites).
- [x] repeated spawn/shutdown/kill/respawn keeps fd/handle count flat (session lifetime case + stream steady-state case).
- [x] real `ui.invoke` contract test proves state update (runtime-level real worker + Qt widget test with the production envelope).
- [x] POSIX descendant-holds-pipe test returns in ~0.5 s with the real exit code and no false `timedOut`; a genuinely hung child still hits the timeout ladder.
- [x] plugin/sdk tests and build targets pass twice consecutively under `-j1` (Windows 7×2, POSIX 5×2).

## Independent review

Adversarial read-only review subagent + 8-axis self-review; findings R1–R8 with dispositions in `REVIEW.md`. The P1 (writer/close descriptor race) and P2s are fixed, not deferred; the manifest-strictness behavior change is documented. `git diff --check` is clean.

## Known pre-existing conditions (not masked as green)

- Catch2's `SIGTERM - Termination request signal` line in the external-process suite is emitted by the forked child (the parent's handler runs inside the kill window) and exists on pristine master; the suite exits 0. The child now resets signal dispositions before exec.
- The hostprocess Debug-CRT teardown abort existed on master and prevented the whole suite from running on this host; it is fixed here because it sits in the owned lifecycle files and blocked validation.

## Resource / concurrency

All builds and tests were run with `CMAKE_BUILD_PARALLEL_LEVEL=1` / `CTEST_PARALLEL_LEVEL=1`; only `-j1`. No CI/CD was awaited or modified; all evidence is local.

## Conflict hotspots for merge ordering

`src/sdk/exprs/ipc_stream.*`, `ipc_channel.*`, `external_process.*`, `plugin_manifest.*`, `plugin_package.*`, `plugin_ui_schema.*`, `src/plugins/host/*`, `src/plugins/framework/plugin_ui_schema_host.*`, `tests/CMakeLists.txt`. The open ds41 PRs (#1057–#1059) do not touch these files.
