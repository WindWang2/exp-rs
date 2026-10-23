# ZCode R20-04 ORACLES

## O-1（#1560 / D03）— GDAL 长任务及时取消
- RED：`test_raster_processing_dialog_base` 新 SECTION 在 master（种子提交 d5b95a9546 的父）确定性失败于 `REQUIRE(steps.load() < kTotalSteps)`（steps==300：task 无法观察取消，跑完全程）。
- GREEN：修复后同 SECTION 通过 ×2 + 探针语义 SECTION（probe 起始为 false、gdalCancelProgress 取消后返回 0）。
- 禁止的"修复"：缩短模拟任务、屏蔽 Cancel 按钮、删除断言。
- 附带断言：取消后 accepted=false、resultReadyForDisplay 不发、Run 恢复可用、后续任务可再次执行成功。

## O-2（CT-6 / D10）— 7 个 cartography 工具 schema 全绿
- RED：`test_surface_snapshot` registry 腿（raw registry schema vs 快照 canonical 逐工具比较）在 master 失败（R19-02 记录，首个失败 list_token_sets）。
- GREEN：修复 raw 源（cartography_tools.cpp 各加一行 type/properties）后 registry 腿 + projection 腿 + wire 腿全绿 ×2。禁改快照文件迎合（快照已是 canonical，无需 regen；若发现快照错则先证后改并说明）。

## O-3（DPI 统一 / D11 关联）
- RED：`test_layout_tools` 新 SECTION 在 master 失败于 `CHECK_FALSE(ok)`（36 dpi 被放行并渲染成功）。
- GREEN：exportLayout 拒绝 dpi<72 且错误含 "72"；72 边界仍可导出；96/1200/NaN 既有断言不回归 ×2。

## O-4（W1b）— 只读对照
- 产出：UI/Data 身份对照记录（qgis_app_facade 编辑路径 vs DataManager Edit Lease），不改数据域；跨轨移交记录。

## 通用边界
正常/空/错误类型/超大输入/取消/超时 —— 适用于本轨的项以测试矩阵覆盖；已证上游预存红记 KNOWN_BASELINE_FAIL（本机同环境复跑取证）。
