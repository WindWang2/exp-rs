# PR_COLLISION_MATRIX — r21-A

勘查时间：2026-10-24 LIVE。

## Open PR

无（`gh pr list -R WindWang2/exp-rs --state open --limit 20` → `[]`）。

## 最近合并的同域 PR（merge-loss 风险源）

| PR | 轨 | 涉及本轨文件 | 风险与对策 |
|---|---|---|---|
| #1671 | r20-01 科学算法 | rs_classifier_isodata/mlp、rs_sar_backscatter_operator、mosaic_plan、sar_calibration、sar_insar、terrain_viewshed、trend_analysis + 7 个测试 | 全部为 R21-A 白名单文件；本轮每文件开工前 `git log --oneline origin/master -- <file>` 对账，改动不回滚其语义（BASELINE 契约表） |
| #1669 | r20-02 数据/模型/运行时 | src/runtime、data、models | 无本轨文件交集 |
| #1670 | r20-03 Pi/MCP/Workflow | src/operators/runtime、sdk | 无本轨文件交集（rs_sar_backscatter_operator 只在 #1671） |
| #1672 | r20-04 Qt/QGIS | src/app、gui | 无本轨文件交集 |
| #1668 | r20-05 教学/验证 | src/teaching、verification | 无本轨文件交集 |

## 活跃分支（远程）

- `origin/agent/r20-*` 五分支：全部 behind master、无 ahead（R20 结束态），不建 worktree、不 rebase、不覆盖。
- 无 r21-* 远程分支 → 本轨首建，无命名冲突。

## 共享灰区文件（根 CMakeLists.txt、tests/CMakeLists.txt、data 公共注册表、公共 ADR）

- 原则：**只在确有必要时 append-only**；提交前 `git diff origin/master...HEAD --name-only` 逐一核对。
- 若新测试需登记：优先复用 tests/CMakeLists.txt 既有同名测试目标的源文件清单（append 一行），不新增目标；确需新目标时用 `sicnu_add_*_test` 既有宏按本轨命名块追加。
- 冲突处理：发现他轨已动同一区块 → 变基解决以他轨语义为准 + 本轨 append，不删除他人条目。

## 提交前动态对账（2026-10-24 LIVE，fetch 后）

- origin/master 仍为 911e0b17（未前进）。
- 新出现 Open PR：#1673（r21-B）、#1674（r21-E）、#1675（r21-C）——逐一核对 files 列表，
  **无一触碰本轨白名单**（src/analysis、src/processing/algorithms、src/operators/rs、本轨 tests）→ 零冲突。
- Open Issues 仍 126（与 M0 一致）。
- 结论：无需 rebase；分支与 master 无相同文件差异。
