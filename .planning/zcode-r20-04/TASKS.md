# ZCode R20-04 TASKS

| WP | 任务 | 状态 | 文件 | 验证 | 结果 |
|---|---|---|---|---|---|
| D03 | #1560 runGdalTask 取消探针 + GDAL progress 适配器 | VALIDATED | src/app/dialogs/raster_processing_dialog_base.{h,cpp} + tests | O-1 RED→GREEN×2 | 种子 d5b95a9546 已提交 |
| D10 | CT-6 7 工具 raw schema 补 type/properties | VALIDATED | src/agent/cartography/cartography_tools.cpp | O-2 registry 腿 | RED=master 现状（R19-02 记录）|
| D11 | DPI 下界统一 72（layout_service） | VALIDATED | src/agent/layout_tools/layout_service.cpp | O-3 RED→GREEN×2 | 种子同 d5b95a9546 |
| D02 | W1b Edit Lease 绕过 UI 侧对照 | VALIDATED（只读+移交） | 记录于 CROSS_TRACK_FINDINGS | O-4 | — |
| D01/D04-D09/D12-D14 | 盘点型 WP | TODO（按会话容量递进） | — | — | — |

## 会话内优先序
1. 构建完成 → 跑三个 RED 取证（#1560、DPI、CT-6 registry 腿）。
2. 三个修复 → 双 GREEN×2 → 提交。
3. 回归族：test_raster_processing_dialog_base 全量、test_layout_tools 全量、test_surface_snapshot/test_surface_parity、test_m2_batch_a_dialogs（cancel 路径邻近）、test_gui_job_adapter、test_cartography_production_11（已知环境红，区分记录）。
4. W1b 只读对照 → CROSS_TRACK_FINDINGS。
5. R1/R2 独立评审 → PR。
