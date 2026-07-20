# Phase 1: 系统架构与依赖评估报告

> 生成时间：2026-07-16  
> 分析对象：`/home/kevin/projects/exp-rs`（SICNU GEO RS）  
> 分析范围：源码结构、构建系统、核心算法、UI 耦合、Python/Agent 集成现状

---

## 1. 项目总体状态

| 维度 | 现状 |
|------|------|
| **产品定位** | 基于 QGIS 引擎的纯 C++ 遥感影像处理与实验教学桌面平台 |
| **代码规模** | `src/` 下约 14 个一级模块；`tests/` 含 116 个测试文件、约 499 个 `TEST_CASE` |
| **构建状态** | 主程序 `build/sicnu_geo_rs` 已生成；但 CTest 中大量目标标记为 `NOT_BUILT`，需重新配置/编译测试 |
| **最近工作** | 近期集中在 Georeferencer v1.6 打磨与 Phase 11 分类模块生产化（`docs/superpowers/plans/2026-07-16-classification-v11-production-hardening.md`） |
| **未提交改动** | `git status` 显示 `CMakeLists.txt`、`cmake/Boost/BoostConfig.cmake`、OTB 源码补丁等 30+ 文件处于修改状态，疑似正在进行 OTB vendor 编译修复 |

---

## 2. 架构分层盘点

### 2.1 源码目录结构

```
src/
├── app/              # Qt 主窗口、对话框、菜单、工具栏、map tools、widgets
├── analysis/         # 遥感算法库：classification、georeferencing、segmentation
├── agent/            # MCP Server、STAC Client（AI Agent 基础设施）
├── core/             # vendored QGIS core（层、渲染、CRS、投影、数据源等）
├── gui/              # vendored QGIS gui（map canvas、map tools、dialogs、layer tree）
├── native/           # 平台原生集成
├── processing/       # 处理框架 + GDAL/OTB/QGIS 算法封装
│   ├── algorithms/   # 自研算法：spectral_indices、band_math、image_enhancement 等
│   ├── framework/    # ProgressCallback、ErrorReporter、ProcessingCache、InputValidator
│   ├── gdal/         # GDAL C API 包装（GdalDatasetWrapper）
│   ├── providers/    # gdal_tools / otb_tools / qgis_algorithms / generic_cli
│   └── tools/        # CLI 工具发现、路径管理
├── plugins/          # layer_tree、processing 插件
├── python/           # 可选嵌入式 Python API/Console（当前默认关闭）
├── runtime/          # 运行时相关
├── stubs/            # Qwt/QScintilla stub headers
└── ui/               # Qt Designer .ui 文件
```

### 2.2 依赖矩阵

| 依赖 | 状态 | 用途 | 风险 |
|------|------|------|------|
| Qt 6.2+ | ✅ 系统安装 | GUI、信号槽、并发、网络 | 低 |
| GDAL 3.4+ | ✅ 系统安装 | I/O、投影、栅格/矢量处理 | 低 |
| PROJ 8+ | ✅ 系统安装 | 坐标转换 | 低 |
| GEOS 3.10+ | ✅ 系统安装 | 几何运算 | 低 |
| OpenCV 4.5+ | ✅ 系统安装 | SIFT、分类器（NormalBayes/SVM/KMeans） | 低 |
| QGIS core/gui | ✅ vendor 在 `src/core`、`src/gui` | 地图渲染、图层树、处理框架 | 中（版本锁定） |
| OTB 10 | 🟡 `otb_ref/` vendor，但 `SICNU_BUILD_OTB=ON` 编译阻塞 | MeanShift、Learning、Feature Extraction | **高** |
| ITK 5.4 | 🟡 `itk_ref/` vendor subtree，可单独配置 | OTB 底层 | 中 |
| nlohmann_json | ✅ `external/nlohmann` | JSON | 低 |
| pybind11 | ❌ 已移除（Phase 6R.14） | Python 绑定 | **高**（影响 Python 扩展目标） |
| Catch2 v3.7.1 | ✅ FetchContent | 单元测试 | 低 |

