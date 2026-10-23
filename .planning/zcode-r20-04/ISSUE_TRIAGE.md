# ZCode R20-04 ISSUE_TRIAGE（live 复核，2026-10-23）

## 已闭环（本轨修复）

| Issue | 实际路径/行 | 处置 |
|---|---|---|
| #1560 [P2] runGdalTask 空 cancel hook | src/app/dialogs/raster_processing_dialog_base.cpp:381-435（master blob 24d033b95c 前身） | 本轨 f9bbe9a5f6 修复：探针 + gdalCancelProgress；RED 种子 d5b95a9546 |
| R19-02 CT-6（非 issue，跨轨发现） | src/agent/cartography/cartography_tools.cpp 7 处 inputSchema | 本轨 7e147f0361 修复；registry 腿为 RED |
| R19-03 #4 DPI 下界（非 issue） | src/agent/layout_tools/layout_service.cpp:1360 | 本轨 b7a04d7465 修复（1→72） |

## 复核后判定已修/过期（上游已解决，不重复修）

| Issue | 条目 | 证据 |
|---|---|---|
| #1632 [P3] src/app 技术债 | 条目 1（terrain_dialog 死 QFutureWatcher/onAnalysisFinished） | 现码 terrain_dialog.h 无 QFutureWatcher/mWatcher；cpp 零命中 —— 已由上游修复 |
| #1632 | 条目 2（路径派生 completeBaseName/baseName 分歧） | 现码已有统一 defaultOutputPath()（含防漂移注释）—— 已修 |
| #1632 | 条目 5（va_selection_hub clampText 第三份拷贝） | va_selection_hub.cpp:22 已调 sicnu::help::truncateSummary —— 已修 |

## 评估后不改（有理由记录）

| Issue | 条目 | 判定 |
|---|---|---|
| #1632 | 条目 3（exp_gui::SpectralProfileWidget 双同名类） | "全仓无使用者"说法过期：tests/test_spectral_profile_widget.cpp 引用 exp_gui 12 处（R19-03 记录 154/154 绿）。删除属产品决策（D13 seam）而非缺陷修复，不改。 |
| #1632 | 条目 4（gui_job_adapter 三重 submit 样板） | 真实重复（P3），但属作业提交热路径重构；本轨 #1560 恰好触碰该文件相邻 seam，避免同 PR 混入行为性重构。移交后续。 |
| #1638 [P3] src/gui 弱桩三题 | src/gui 只读 | 记录于 CROSS_TRACK_FINDINGS #2，归属 vendored 基线 owner。 |
| #1623 [P2] .qm 无 install 规则 | 共享根 CMakeLists 区 | 本轮不触碰共享构建区（协议 §3）；留后续 append-only 提交。 |
| #1621/#1622 [P2] 测试恒真/墙钟 | tests/test_classification_studio_widget / test_spectral_profile_widget | 候选后续轮次（工作量大于本轮收益）。 |
| #1655 [P3] CP11 渲染门 | tests/test_cartography_production_11 | 环境相关；本轨复跑区分（TEST_RESULTS 记录）。 |

## 其余 open issues

126 条中其余属 src/core、src/runtime、src/cli、src/verification、src/teaching* 等其他轨区域 —— 不处理（ownership 红线）。
