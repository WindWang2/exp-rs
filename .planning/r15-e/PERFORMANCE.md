# R15-E PERFORMANCE

**本轨未做任何性能优化，因此本文件不提供任何性能数字。**

## 为什么

任务书的 E16（UI 性能画像）要求记录启动、打开大栅格、切换图层、矩阵表滚动、10 万条任务列表筛选的
p50 / p95 / RSS。本轮的执行顺序把所有算力预算给了**已实证的正确性缺陷**（4 个 P1 + 7 个 P2），
没有采集基线，也没有做局部优化。

按任务书「不允许凭空宣称 +X%」「没有实测就不得编造数字」的要求，这里如实留空：

| 场景 | 基线 | 修复后 | 结论 |
|---|---|---|---|
| 启动（冷启动到主窗口可交互） | 未采集 | 未采集 | NOT VERIFIED |
| 打开大栅格（≥1 GB GeoTIFF） | 未采集 | 未采集 | NOT VERIFIED |
| 图层切换（≤50 图层） | 未采集 | 未采集 | NOT VERIFIED |
| 矩阵表滚动（Experiment Studio，500 行 bounded） | 未采集 | 未采集 | NOT VERIFIED |
| 任务列表筛选（10 万条） | 未采集 | 未采集 | NOT VERIFIED |
| 常驻内存 RSS（空载 / 满载） | 未采集 | 未采集 | NOT VERIFIED |

## 已知的性能相关事实（来自代码，不是测量）

1. `ExperimentStudioDock::rebuildMatrixTable()` 的显示上限是 `kMaxMatrixDisplayRows = 500`
   （`src/app/experiment_studio/experiment_studio_dock.cpp`），由 #1510 引入；超出时状态栏追加
   「preview limited to first 500 of N rows」。**本轨复核确认统计数字来自完整 report，不来自屏幕行**，
   因此未重写 `QAbstractTableModel`（任务书 E07 明确要求「若现状达标不新增替代模型」）。
2. `RsJobPanel::fillDetailsForTask()` 对 500 ms 内的进度刷新做节流、日志视图按增量切片追加
   （#1510/#704 既有实现），本轨未改动这部分逻辑。
3. 本轨新增的 `QFileInfo::exists()` 调用（`rs_result_summary.cpp`、`rs_job_panel.cpp`
   `updateActionEnabled()`）只作用于**单次结果渲染**与**单次选中变更**，每次 ≤ 产物条数（通常 1–3 个），
   不构成热点；未做量化验证，故不作为性能声明。

## 后续建议

若要补 E16，最小可行路径：在 `offscreen` 下用 `QElapsedTimer` + 合成数据（10 万条任务、100 万像元栅格）
测 `refreshAll()` / `rebuildMatrixTable()` / `findChild` 遍历的墙钟时间与 `GetProcessMemoryInfo` 的
WorkingSet，各跑 5 次取中位数，并明确 warmup 与环境。
