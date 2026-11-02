# R27-T04 PR / Branch Collision Matrix

Checked 2026-11-02T16:2xZ: `gh pr list --state open` → **empty**. All listed PRs (#1528–#1731) are MERGED.

| Potential collision source | Files | Status | Mitigation |
|---|---|---|---|
| Sibling R27 worktrees (local only, unpushed): t01 science, t02 data-io, t03 agent-security | disjoint domains (science numerics; runtime/data IO; agent_ops security) | No overlap with my whitelist files | None needed; do not touch their worktrees |
| T2 (#1765 co-owner) | `src/geospatial/**` safe COG API | Already on master (`process_network_policy`, range cache) | I only *call* the existing API from `src/app/main.cpp` / dialog; no geospatial edits |
| T5 CI/build maintenance | `tests/CMakeLists.txt`, root `CMakeLists.txt` | Shared protected resource | Append-only block with unique `# ── R27-T04` markers at end; re-3-way-check against origin/master before PR |
| #1805 P3 items F6 (`qgsmacnative.mm`) | platform native | File lives in `src/native/` (whitelisted UI/platform part) but is macOS-only | Code fix allowed; testing will be marked NOT_EXECUTED (no macOS host) |

Conflict-matrix re-check is mandatory again at PR-readiness step (§1.9).
