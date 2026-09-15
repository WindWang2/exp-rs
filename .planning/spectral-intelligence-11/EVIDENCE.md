# EVIDENCE — spectral-intelligence-11

逐轮/逐阶段证据账本。每条 = 命令 + exit + 摘要。

## Phase 0

- `git fetch origin --prune && git rev-parse origin/master` → 0, `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- `gh pr list --state open` → 仅 #1008（open, CONFLICTING）；#991/#992 已并入 master。
- `gh issue list --state open` → #1001–#1007，全为非光谱域，dedupe 记录于 PARALLEL_OWNERSHIP.md。
- `git worktree add ../exp-rs-spectral-intelligence-11 -b zcode/spectral-intelligence-11 origin/master` → 0。
- Subagent #1（只读架构审计）→ 见 CURRENT_ARCHITECTURE.md 引用。

## 资源政策记录

- 宿主 Windows / Git Bash：无 uptime load average 可测 → 按规则记录一次：`not-executed: load average under Git Bash`；build 固定 `-j2`，RSS > 70% 时降 `-j1`（用 tasklist 抽查）。
- `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`。

## OUT_OF_SCOPE

-（待填）

## Phase 1+
-（逐条追加）
