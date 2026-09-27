# REVIEW_LOG — Track 10: Workflow Durability R4

Independent adversarial review (read-only, evidence-carrying) of the full
branch diff before the PR. Reviewer: dedicated subagent (1 of the ≤3 slots).
Axes: Standards (C++20/Qt6, process safety, no redundancy) and Spec (injection
points genuinely process-level? recovery assertions independently reconciled?
whitelist respected? §3.2 floors met? — with a specific eye for
"in-process simulation masquerading as process death").

## Findings (review completed 2026-09-27)

Overall: PASS for PR conditional on 2 P1 fixes. 0 P0. 1 subagent used (of ≤3).

- **P1-1 (fixed)**: runId capture was a one-shot stdout drain racing the kill
  barrier; for run-exit-at the child can `_exit` before its main thread ever
  prints. Fixed in `workflow_crash_injector.cpp`: stdout is accumulated during
  the barrier poll, the RUN line gets a bounded 5s grace poll after the
  barrier, and `resolveRunId()` falls back to the fresh scratch dir's single
  checkpoint file on disk. All call sites updated.
- **P1-2 (fixed)**: ADR 0177 claimed "all verified by real SIGKILL" — wrong
  for the crafted IP-4b window and the `_exit`-based IP-4a/5b, and the
  Consequences section extended the claim to cache cases. Reworded: per-window
  verification language, the crafted window carved out, cache contracts
  described as in-process API tests.

P2 dispositions:
- forceSetState -> transitionTo in the empty-DAG guard (consistency with the
  neighboring branch): FIXED.
- IP-5b: separate `step3Runs >= 1` assertion (no silently skipped step): FIXED.
- IP-5a: positive barrier-time state pin (`Cancelling` or `Canceled` on disk
  before the kill): FIXED.
- cancel lane: refusal tightened to the ownership-gate message; resumed
  second step now counted: FIXED.
- cache_e2e cancelled-step case: shadow executor now writes bPath bytes
  before blocking (strongest seeding-gate shape); dead bStarted removed: FIXED.
- two-view case: renamed to "two coherent views" (view 1 is in-memory —
  claimed honestly now); coordinator dir reset moved into RAII: FIXED.
- incremental-cache comment self-contradiction + title ("moves the key"):
  FIXED.
- drive-by WHOLE_ARCHIVE conversions of 7 pre-existing test_agent_loop_*/
  test_agent_session_adapter targets: REVERTED to master's plain linking
  (they were never broken).
- missing <QFileInfo>/<cstring>/<unistd.h> includes: FIXED. Read-only-dir
  case guarded with geteuid() != 0: FIXED. Helper barrier() now reports
  write failures on stderr: FIXED. Chinese comments translated: FIXED.
- EVIDENCE §4 claim softened (committed-set reconciliation is disk-based;
  resume-verdict asserts are in-memory): FIXED.

Backlog (not blocking, recorded for future tracks):
- MCP INVALID_PIPELINE message conflates the empty-document refusal with
  parse errors (src/agent domain).
- Product question: resume of a refused zero-step run upgrades Failed ->
  Completed with zero executed work (#1078a path) — document or refuse.
- `eventually()`/`waitFor()` triplication across fixtures; containsPath
  wrapper; cancel-lane relative output paths.
- Upstream: test_mcp_server's run_workflow cases need HOME isolation (they
  accumulate state in the shared default checkpoint dir and degenerate).
