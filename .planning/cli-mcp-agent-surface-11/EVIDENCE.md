# EVIDENCE — cli-mcp-agent-surface-11

证据政策：每条能力断言映射到 本地命令 + exit code，或显式 not-executed。

## Phase 0（审计与落盘）

- `git fetch origin --prune && git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
- `gh pr list --state open` → #1008 (DIRTY), #1009 (UNSTABLE)；`gh pr diff <N> --name-only` 已记录 PARALLEL_OWNERSHIP.md
- `gh issue list --state open` → #1001–#1007
- Worktree 创建：`git worktree add ../exp-rs-cli-mcp-agent-surface-11 -b zcode/cli-mcp-agent-surface-11 origin/master` → exit 0，HEAD=a5b11b7f10
- Skills 存在性：见 PLAN 附录（下方追加）
- OUT_OF_SCOPE 登记：issues #1001（io:clip CRS 误用）、#1002（workflow fail-open）、#1003（dataset null 列）、#1004（dataset:qa scan_capped）、#1005（georef 异常路径）、#1006（workflow soft-default）、#1007（dataset:qa CRS 审计）——全部为域语义缺陷，与本 surface track 零文件交集；不修。
- ISSUES.md = D3 教学 backlog（T-1..C-2），不作为本 track 实施依据（已核对当前代码仍属算子域缺口，非 surface 缺口）。

（后续 Phase 证据逐节追加）
