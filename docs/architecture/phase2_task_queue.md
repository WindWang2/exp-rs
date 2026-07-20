# Phase 2: 任务队列（Task Queue）

> 生成时间：2026-07-16  
> 依据：`docs/phase1_codebase_analysis_report.md`

---

## 队列设计原则

1. **自底向上**：先建立统一的算子抽象层（RSOperator），再将现有算法逐步迁入；避免在没有统一接口的情况下堆积实现。
2. **核心优先**：先完成 C++ 内核与 Agent 接口，再补齐 UI 与 Python 扩展。
3. **可验证性**：每个 Task 必须有对应的 Catch2 单元测试或 CLI 烟雾测试。
4. **不阻塞主线程**：所有算子必须支持后台线程执行，UI 层仅做绑定。

---

## 任务队列

### P0 — 架构层（必须先完成）

#### Task 1: 设计并实现 `RSOperator` 统一算子基类
- **目标**：建立所有遥感算法的共同抽象接口。
- **交付物**：
  - `src/operators/framework/rs_operator.h`
  - `src/operators/framework/rs_operator.cpp`
  - `src/operators/framework/rs_operator_context.h`
  - `src/operators/framework/rs_error.h`
  - `src/operators/framework/rs_progress.h`
  - `src/operators/framework/rs_schema.h`
- **核心接口**：
  - `virtual Json::Value run(const Json::Value& params) = 0;`
  - `virtual std::string name() const = 0;`
  - `virtual Json::Value schema() const;`
  - `virtual Json::Value metadata() const;`
  - 统一进度回调：`std::function<void(double, const std::string&)> progressCallback`
  - 统一取消标志：`std::atomic<bool>* cancelFlag`
- **异常规范**：`RSOperatorError`（含 `ErrorCode` 枚举、`message`、可选 `details`）。
- **线程要求**：`run()` 必须可在任意线程调用，不依赖 Qt GUI。
- **验证**：`tests/test_rs_operator.cpp`（参数解析、进度回调、异常、Schema 导出）。

---

### P1 — 算子层（多库融合）

#### Task 2: 将 OpenCV 滤波/边缘检测算子包装为 RSOperator
- **目标**：把现有 `src/processing/algorithms/image_enhancement.cpp` 中的空间滤波能力接入 RSOperator。
- **算子列表**：
  - `opencv:gaussian_blur`
  - `opencv:median_blur`
  - `opencv:sobel`
  - `opencv:laplacian`
  - `opencv:canny`
- **实现路径**：新增 `src/operators/opencv/` 目录；算子内部读取输入栅格 → `cv::Mat` → OpenCV 处理 → 写出 GeoTIFF。
- **验证**：每个算子至少 2 个测试（参数校验 + 像素级结果断言）。

#### Task 3: 将 OTB 分类/分割能力包装为 RSOperator
- **目标**：将 OTB MeanShift、SVM Classification、TrainImagesClassifier 等能力接入 RSOperator。
- **算子列表**：
  - `otb:meanshift_segmentation`
  - `otb:svm_classification`
  - `otb:compute_images_statistics`
- **实现路径**：复用 `src/processing/providers/otb_tools/` 的 CLI wrapper，但对外暴露为 RSOperator；默认优先使用系统 `otbcli_*`，未来替换为 vendored OTB。
- **验证**：`tests/test_otb_rs_operators.cpp`（schema、参数传递、失败路径）。

#### Task 4: 将 GDAL 正射校正/几何校正包装为 RSOperator
- **目标**：把 `QgsImageWarper` + GDAL warp 能力接入 RSOperator。
- **算子列表**：
  - `gdal:orthorectification`（GCP/RPC/DEM 模式）
  - `gdal:reproject`
  - `gdal:clip`
- **实现路径**：新增 `src/operators/gdal/`；复用 `src/analysis/georeferencing/` 和 `src/processing/gdal/gdal_dataset_wrapper.cpp`。
- **验证**：复用现有 `test_image_warper.cpp` 数据，增加 RSOperator 接口层测试。

#### Task 5: 将自研遥感算法包装为 RSOperator
- **目标**：NDVI、BandMath、大气校正、变化检测、图像融合、地形分析等接入 RSOperator。
- **算子列表**：
  - `rs:spectral_index`
  - `rs:band_math`
  - `rs:atmospheric_correction`
  - `rs:change_detection`
  - `rs:image_fusion`
  - `rs:terrain_analysis`
- **实现路径**：`src/operators/rs/` 目录；每个算子调用 `src/processing/algorithms/` 的现有实现，但参数/进度/错误统一走 RSOperator 接口。
- **验证**：为每个算子补充 schema + 运行测试。

---

### P2 — Python 扩展层

