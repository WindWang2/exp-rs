# OWNERSHIP — r23-C

## 可写（本轨独占生产文件）
- `src/processing/framework/task_center.h` / `task_center.cpp`（WP-1 消费端修复 + 测试 seam）
- `src/teaching_admin/batch_assessment.cpp`（WP-2 批量输出原子性）
- `src/app/teaching_admin/teaching_admin_dock.cpp`（WP-3 i18n 复数）
- `tests/test_r23_c_*.cpp`（本轨新测试）
- `.planning/zcode-r23-C/**`

## 共享文件（append-only，push 前逐字节复核）
- `tests/CMakeLists.txt`：仅本轨注册块 append；push 前 `git diff origin/master...HEAD -- tests/CMakeLists.txt` 确认只有 append 行，且 `git show origin/master:tests/CMakeLists.txt` 的兄弟注册块逐字节保留。

## 只读（绝不修改）
- 主 checkout `/home/kevin/projects/rs-studio/main` 及所有兄弟 worktree/分支（r20-*/r22-*/hardening/*/flash/*/rs14-*）
- `src/processing/gdal/**`, `src/processing/algorithms/**`, `src/processing/providers/**`（非 framework 子树）
- `src/lab/**`（#1695-17d 属他轨）
- `src/jobs/**`：本轨原则上只读（R2-F3 修复在消费端；若评审认定需生产端配套，再单独论证）
- 公共根文档（CLAUDE.md、README、docs/adr）：本轨不动；报告写入 `.planning/zcode-r23-C/`
- `.goal-loop-ledger.md`（全局账本，只读）

## 并行轨冲突评估
- GitHub open PR = 0；远程分支仅 master（fetch 核对）。
- r20/r22 各轨分支均为已合并历史（本地 worktree 残留只读）。
- `tests/CMakeLists.txt`：master 最新为 #1715 修复后的干净版本；本轨 append 后需防三轨并行（若有新并行轨）交叉——push 前重新 fetch 并对照。
