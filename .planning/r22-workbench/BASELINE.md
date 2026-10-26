# R22-05 BASELINE（启动事实盘点）

- 盘点时刻（本地）：2026-10-25T16:38Z（会话日期 2026-10-26）
- `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（git fetch + `gh api` 一致；与任务书给定基线相同）
- 本地 master worktree：clean，位于 911e0b17a3
- 权限：`gh repo view` → viewerPermission=ADMIN（WindWang2/exp-rs）
- Open PR（5，全为 R21 轨，均 base=master，均基于 911e0b17a3）：
  - #1673 R21-B data/model-runtime（head 941af0d18f）
  - #1674 R21-E teaching-verification（head 0d919f5082）
  - #1675 R21-C pi-mcp-harness（head d088f0a4ca）
  - #1676 R21-A scientific-algorithms（head ec7d85effd）
  - **#1677 R21-D qt-qgis-cartography（head 37c5f539bb→PR 列表值）** —— 与本轨白名单重叠最大，见 PR_COLLISION_MATRIX
- Open issues：约 120 条（R17 lane 2026-10-24 批次 #1678–#1707 + 历史 #146x–#165x）。
- 兄弟 R22 worktree 在场：r22-data / r22-inference / r22-science（各自独立分支，互不写对方文件）。
- 本轨 worktree：`/home/kevin/project/exp-rs-r22-workbench-20261025T163805`，分支 `agent/r22-workbench-20261025T163805`，基于 911e0b17a3。

## 启动命令记录

```
git fetch origin master --prune; git rev-parse origin/master   # 911e0b17a3...
gh repo view WindWang2/exp-rs --json defaultBranchRef,viewerPermission
gh pr list --state open --limit 200 ...   # 5 PR（#1673–#1677）
gh issue list --state open --limit 500    # ~120
git worktree list                          # 34 个 worktree，含 R21 A–E 与 R22 data/inference/science
```

## 初审读过的材料

- master：`README.md`、`CMakeLists.txt`、`CMakePresets.json`、`PROJECT.md`（摘要）、`.zcode/`（无本轨相关 skill）、`.planning/r21-D/*`（R21-D worktree 内终报/评审）
- #1677 PR body + files changed（29 文件，+2116/−68）
- Issues：#1695、#1557、#1655、#1678、#1688、#1689、#1694、#1638、#1632（正文全读）；其余列表级扫描