---

## 3. 现有处理框架评估

### 3.1 已具备的能力

项目已经在 `QgsProcessingAlgorithm` 基础上建立了较完整的处理框架，与目标 `RSOperator` 概念高度接近：

- **参数化输入**：`QgsProcessingAlgorithm::run(const QVariantMap &parameters, ...)` 已支持字典式参数输入（`src/core/processing/qgsprocessingalgorithm.h:403`）。
- **JSON Schema 导出**：`toJsonSchema()` 和 `metadata()` 已作为虚方法加入基类（`src/core/processing/qgsprocessingalgorithm.h:236-241`），为 Agent 自动解析参数提供了接口。
- **进度回调**：`QgsProcessingFeedback` 已在 MCP Server 的 `AlgorithmWorker` 中使用（`src/agent/mcp_server.cpp:84-111`），支持 `progressChanged` 和取消传播。
- **错误报告**：`ErrorReporter` + `ProcessingError` 结构已存在（`src/processing/framework/error_reporter.h`），但尚未与所有算法深度集成。
- **缓存机制**：`ProcessingCache` 已存在（`src/processing/framework/processing_cache.h`），基于 SHA256(算法ID+参数JSON) 做键值缓存。
- **输入校验**：`InputValidator` 提供栅格/矢量/波段/输出路径/数值范围/核大小等静态校验。

### 3.2 算法覆盖度

| 来源 | 数量/能力 | 备注 |
|------|-----------|------|
| 自研 `processing/algorithms` | 14+ 个 C++ 算法文件 | NDVI/EVI/SAVI/NDWI、BandMath、大气校正、变化检测、图像增强（含 PCA/IHS/滤波/斑点）、图像融合、地形分析、镶嵌 |
| GDAL Tools Provider | 21+ 工具 | 通过 QProcess 调用 `gdal_*` CLI |
| OTB Tools Provider | 28+ 工具 | 通过 QProcess 调用 `otbcli_*` CLI |
| QGIS Algorithms Provider | 20+ 算法 | 含自研 RS 算法封装 |
| Generic CLI Provider | 长尾工具 | `data/tools/custom/*.json` 声明式注册 |

---

## 4. Core-UI 耦合度评估

### 4.1 当前耦合现状

| 层级 | 状态 | 关键证据 |
|------|------|----------|
| **算法实现层** | 基本解耦 | `src/processing/algorithms/` 中大多数算法只依赖 GDAL/OpenCV/QGIS Core，不直接依赖 Qt Widgets |
| **对话框层** | 中度耦合 | `src/app/dialogs/` 中 20+ 个 QDialog 子类负责参数收集、线程启动、结果加载。`RasterProcessingDialogBase` 已提取公共异步生命周期，但每个算法仍有专属对话框 |
| **主窗口层** | 高度集中 | `main_window.cpp` 拆分为 `main_window_*.cpp`，但菜单/工具栏/状态栏/图层树/画布仍由 `QgisDesktopWindow` 统一持有 |
| **异步执行** | 已部分解决 | `AsyncGdalRunner`、`AsyncAlgorithmRunner`、`QgsTask` 已在分类、几何校正、OBIA 中使用 |

### 4.2 与理想 RSOperator 架构的差距

1. **参数格式不统一**
   - 现状：算法使用 `QVariantMap`（Qt 类型系统）。
   - 目标：希望使用 `Json::Value` 作为跨语言、Agent 友好的统一参数格式。
   - 影响：当前 MCP Server 已能接收 JSON，但内部需转换为 `QVariantMap`，尚未原生支持 JSON Schema。

2. **进度回调接口不统一**
   - 现状：自研算法使用 `sicnu::ProgressCallback`，QGIS 算法使用 `QgsProcessingFeedback`，GDAL/OTB CLI 使用 QProcess 信号。
   - 目标：所有算子应暴露统一进度信号/回调。
   - 影响：Agent 和 UI 需要为不同来源写不同的进度监听代码。

