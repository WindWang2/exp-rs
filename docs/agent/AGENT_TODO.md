# AGENT_TODO — 全自动任务队列

> 生成时间：2026-07-16
> 模式：Autopilot（全自动开发）
> 依据：Phase 1 Deep Scan 结果

---

## 当前状态快照

已具备：
- `src/operators/framework/`：RSOperator 基类、Registry、Schema、Error、Context、Progress
- `src/operators/opencv/`：滤波/边缘检测算子
- `src/operators/otb/`：分割、SVM、统计算子
- `src/operators/gdal/`：正射校正算子
- `src/operators/python/`：pybind11 绑定与 cv::Mat ↔ NumPy 桥接
- `src/app/widgets/python_script_editor.h/.cpp`：Python 脚本编辑器 Dock（已完成）

缺失：
- `src/operators/rs/`：自研遥感算法未包装为 RSOperator
- 卷帘对比地图工具（MapTool 级别）
- 实时直方图拉伸控件
- 实验操作日志导出
- Headless CLI pipeline 执行器

---

## 任务队列

### P0 — 算子层（自研 RS 算法）

#### [x] Task 1: 将自研遥感算法包装为 RSOperator
- **目标**：NDVI、BandMath、大气校正、变化检测、图像融合、地形分析接入统一 RSOperator 接口。
- **交付物**：
  - `src/operators/rs/rs_spectral_index_operator.h/.cpp`
  - `src/operators/rs/rs_band_math_operator.h/.cpp`
  - `src/operators/rs/rs_atmospheric_correction_operator.h/.cpp`
  - `src/operators/rs/rs_change_detection_operator.h/.cpp`
  - `src/operators/rs/rs_image_fusion_operator.h/.cpp`
  - `src/operators/rs/rs_terrain_analysis_operator.h/.cpp`
  - `src/operators/rs/rs_operators_init.cpp`
- **算子列表**：
  - `rs:spectral_index`
  - `rs:band_math`
  - `rs:atmospheric_correction`
  - `rs:change_detection`
  - `rs:image_fusion`
  - `rs:terrain_analysis`
- **验证**：`tests/test_rs_operators.cpp`（schema、参数传递、运行结果）。

---

### P1 — 教学增强 UI 层

#### [x] Task 2: 实现卷帘对比地图工具（Swipe Tool）
- **目标**：在 QgsMapCanvas 上实现左右/上下卷帘对比。
- **交付物**：
  - `src/app/map_tools/swipe_map_tool.h/.cpp`
  - 与 `comparison_widget` 联动或独立地图渲染
  - `View → Compare Layers → Swipe` 菜单/工具栏入口
- **验证**：`tests/test_swipe_map_tool.cpp`（事件、图层校验）。

#### [x] Task 3: 实现实时直方图拉伸（Histogram Stretch Tool）
- **目标**：拖动 min/max/拉伸百分比时，画布实时更新渲染。
- **交付物**：
  - `src/app/widgets/histogram_stretch_widget.h/.cpp`
  - 与 `QgsRasterLayer` 的 contrast enhancement 联动
  - 集成到 Image Enhancement Panel 或独立 Dock
- **验证**：`tests/test_histogram_stretch_widget.cpp`。

#### [x] Task 4: 实现实验操作日志导出
- **目标**：自动记录每步操作（算法名、参数、输入/输出、耗时、错误），导出为实验报告 JSON/CSV。
- **交付物**：
  - `src/operators/framework/rs_operation_logger.h/.cpp`
  - `File → Export Lab Report` 菜单
  - RSOperator 基类自动埋点
- **验证**：`tests/test_operation_logger.cpp`。

---

### P2 — Agent CLI 层

#### [x] Task 5: 实现 headless pipeline 执行器
- **目标**：`sicnu_geo_rs --pipeline pipeline.json` 在无界面状态下执行算子链。
- **交付物**：
  - `src/cli/rs_pipeline_runner.h/.cpp`
  - `src/cli/main_cli.cpp`（或复用 main.cpp --pipeline）
  - Pipeline JSON Schema：`data/schemas/pipeline_schema.json`
  - CMake target `sicnu_geo_rs_cli` 或条件编译
- **验证**：CLI 烟雾测试 + 多步骤 pipeline 结果断言。

---

## 执行顺序

```
Task 1 → Task 2 → Task 3 → Task 4 → Task 5
```

## 达成标准

- [x] 6 个自研 RS 算子全部接入 RSOperator 并通过测试
- [x] 卷帘对比、直方图拉伸、实验日志导出三项教学功能可用
- [x] `sicnu_geo_rs_cli --pipeline pipeline.json` 可在无显示器环境下执行
- [x] 全部新增代码配套 Catch2 测试，原有测试无回归