#### Task 6: 重新引入 pybind11 并设计 Python 算子 API
- **目标**：让 Python 脚本能够调用 C++ RSOperator 并注册自定义算子。
- **交付物**：
  - 恢复 `SICNU_EMBED_PYTHON` CMake 选项与 pybind11 FetchContent。
  - `src/python/rs_python_module.cpp`：暴露 `rs.run_operator(name, params)`、`rs.list_operators()`。
- **验证**：`tests/test_python_operator.cpp` 或 Python 测试脚本。

#### Task 7: 实现 `cv::Mat` / `GDALDataset` 与 NumPy `ndarray` 的零拷贝桥接
- **目标**：大影像在 C++ 与 Python 之间高效传递，避免复制。
- **交付物**：
  - `src/python/rs_numpy_bridge.h/.cpp`
  - 支持 `cv::Mat` ↔ `numpy.ndarray`（共享 data pointer）
  - 支持 GDAL MEM dataset ↔ `ndarray`（通过 `GDALDriver::Create("MEM", ...)`）
- **验证**：内存地址一致性测试、大数据量不触发 OOM 测试。

#### Task 8: 在 Qt GUI 中集成 Python 脚本编辑器 Dock
- **目标**：学生可在应用内编写并运行 Python 脚本处理当前图层。
- **交付物**：
  - `src/app/widgets/python_console_widget.h/.cpp`
  - `View → Python Console` 菜单
  - 脚本可访问当前 `QgsProject` 和 `QgsMapCanvas`
- **验证**：UI 存在性测试 + 简单脚本执行测试。

---

### P3 — 教学增强 UI 层

#### Task 9: 实现卷帘对比（Swipe Tool）
- **目标**：左右/上下分屏对比处理前后影像。
- **交付物**：
  - `src/app/map_tools/swipe_map_tool.h/.cpp`
  - 集成到 `View → Compare Layers`
- **验证**：`tests/test_swipe_tool.cpp`（事件、图层数量校验）。

#### Task 10: 实现实时直方图拉伸（Histogram Stretch Tool）
- **目标**：拖动 min/max/拉伸百分比时，画布实时更新渲染。
- **交付物**：
  - `src/app/widgets/histogram_stretch_widget.h/.cpp`
  - 与 `QgsRasterLayer` 的 contrast enhancement 联动
- **验证**：`tests/test_histogram_stretch.cpp`。

#### Task 11: 实现实验操作日志导出
- **目标**：自动记录每步操作（算法名、参数、输入/输出、耗时、错误），导出为实验报告 JSON/CSV。
- **交付物**：
  - `src/operators/framework/rs_operation_logger.h/.cpp`
  - `File → Export Lab Report` 菜单
- **验证**：`tests/test_operation_logger.cpp`。

---

### P4 — Agent CLI 层

#### Task 12: 实现 headless pipeline 执行器
- **目标**：`sicnu_geo_rs --pipeline pipeline.json` 在无界面状态下执行算子链。
- **交付物**：
  - `src/cli/rs_pipeline_runner.h/.cpp`
  - `src/cli/main_cli.cpp`（或复用 `main.cpp --pipeline`）
  - Pipeline JSON Schema 定义：`data/schemas/pipeline_schema.json`
- **Pipeline DSL 示例**：
  ```json
  {
    "steps": [
      {"operator": "rs:spectral_index", "params": {"input": "input.tif", "index": "NDVI", "output": "ndvi.tif"}},
      {"operator": "opencv:gaussian_blur", "params": {"input": "ndvi.tif", "kernel": 5, "output": "smooth.tif"}}
    ]
  }
  ```
- **验证**：CLI 烟雾测试 + 多步骤 pipeline 结果断言。

---

## 执行顺序

```
Task 1 → (Task 2 ∥ Task 3 ∥ Task 4 ∥ Task 5) → (Task 6 ∥ Task 7) → Task 8
                                                            ↓
                                                  (Task 9 ∥ Task 10 ∥ Task 11) → Task 12
```

> 注：`∥` 表示可并行推进，但所有算子包装任务都依赖 Task 1 的接口稳定。

---

## 达成标准

- [ ] `RSOperator` 接口稳定且所有新算子通过统一接口运行。
- [ ] 至少 5 个 OpenCV 算子、3 个 OTB 算子、3 个 GDAL 算子、6 个自研 RS 算子接入 RSOperator。
- [ ] Python 控制台可运行脚本调用 C++ 算子并处理当前图层。
- [ ] 卷帘对比、直方图拉伸、实验日志导出三项教学功能可用。
- [ ] `sicnu_geo_rs --pipeline pipeline.json` 可在无显示器环境下成功执行多步算子链。
- [ ] 全部新增代码配套 Catch2 测试，原有测试无回归。