3. **异常机制不统一**
   - 现状：QGIS 算法抛 `QgsProcessingException`，自研算法有的返回 `bool`、有的用 `QMessageBox`、有的写 `QgsMessageLog`。
   - 目标：所有算法抛出带 `ErrorCode` 和 `ErrorMessage` 的强类型异常。
   - 影响：Agent Self-healing 难以统一捕获和分类错误。

4. **缺少 headless pipeline 可执行程序**
   - 现状：`--mcp` 模式启动 MCP Server（stdio/JSON-RPC 2.0），可执行单步算法。
   - 目标：需支持 `--pipeline pipeline.json`，无界面执行多步算子链，并自动处理中间结果依赖。
   - 影响：Agent 批处理/工作流自动化能力不足。

---

## 5. Python 互操作现状

| 方面 | 现状 | 评估 |
|------|------|------|
| 嵌入式 Python Console | 代码保留在 `src/python/`，但 `CMakeLists.txt` 中 `SICNU_EMBED_PYTHON=OFF` 为默认，`pybind11` 已移除 | ❌ 当前不可用 |
| Python API 设计 | `SicnuPythonApi` 已设计（`src/python/sicnu_python_api.h`），暴露项目、图层、栅格/矢量操作、处理算法、画布范围等接口 | ✅ 设计较完整 |
| 零拷贝绑定 | 尚未实现 `cv::Mat`/`ndarray` 或 `GDALDataset` 零拷贝传递 | ❌ 缺失 |
| 脚本编辑器 | 无 | ❌ 缺失 |

> 注：`CLAUDE.md` 第 37 行明确声明“100% C++，运行时无 Python”，与用户目标“Python 插件/算子扩展机制”存在方向性冲突，需要重新对齐。

---

## 6. Agent / CLI 现状

### 6.1 已具备

- **MCP Server**：`src/agent/mcp_server.cpp` 实现 stdio/JSON-RPC 2.0，提供：
  - `tools/list`
  - `list_algorithms`
  - `get_algorithm_schema`
  - `execute_algorithm`（异步，返回 execution_id）
  - `get_execution_status`
  - `cancel_execution`
  - `list_layers`
  - `describe_dataset`
- **STAC Client**：`src/agent/stac_client.cpp` 支持 STAC 目录搜索与 COG 流式加载。
- **测试覆盖**：`tests/test_mcp_server.cpp`、`tests/test_algorithm_schema.cpp` 已存在。

### 6.2 缺失

- `--pipeline pipeline.json` 无界面管道执行模式。
- 算法执行结果自动落盘/加载逻辑主要在 UI 层，headless 模式下缺少结果持久化规范。
- 缺少针对 Agent 的算子编排中间语言（如 JSON Pipeline DSL）。

---

## 7. 重构为 Agent-Ready 算子的关键痛点

### 7.1 高优先级痛点

| # | 痛点 | 影响 | 建议解决路径 |
|---|------|------|--------------|
| 1 | **参数格式碎片化**：`QVariantMap` vs JSON vs 对话框控件，Agent 难以统一生成参数 | Agent 开发成本高、易出错 | 引入 `RSOperator` 基类，要求 `run(const Json::Value&)`；QGIS 算法作为 adapter 包装 |
| 2 | **异常类型弱**：大量使用 `qDebug()`、`QMessageBox`、返回码，缺少结构化错误码 | Agent 无法自动分类和 Self-heal | 定义 `RSProcessingException`（含 ErrorCode、message、provider、algorithm） |
| 3 | **进度回调不统一**：多来源进度机制并存 | UI 和 Agent 都需要适配多套接口 | `RSOperator` 统一输出 `progress(double, QString)` 信号/回调 |
| 4 | **缺少 headless pipeline 执行器** | 无法实现 `--pipeline pipeline.json` | 新建 `rs_pipeline` 可执行程序或 `sicnu_geo_rs --pipeline` 模式 |
| 5 | **Python 运行时缺失** | 学生无法写 Python 脚本扩展算子 | 重新引入 pybind11，实现 `cv::Mat <-> numpy.ndarray` 零拷贝绑定 |

