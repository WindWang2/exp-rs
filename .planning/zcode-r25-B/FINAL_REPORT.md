# R25-B FINAL_REPORT

**分支** `agent/r25-B-data-model-runtime-20261028T140313Z`
**Worktree** `/home/kevin/projects/rs-studio/exp-rs-r25-B-20261028T140313Z`
**BASE** `2610cb4741e69a89124921e44c03e340e2522ee2`（origin/master @ 2026-10-27，与任务书快照一致）
**HEAD** `d777573994`（11 commits；`git rev-list --left-right --count origin/master...HEAD` = 0 behind / 11 ahead）

## 交付总览

在 B 域既有架构内完成**三项功能深化**（均接生产代码路径、带对外 schema、RED→GREEN×2、双轮独立评审闭环）：

| # | 功能 | 交付物 | 测试 |
|---|---|---|---|
| F1 | 导入预检解释报告 `sicnu.import.preflight/1` | `src/data/import_preflight.{h,cpp}` + `ProductAdapterRegistry::adaptersAccepting()` | test_r25_b_import_preflight 12 用例 |
| F2 | 可解释推理计划 + 质量摘要 `sicnu.inference.plan/1` + `sicnu.inference.quality/1` | `src/operators/runtime/inference_plan.{h,cpp}` | test_r25_b_inference_plan 6 用例 |
| F3 | 复现差异原因报告 v2（domain 归属 + remediation） | `src/experiment/replay_deviation.{h,cpp}` | test_r25_b_replay_deviation_v2 2 用例 |
| W7 | 跨层 E2E（预检→登记→计划→推理→质量→实验→差异归因） | — | test_r25_b_end_to_end 1 用例 |

文档：docs/datasets/import-preflight.md、docs/inference/plan.md、docs/experiments/replay-deviation.md；账本 `.planning/zcode-r25-B/`（BASELINE/GOAL/ORACLES/TASKS/DECISIONS/ISSUE_TRIAGE/PR_COLLISION_MATRIX/CROSS_TRACK_FINDINGS/LOOP_LEDGER/TEST_RESULTS/PERF_BASELINE/RISK_REGISTER/REVIEW_R1/REVIEW_R2/DATA_INFERENCE_MAP/PR_BODY）。

## 验证证据（全部实跑）

- **RED×3**：BASE detached worktree + 生产编译旗，三个新测试 TU 全 COMPILE_EXIT=1（缺头文件/缺 v2 类型）——证明用例判别力与功能不存在于 master。
- **GREEN×2**：`ctest -R "r25_b"` **21/21 通过 × 2 轮**（含 1 个 netCDF 用例按驱动可用性 SKIP——本 GDAL 无 netCDF 创建支持，SKIP 非 PASS 伪装）。
- **邻域回归**：r24_b 全 8 套件 + r23_b 2 + mlops9 + experiment_consistency_r4 + dataset_core + model_runtime + collection_import + data_manager 全族 = **40/40 通过**（0 failed / 0 NOT_BUILT）。
- **注册门**：1067 targets（base 1063 + 4），lost 0 / markers 0 / duplicates 0，exit 0。
- `git diff --check` 干净；无冲突标记；tests/CMakeLists.txt 仅末尾 append-only `# BEGIN R25-B` 块；.gitignore 仅末尾本轨 allow-list。
- **R1**（REQUEST_CHANGES：P0×1+P1×1+P2×5+P3×6）→ 全部关闭 → **R2**（APPROVE_WITH_FIXES：R1 修复自引入 P2×1 + P2×6 + P3 族）→ 全部关闭；处置表与 3 项测试侧修正如实记录。
- **性能**：三组指标 + 判别器（PERF_BASELINE.md），无加速宣称。

## 安全/不回退确认

- F1 远程拼写零探测（out_of_scope；含 Windows 盘符 VSI 拼写）；错误文本不泄露文件内容（正/负双 oracle 钉死）。
- 无网络、无 QLockFile、无裸 FILE*；GDAL RAII；身份 hash 8MiB 预算。
- B/C 既有 remote/SSRF/offline 语义未触碰（tests 只加不改）；邻域回归钉住。

## 未执行（如实标注）

- **push + PR**：**外部阻断**——GitHub 持续不可达（TLS `unexpected eof while reading`；api.github.com EOF；连续探测 >15 分钟、HTTPS/SSH 均不通；与本轨代码无关）。本地 11 commits 就绪，网络恢复后按 PR_BODY.md 末尾两条命令完成（push + `gh pr create`）。
- GPU/TRT/OpenVINO 真机：NOT_EXECUTED（本构建无 SDK；F2 用注入 provider 的真实引擎路径 + metamorphic 网格对照替代）。
- Windows/macOS：NOT_EXECUTED（Linux GCC 16.2.1/Qt 6.12/GDAL 3.13.3 实跑；评审确认跨平台面 clean）。
- 线上 CI：按任务书不等待。

## 剩余问题 / 已知限制

- R2-11 相关：`fedChannels` 在 scene/refused 计划中如实为 0（场景 lane 无批量语义）。
- `batchWorkingSetMiB` 为估算（provider staging 与 to_input resize 不在此模型内）——文档明示。
- ensemble 的 tilesPlanned 按成员乘算（R2-8），逐成员网格差异不展开（v1 边界）。
- 144 open issues 未批量关闭（B 域点名项实码核验全部 FIXED_IN_MASTER_BUT_OPEN，证据 ISSUE_TRIAGE.md）。
