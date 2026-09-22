# Goal-Loop Ledger — hardening/integration-build-contract-drift

Worktree: /home/kevin/projects/rs-studio/exp-rs-hardening-integration-build-contract-drift
Baseline: origin/master a9dc33fa73 (PR #1236). Open PRs at slice 1: #1237–#1240.
(注：仓库根的 .goal-loop-ledger.md 是已 track 的共享工件，本 track 不改它；账本在此。)

| 轮 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|
| 1 | Phase 0 recon + 机械 inventory：6 孤儿模块（verify/agent_loop/preflight/repair_planner/recipes/study-bridge）、2 未接线源文件（sci_inspection.cpp、死 moc 触发器）、18 未注册测试；RED 实证 `cmake --build --target test_agent_loop_core`（fatal: agent_loop/session_state.h not found + cannot find -lsicnu_agent_loop） | grep add_subdirectory 全树 + git log -S + 窄 target 构建 | 通过（证据入 01-recon.md） | 实施接线 |
| 2 | 接线：root CMake +6 add_subdirectory；新建 src/recipes/CMakeLists.txt；tests/CMakeLists 注册 19 个 target；删死 moc 文件 | 六库编译（5/6 一次通过；study/bridge 等 Qt 栈） | 进行中 | 轻量测试批构建 |
| 3 | 结构 oracle test_build_wiring_drift（三规则 + 注释剥离 + 模块作用域 + 路径 token 后缀 + 显式豁免集×3） | harness -O1/-O2/-O3 全绿；sabotage S1(源除名)/S2(模块断线)/S3(测试注销)/S4(注释遮蔽) 全部精确命中后恢复 GREEN | 通过 | Catch2 内跑两遍 |
| 4 | 独立 adversarial review（Explore subagent，只读） | P0=0 P1=0；P2×2（basename 遮蔽、build-dev 污染）+P3×3 全部修复并经 reviewer 逻辑复核 | READY | 修复后复验 |
| 5 | 教训：sabotage 期间 `git checkout` 回滚了未提交的中央文件编辑 → 已重建 root/tests 编辑；改用文件级备份协议（cp+sed+mv） | git status + 全量 green 重跑 | 已恢复 | 继续 |

## 事故记录
- sabotage 轮误用 `git checkout CMakeLists.txt` / `tests/CMakeLists.txt`（含未提交改动）→ 两文件被回滚到 HEAD。root 接线块与 19 个测试注册已按原内容重建并用 oracle + git status 验证。此后 sabotage 一律 cp 备份 + sed + mv 还原。

## 待办（剩余 Oracle）
- [ ] 轻量测试批（17 target）构建+ctest
- [ ] test_study_e2e + test_curriculum + contract tests（需 qgis_core，后台排队）
- [ ] 关键 Oracle 连续两遍
- [ ] rebase 最新 master、PR 创建（不 merge、不等 CI）