### 7.2 中优先级痛点

| # | 痛点 | 影响 | 建议解决路径 |
|---|------|------|--------------|
| 6 | 自研算法与 QGIS 算法并存，但命名空间/目录不一致 | 新开发者难以定位算法实现 | 统一目录：`src/operators/core/`、`src/operators/opencv/`、`src/operators/otb/`、`src/operators/gdal/` |
| 7 | 对话框层重复代码仍较多 | 维护成本高 | 全部迁移到 `RasterProcessingDialogBase` / `SicnuAlgorithmDialog` 模式 |
| 8 | OTB vendor 编译阻塞 | 无法使用 OTB C++ 算法，只能 QProcess 调用系统 `otbcli_*` | 继续推进 `SICNU_BUILD_OTB=ON` 修复，或优先使用 CLI wrapper |
| 9 | 缺少教学增强 UI：实时直方图拉伸、卷帘对比联动 | 教学体验未达标 | 在 `src/app/widgets/` 增加 `SwipeMapTool`、`HistogramStretchWidget` |
| 10 | 实验操作日志未结构化导出 | 学生写实验报告困难 | 扩展 `ErrorReporter`/`LogPanel` 为 `OperationLogger`，输出 JSON/CSV 实验报告 |

---

## 8. 风险评估

| 风险项 | 等级 | 说明 |
|--------|------|------|
| OTB vendor 编译 | 🔴 高 | 当前 OTB 对 GCC 16 + GDAL 3.13+ 不兼容，大量补丁未合入；若无法修复，需长期依赖系统 OTB CLI |
| Python 运行时重新引入 | 🟡 中高 | 与现有 `CLAUDE.md` 约束冲突，需明确是否恢复 pybind11；Python 版本与 Qt 信号槽交互需谨慎 |
| 主线程阻塞 | 🟡 中 | 部分对话框仍有同步执行路径（如分类交叉验证早期版本），需全面审计 |
| 大影像内存泄漏 | 🟡 中 | `QgsRasterLayer`/`GDALDataset` 生命周期在部分对话框中依赖 Qt parent，需增加 RAII 包装 |
| QGIS vendor 源码升级 | 🟢 低 | 当前 vendor 版本已满足教学需求，短期无需升级 |

---

## 9. 结论与下阶段建议

项目已具备**较好的基础**：QGIS 处理框架、自研遥感算法库、MCP Agent 入口、异步执行基础设施均已存在。但要实现用户提出的“Core-UI 分离、统一 RSOperator、Python 扩展、headless pipeline”目标，还需完成以下关键重构：

1. **设计并实现 `RSOperator` 基类**（`src/operators/framework/`），统一：
   - `Json::Value` 参数输入
   - 统一进度回调
   - 强类型异常（ErrorCode + ErrorMessage）
   - 元数据/JSON Schema 自描述
2. **将现有算法逐步迁移/包装为 RSOperator**：
   - 自研算法直接继承
   - QGIS 算法通过 adapter 包装
   - GDAL/OTB CLI 通过 wrapper 包装
3. **实现 headless pipeline 执行器**：
   - 新增 `--pipeline pipeline.json` CLI 模式
   - 支持步骤依赖、中间结果缓存、失败回滚
4. **恢复 Python 互操作能力**：
   - 重新引入 pybind11（可选编译）
   - 实现 `cv::Mat <-> numpy.ndarray` 零拷贝
   - 添加脚本编辑器 dock
5. **增强教学 UI**：
   - 卷帘对比（Swipe Tool）
   - 实时直方图拉伸
   - 实验操作日志导出

本报告作为 Phase 1 交付，为后续 Task Queue 的生成与主循环执行提供依据。
