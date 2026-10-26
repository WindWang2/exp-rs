# R21-D PR_COLLISION_MATRIX（基线 911e0b17a3，提交前需再盘点刷新）

## Open PR（2026-10-24 盘点时）

- Open PR = 0（gh + API 双确认）。无在飞冲突。

## 近期已合并同域 PR（防回滚检查清单）

| PR | 域 | 与本轨关系 | 防回滚义务 |
|---|---|---|---|
| #1672 (R20-04) | Qt/QGIS 工作台/制图 | 直接前驱 | GDAL cancel 探针、7 raw schema、DPI [72,1200] 不得回滚；本轨改动涉及时跑其三腿测试 |
| #1665 (R19-03) | Qt/QGIS UX | 前前驱 | Edit-menu session gate、ribbon projection、terminology summary、honest spectral dialog |
| #1661 (R18-D) | workbench 交互状态 + MapSpec | 交互状态真值 | 状态转移表测试不得削弱 |
| #1662 全项目 sweep | 全域 | — | 本轨文件的历史 patch 不得静默回滚（push 前 merge-loss 审计） |
| #1667 (R19-01) | data/model/runtime | B 轨前驱 | DataManager API 兼容性 |
| #1666 (R19-02) | Pi/MCP | C 轨前驱 | cartography schema canonical/raw 对齐契约 |

## 兄弟轨在飞分支（会话期间动态变化）

- R21-A/B/C worktree 在场，分支未推 PR（盘点时 open=0）。R21-E worktree 于 06:28 出现（`exp-rs-r21-E-20261023T215154Z`，教学/验证轨），共享构建锁串行中。提交前重新 `git fetch origin` + `gh pr list --state open` + `git diff origin/master...HEAD --name-only` 对账。
- 预计交集面：R21-B 若改 `src/data/data_manager.*` → 本轨 W1b 修复使用其公共 API，需 rebase 复验；R21-C 若改 cartography 注册 → 三腿回归；R21-E 与本轨在 `src/app/teaching/**` 完全隔离（本轨让出）。

## merge-loss 检查点（push 前）

1. `git fetch origin && git rev-parse origin/master` 不变则直接比；前进则 `git merge-base origin/master HEAD` 分析同文件差异。
2. 对照 #1672/#1665/#1661 的 files-changed 清单 ∩ 本轨 diff —— 有交集即逐 hunk 比对语义保留。
