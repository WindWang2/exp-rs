# R19-03 ORACLES（逐行：ID / 触发源 / 命令 / RED / fix commit / GREEN×2 / 负向 / reviewer / rebase 后）

| ID | 触发源/风险 | 测试/探针命令 | baseline RED | fix commit | GREEN-1 | GREEN-2 | negative test | reviewer | post-rebase |
|---|---|---|---|---|---|---|---|---|---|
| O-1 | Edit 菜单 Cut/Paste/Delete Selected 无会话恒可用（r18-D R2 P3-5 遗留；C01） | `test_r14_D_vector_edit_honesty [menu_session_gate]` | :1320/:1354 `nullptr != nullptr`（mActionCutFeatures） | 04e81fac0+6104933e6（ribbon 投影） | 30/30 | 30/30 | raster 当前层禁用+reason；会话开启解锁、rollback 复锁 | R1 条件通过→处置；R2 APPROVE | 待 |
| O-2 | terminology summary 无 120 上界（#1639-2；C11 契约） | `test_help_core [r19d][terms]` | :608 `302 (0x12e) <= 120` | 04e81fac0+6104933e6（切点强化） | 7/7 | 7/7 | 119 CJK+星面对（恰跨 120 切点，断言 `== left(119)`）；短定义无损 | R1 条件通过→处置；R2 APPROVE | 待 |
| O-3 | m2_batch_a 两处 CHECK(true) 恒真（#1621 残余；O-C5） | `test_m2_batch_a_dialogs` 两 SECTION | `spectralSummaryLabel` 查无 `nullptr != nullptr` | 04e81fac0+6104933e6（换参修复） | 132/132 | 132/132 | 空态文本+按钮禁用前置；**变异探针**：移除 ctor updateSpectrumSummary → 1 用例红（131/132） | R1 条件通过→处置；R2 APPROVE | 待 |
| O-4 | #1655 渲染门归属（证据性） | `SICNU_CP11_RENDER=1 ./test_cartography_production_11` | N/A | 无代码 | — | — | 探针 EXIT=42（3 渲染失败非挂起）→ KNOWN_BASELINE_FAIL（diff 零编图文件） | N/A | N/A |

## 门禁清单（PR 前逐项核）

- [x] `git diff --check` clean（origin/master...HEAD 实测）
- [x] 分支未写共享/红线目录（20 文件 vs OWNERSHIP/#1663/#1664 核对）（`git diff origin/master...HEAD --name-only` 对照 OWNERSHIP）
- [x] 无新增恒真断言（RED×3 + 变异探针可红证明；两处旧 CHECK(true) 被移除）（新断言均有可红证明或 mutation 探针）
- [x] 无把依赖缺失/构建失败记为通过（KNOWN_BASELINE_FAIL/NOT_EXECUTED 如实）；NOT_EXECUTED 如实标注
- [x] 本机复现所改模块核心路径 GREEN×2（R1/R2 reviewer 独立重跑）
- [x] 两轮独立 reviewer P0/P1 闭环（R1 条件→处置；R2 APPROVE 零 P0/P1/P2）（REVIEW_R1/R2）
- [x] rebase 检查：fetch 实测 0 behind（master 未动，无需 rebase）；终态门全部重跑
- [x] PR 已创建：https://github.com/WindWang2/exp-rs/pull/1665
