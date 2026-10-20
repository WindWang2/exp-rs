# R19-03 ORACLES（逐行：ID / 触发源 / 命令 / RED / fix commit / GREEN×2 / 负向 / reviewer / rebase 后）

| ID | 触发源/风险 | 测试/探针命令 | baseline RED | fix commit | GREEN-1 | GREEN-2 | negative test | reviewer | post-rebase |
|---|---|---|---|---|---|---|---|---|---|
| O-1 | Edit 菜单 Cut/Paste/Delete Selected 无会话恒可用（r18-D R2 P3-5 遗留；C01） | `test_r14_D_vector_edit_honesty [menu_session_gate]` | 待录（editingAction 查无 → REQUIRE 失败） | 待 | 待 | 待 | 无会话 raster 当前层 + 会话开启/回滚两段 | 待 | 待 |
| O-2 | terminology summary 无 120 上界（#1639-2；C11/O-C4 契约） | `test_help_core [r19d][terms]` | 待录（300 字 summary > 120） | 待 | 待 | 待 | 超长 + 尾部星面字符（代理对边界）；短定义无损 | 待 | 待 |
| O-3 | m2_batch_a 两处 CHECK(true) 恒真（#1621 残余；O-C5） | `test_m2_batch_a_dialogs`（DataManager toggle / Set spectral curve profile 两 SECTION） | 待录（summary 标签查无 objectName → REQUIRE 失败） | 待 | 待 | 待 | 空态文本 + 按钮禁用前置；断言可红探针（mutation：注释 updateSpectrumSummary 调用） | 待 | 待 |
| O-4 | #1655 渲染门永久熄灭（证据性） | `SICNU_CP11_RENDER=1 timeout 300 test_cartography_production_11` | N/A（探针） | 无代码 | — | — | 挂起即 timeout 124 如实记录 | N/A | N/A |

## 门禁清单（PR 前逐项核）

- [ ] `git diff --check` clean
- [ ] 分支未写共享/红线目录（`git diff origin/master...HEAD --name-only` 对照 OWNERSHIP）
- [ ] 无新增恒真断言（新断言均有可红证明或 mutation 探针）
- [ ] 无把依赖缺失/构建失败记为通过；NOT_EXECUTED 如实标注
- [ ] 本机复现所改模块核心路径 GREEN×2
- [ ] 两轮独立 reviewer P0/P1 闭环（REVIEW_R1/R2）
- [ ] rebase 后相关 RED/GREEN 重跑
- [ ] PR 已创建（URL 回填）
