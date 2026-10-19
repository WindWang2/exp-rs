# R18-C Final Report — Pi/MCP Agent Harness: tool contracts, workflow closure, scientific-decision consistency

## Baseline & branch
- LIVE baseline at start: `origin/master = 2d2881631` (task snapshot `7825c1ebd` was stale); at delivery `a60c8831c` (#1513 merged mid-run — branch rebased cleanly, zero conflicts).
- Worktree `/home/kevin/project/exp-rs-r18-c-agent`, branch `agent/r18-c-pi-mcp-workflow`.
- 11 atomic commits: 1 restoration (verbatim from a8d5cd510), 1 restoration-completion, 5 behavioral fixes, 2 review-fix rounds, 1 docs.

## Headline result
**Merge 29596c913 (inside PR #1517) silently reverted already-merged #1516 (r15-D) content** — every conflicted file was resolved to the r15-E side although the r15-E branch had zero own changes to them: Pi bridge failed-start teardown, StdinReader clean-EOF/4MiB-boundary/badbit framing, Windows junction sandbox escape fix, DataStateSnapshot typed-leaf guards, checkpoint temp-file shape classifier + poison-document typed refusal, plugin worker write-gate fail-closed, agent-ops non-object guard — and 230 lines of their regression tests (which is why nothing caught it). Discovered via the audit's "merge-after-gap" hypothesis; proven by blob-id archaeology (29596c913 took parent-1's blob for all 6 conflicted files); restored verbatim; RED reproduced on the reverted tree three ways (behavioral ×2, compile-level ×1); GREEN ×2 after restore.

## Behavioral fixes (each RED → minimal fix → GREEN ×2)
1. **C03-a progress contract**: TaskCenter progress is a 0..1 fraction (completion stamps 1.0; every producer verified). The notifications/progress relay divided by 100 (completed task announced 0.01/total 1.0) and the 5-point limiter threshold was unreachable in fraction space — ALL intermediate progress silently suppressed. Relay now true-scale; limiter 0.05; the one 0..100 test fixture fixed.
2. **C05-a timeout symmetry**: the Pi bridge's request timeout never sent notifications/cancelled (abort did) — timed-out executions kept burning CPU server-side. Now fires the same cancel; interval injectable for tests.
3. **C05-b cancel id namespace**: cancel maps keyed by id.toString() collapsed numeric 5 and string "5" — one request's cancel fired the other's task and consumed its entry. Keys now typed (n:/s:), 17-significant-digit injective formatting after R2.
4. **C01 discovery authority**: tools/list silently rewound non-numeric cursors to page 0 → typed -32602 (and INVALID_PARAMETER tool-errors across list_algorithms/list_operators/list_tools/search_tools/search_algorithms); get_tool_schema vs tools/list disagreed for 11 tools (raw-vs-normalized schemas; dual-source bare aliases list_layers/describe_dataset/get_lineage) → projection renders normalized schemas, detail resolves Meta/Data through the same projection first.
5. **C04 observability**: failed ghost-checkpoint unlink now logged (was indistinguishable from healthy cleanup).

## Verification (all local; CI broken repo-wide since 2026-10-11 — issue #1491)
- 12 C++ suites GREEN ×2 (~17k assertions/run), incl. new test_r18_c_* (3 files) and restored r15-D suites; pi 39/39 ×3.
- Full matrix, exit codes, RED chains, NOT_EXECUTED items: TEST_RESULTS.md.
- Performance: no perf claims made → no measurements claimed (PERFORMANCE.md kept as register; the only perf-relevant change, get_tool_schema's projection pass, is recorded as known cost F4).

## Independent review
- R1 (fresh context): READY, 1×P1 + 2×P2 → P1 fixed (catalog branch keeps rich metadata; typed cursors ×4), P2-pi documented as known risk.
- R2 (second fresh context): READY, no P0/P1; verified restores byte-exact, #1513 interaction none, falsification attempts V1–V7 all passed; F1/F2/F3/F5 adopted and fixed same-day.
- Residual known risks (documented, not blocking): pi rpc-id reuse across bridge generations (pre-existing; needs generation-scoped keys); get_tool_schema O(N) projection per call (caching follow-up); RateLimiter backward-move suppression (pre-existing).

## Ownership
`git diff --name-only origin/master...HEAD`: 26 code files + tests + `.planning/r18-c-agent/*`. All within OWNERSHIP.md zones; shared files touched: `tests/CMakeLists.txt` (three new independent test-target blocks — no alternative existed; re-scanned for overlap before commit) and the restored files themselves (the restoration IS the tracked content of #1516). Cross-track: CT-1 (R18-D cartography raw schema missing root type), CT-3 (data_platform pageCursor cursor typing, outside ownership), CT-2 (CI infra).

## Maintenance suggestions
- Add a repo-level guard: a post-merge check that no merge commit's resolution equals one parent's blob for files the OTHER parent changed exclusively (the 29596c913 class).
- Follow-ups: generation-scoped cancel keys; data-platform pageCursor typing (CT-3); cartography raw schema root type (CT-1); cache the surface projection for get_tool_schema.
