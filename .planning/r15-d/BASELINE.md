# R15-D Baseline (Pi/MCP Agent Harness, tool contracts, workflow reliability)

Frozen 2026-10-17 from live `git`/`gh` reads (not from the prompt snapshot).

| Fact | Value |
|---|---|
| origin/master at start | `46791bc65c6f552ebbcf723bb91892736d56f319` (merge of #1509) |
| Branch / worktree | `hardening/r15-D-pi-mcp-workflow` / sibling `exp-rs-r15-d-20261017` (not committed paths) |
| Build tree | isolated `build-r15-d/` (MSVC Debug, Ninja, `-j2`), deps reused read-only from an existing vcpkg install |
| Execution mode | `/teamwork-preview` and `/goal-loop` are Antigravity commands and do not exist in this host; the Oracle-Ledger loop (analyse -> RED -> minimal fix -> GREEN x2 -> ledger) was executed by hand. Recorded here as "not installed / equivalent execution". |

## Open PRs at start (re-read live)

| PR | Branch | State | Touches (relevant to D) |
|---|---|---|---|
| #1514 | hardening/r15-A-science-processing | open, 0 reviews | analysis/segmentation, operators/rs_sam_classify, processing/image_fusion, tests - no overlap |
| #1513 | hardening/r15-B-geodata-persistence | draft, 0 reviews | data/, dataset/, experiment/, geospatial/, `src/cli/cli_commands.cpp` - no overlap with D files |

The prompt's snapshot said #1505/#1509/#1512 were open and their files locked; they are all **merged** on master (#1505, #1509, #1512). Their files are therefore not locked, but the diffs were treated as already-fixed ground truth (no re-fix).

## Baseline test results (unmodified tree, Debug, offscreen, `-j1`)

| target | result |
|---|---|
| test_exprs_ipc | 28/28 |
| test_workflow_durability_13 | 9/9 |
| test_mcp_server | 28/30 (2 Windows-environment failures, see ORACLES M1/M2) |
| test_r14_C_mcp_workflow_cancel | 2/2 (process segfaults at teardown, rc 139, pre-existing) |
| test_r14_C_plugin_registration | 3/3 |
| test_agent_ops_core | 47/47 |
| test_execution_plane_9 | 17/18, 1 opt-in stress case skipped |
| `node --test pi/test/` | POSIX (WSL) 35/35; native Windows cannot exec the shebang fake server (pre-existing) |
