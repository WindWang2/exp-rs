# R21-E PR_COLLISION_MATRIX — 采集 2026-10-23T21:52Z

## 开放 PR
- **0 条**（`gh pr list -R WindWang2/exp-rs --state open --limit 50` 实测空）。
- 最近合并：#1672(r20-04)、#1671(r20-01)、#1670(r20-03)、#1669(r20-02)、#1668(r20-05)——全部已进 BASE。

## 活跃兄弟分支（本地 worktree 实见）
| 分支 | 域 | 状态 | 冲突面 |
|---|---|---|---|
| agent/r21-a-scientific-algorithms-20261023T184835Z | 科学算法 | 在跑 | 无共同写区；共享 tests/CMakeLists.txt append 区 |
| agent/r21-b-data-model-runtime-20261023T185101Z | 数据/模型/运行时 | 在跑 | 无共同写区；共享 tests/CMakeLists.txt append 区 |
| agent/r21-c-pi-mcp-harness-20261023T185308Z | Pi/MCP | 在跑 | 无共同写区；共享 tests/CMakeLists.txt append 区 |
| 旧 r19/r20-* 远程分支 | — | 只有 behind | 不作基座，不 rebase 搬运 |

## 共享灰区文件策略
- `tests/CMakeLists.txt`（16k+ 行）：**append-only 尾部追加**，提交前 `git diff origin/master...HEAD -- tests/CMakeLists.txt` 确认无他人区块被改。
- 根 `CMakeLists.txt` / `data/` 公共注册表：预期零触碰；若必需则最小 append。
- `docs/adr/`：预期零触碰（#1601 的处置不写新 ADR，走代码注记 + 既有 ADR 0172 事实）。

## 提交前复核清单（M7 时执行）
1. `gh pr list --state open` 再查（他轨新 PR 若与本轨文件交集 → 读 diff、让位或 rebase）。
2. `git fetch origin` 或 `gh api repos/.../branches/master` 比对 master SHA 漂移；漂移则评估 `git diff origin/master...HEAD` 与新 master 的同文件改动。
3. merge-loss 检查：本轨 patch 的每个文件与最近同文件 merged patch 对照（尤其 R20-05 的 43 文件清单）。
