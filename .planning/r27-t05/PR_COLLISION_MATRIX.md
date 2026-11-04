# R27-T05 PR Collision Matrix

开放 PR（侦察时 `gh pr list --state open`）：无（0 条）。近期合并 #1724–#1731 全部已入 BASE_SHA，无重复实现风险。

## 文件 × 轨 交叠预测

| 文件/目录 | 本轨 | 兄弟轨风险 | 处置 |
|---|---|---|---|
| `tests/CMakeLists.txt` | 末尾 append-only 块（唯一标识 `R27-T05`）| t01–t04 同样 append | PR 前对 `origin/master` 三方 diff；冲突只重放本轨追加段 |
| `tests/test_r21_e_studio_verifier_hook.cpp` | 修 :132 编译错误 | 低（r21 历史文件，无活跃轨认领）| 手术式单行改 |
| `tests/test_adversarial_m6.cpp` | 修 :216 空断言 | 低 | 手术式 |
| `tests/test_contract_*_9.cpp` 等契约漂移 | 更新契约期望 | 中（t01 science 若改 src/operators 会再漂移）| 只对 BASE_SHA 事实更新；PR 描述声明基准 |
| `src/repair_planner/` | #1774/#1778 | 无兄弟轨认领（近史 r13-h）| 见 DECISIONS#D1 |
| `.github/workflows/ci.yml` | #1734/#1682/#1683 | 中（他轨也可能修 CI）| 最小 diff；PR 前重 fetch |
| `scripts/build_offline_bundle.sh`、`packaging/` | #1748/#1749 | 低 | — |
| `src/workflow/`、`src/experiment/`、`src/grader/`、`src/teaching*/`、`src/verify*/`、`src/faultlab/` | 主体修复 | 低（t02 管 data/geospatial/runtime，t03 管 agent/sdk/plugins）| — |
| 根 `CMakeLists.txt`、`.gitignore`、`docs/adr/**` | 不动（受保护）| — | 如需注册新测试仅动 tests/CMakeLists.txt |

## Push 前

1. `git fetch origin --prune` → 若 master 移动：rebase/merge 解决 `tests/CMakeLists.txt`（只重放本轨段）。
2. `git diff --name-only $(git merge-base origin/master HEAD)..HEAD` 对照白名单。
