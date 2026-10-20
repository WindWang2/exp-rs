# R19-03 TASKS（按 P0/P1/P2/P3；全部锚定源码符号和可执行验证）

> **2026-10-20 实读复核后大幅收敛**：#1662 已修 #1558/#1571/#1561/#1562/#1563/#1564/#1559/#1621(大部)/#1622/#1657(RAII)/#1632(1,2,5,6)/#1639(1)；r18-d 遗留 C4（produce legend 守卫纯函数化）也已在位。下列为 LIVE 验证后仍开放的全部任务。

## P1（先做）

- [x] ~~T1 / #1558~~：#799/#784 用例已重写真断言（实读 :813-841/:893+），撤销。
- [ ] **T2 / r18-D 遗留 R2 P3-5**：`src/app/main_window_menus.cpp:191/:200/:206` Edit 菜单 Cut Features/Paste Features/Delete Selected 为裸 addAction（无 objectName、不入门控列表）→ 注册 objectName + `m_editingSessionMenuActions` 列表 + updateEditingUI 门控 + editingAction/新 accessor 测试面。RED：`test_r14_D_vector_edit_honesty.cpp` 新 [r19d] 用例（editingAction 查无 → REQUIRE 失败）。

## P2

- [x] ~~T3 / #1571~~：已修（锁外回调/部分失败语义），撤销。
- [x] ~~T4 / r18-D C4~~：`legendAutoUpdateHazard` 纯函数守卫已在 produce.cpp:93-130，撤销。
- [ ] **T5 / #1621 残余**：`tests/test_m2_batch_a_dialogs.cpp:299/:318` 两处 CHECK(true) → 真断言（DataManager 切换的输入可见性互换；setSpectrum 后 summary "4 bands" + match 按钮使能 + 空态文本/禁用前置断言）。需 `spectral_library_dialog.cpp` 给 `m_spectrumSummary` 补 objectName（一行，测试面）。
- [x] ~~T6 / #1622~~：三处墙钟断言已移除（grep 0 命中），撤销。

## P3

- [ ] **T7 / #1639 条目2**：`src/help/terminology_provider.cpp:228` `d.summary = term.definitionZh` 无上界 → `truncateSummary`。RED：`test_help_core.cpp` 新 [r19d][terms] 用例（300 字定义 + 尾部星面字符 → summary ≤ kHelpSummaryMaxChars 且无孤立代理；短定义无损）。
- [ ] **T12 / #1655 证据性**：本机探针 SICNU_CP11_RENDER=1 × 5 渲染用例（timeout 护栏）；跑不动如实 NOT_EXECUTED，不改 CI。

## 明确不做（本轮，记录于 CROSS_TRACK_FINDINGS）

- #1560（runGdalTask 取消通道：seam 注释已自述设计方向——task lambda 需要能触达 ctx.isCancelled 的 GDAL 进度回调；独立 WP）
- W1b（qgis_app_facade lease 路由：高危跨 data 侧）
- #1638（src/gui 弱桩 ABI 禁改，仅诊断）
- #1565（教学业务，只读）

## REVIEW-ONLY

- 每 PR 文件级逆向审查（两轮独立 reviewer）本身即工作项，见 ORACLES.md。
