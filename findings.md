# Findings & Decisions — SICNU GEO RS

## Phase 10A Classification 实施记录 (2026-06-04 完成)

### 偏离原始计划

1. **K-Means 精度评价跳过** (review patch + Task 10.9)
   - cluster ID 是 1..K 的任意置换，与 ROI class ID 不对应
   - 直接算混淆矩阵会给出误导性结果 (低精度，但其实是 label 不齐)
   - 决定: `RsClassificationTask::run` 检查 `algoName == "KMeans"` 时不算 accuracy
   - 完整修复需要 Hungarian assignment 算法，留 Phase 10A.1

2. **交叉验证 stub 化** (review patch)
   - 5-fold CV 完整实现需要每 fold 重建 backend + label re-mapping
   - 超过 50 行阈值，按 spec 允许 stub 为 QMessageBox "Phase 10A.1"
   - 信号已 connect，未来填充函数体即可

3. **`RsRoiToolBase` 不能仅头文件** (Task 10.4)
   - Q_OBJECT 在仅头文件基类上，子类 MOC 找不到 `staticMetaObject`
   - 必须有 stub `.cpp` (1 行 include 也行) 进 CMake AUTOMOC 列表
   - Phase 11.4 没遇到因为 Georeferencer 的 base 类都有非 trivial 实现

4. **ColorTable 索引 0 默认黑** (review patch Bonus)
   - GDAL `SetColorEntry(0, ...)` 不显式设的话索引 0 = 黑
   - 未分类像素显示黑色与 6 类配色不协调
   - 决定: 写出前显式设 0 索引为 alpha=0 透明

5. **classDefs() 返回 by value** (Task 10.1 + 10.3 累计观察)
   - `QHash<int, RsClassDef>` 每次访问 deep copy
   - 6 类规模没问题，但 widget 重建时多次调用累计开销
   - 未来若类别数增加 (>50) 应改 `const QHash &` 返回

### 算法选择得失

- **OpenCV ML 三件套都顺利集成**：NormalBayes / SVM(RBF) / K-Means。NormalBayes API 自然，SVM 默认 C=10/γ=0.5 教学够用，K-Means 用 `cv::kmeans` 而非 `cv::ml::KMeans` (后者 API 不友好)
- **JM 算法 ε=1e-6 ridge** 很关键，小样本类 (2 像元) 协方差秩亏不加 ridge 直接发散
- **GDAL `GDALRasterizeGeometries` + MEM 驱动** 比手写扫描线/Bresenham 简洁，精确度匹配 GDAL 用户预期

### 工程实践

- **Tile-streamed predict** (256×256) 比一次性整图 predict 内存峰值降 100×
- **`cls_id` 字段名** 避开 OGR `class` 关键字风险，确认有效
- **像素索引 `uint64` 编码 `row * W + col`** 跨任务一致，1 万像元 ROI 占 80KB
- **70/30 分层抽样** `std::mt19937(42)` 确定性种子，测试可重现
- **QgsTask + QgsFeedback 模式** Phase 11.4.7 的 `RsWarpTask` 模板复用度 90%

### Phase 10A v1.0 已知限制

- K-Means 精度评价 (Hungarian assignment 未实现)
- 5-fold CV (stub)
- 设计稿视觉 review (mimo-v2.5 `ui_diff_check` 未跑)
- 真实 Sentinel-2 / Landsat 数据手工烟雾 (无 X display)
- 快速预览延迟基线 (< 2s 未实测)
- ROI 顶点编辑 (整删重画)
- 训练模型 .yml 加载入口
- 混淆矩阵 PDF 导出 (有 CSV)

---

## Phase 10A Classification 设计决定 (2026-06-04)

### Phase 10 分两个模块
- **10A 像元级**（本 phase）：每像元光谱特征 → 分类器 → 标签图
- **10B 面向对象 OBIA**（后续 phase）：影像分割 → 段级特征 → 分类器 → 标签图
- 共用 `RsClassifierBackend` 抽象（cv::ml 包装）；OBIA 多一层 `RsImageSegmenter`
- 分割算法 10B 时再定（OTB MeanShift / OpenCV contrib SLIC / 自写 SLIC）

### 算法后端：OpenCV ML（不是 OTB CLI）
- 复用 Phase 11.5 引入的 OpenCV 4.5+
- 主优势：同一编译单元 + 无子进程 IO + 与 SIFT 统一
- v1 三件套：`cv::ml::NormalBayesClassifier`（最大似然代理）+ `cv::ml::SVM`（RBF 核）+ `cv::kmeans`
- v2 stretch：`cv::ml::RTrees`（随机森林）/ Mahalanobis / 深度学习 UNet（UNet 极可能去 ONNX runtime 路线）
- 强依赖（不 OPTIONAL）：无 OpenCV 时菜单灰显，不进窗口；OPTIONAL 灰显 UI 误导学生

### v1 算法 + 样本评估清单
- 监督：NormalBayes (Maximum Likelihood) + SVM (RBF, C=10 默认, γ 网格搜索)
- 非监督：K-Means
- 样本评估：JM (Jeffries-Matusita) 分离度矩阵（用户指定加）
- 精度评价：混淆矩阵 + Kappa + per-class Producer/User Accuracy + F1

### UI 形态
- 独立 QMainWindow，对齐 Phase 11.4 Georeferencer 节奏
- 严格按 `UI/design.html` `ArtboardClassify` 布局：左 240 (图层 + 类别快览) + 中央画布 + ClassifierBar 72 + 右 380 (类别管理 + JM 矩阵) + 底 180 (光谱曲线 dock)
- 6 类默认配色（林地/草地/水体/建成区/耕地/裸地）

### ROI 数据存储
- 几何 + 类别 ID + **像素索引集**（uint64 vector，一次性算入）双存
- 像素索引集大小：1 万像元 × 8 byte = 80KB / ROI，可接受
- 编辑 ROI 几何时重算像素索引
- Shapefile 字段名用 `cls_id`（避开 OGR 关键字风险）
- 类别定义 sidecar JSON `<rois>.classes.json`（id/name/color）

### JM 数学护栏
- 协方差矩阵加 ε=1e-6 ridge 防奇异（小样本类）
- 公式：B = ⅛(μ₁-μ₂)ᵀΣ̄⁻¹(μ₁-μ₂) + ½ln(detΣ̄/√(detΣ₁·detΣ₂))
- JM = 2(1-exp(-B)) ∈ [0, 2]
- 热图阈值：≥1.9 绿 / 1.5-1.9 黄绿 / 1.0-1.5 黄 / <1.0 红

### 性能 / 内存策略
- 分块 predict 256×256 tile + 流式写出（不持有整张 X 矩阵）
- 大栅格内存峰值控制：1.4GB → 几 MB 工作集
- 快速预览路径：仅 SRC 画布 viewport 内采样 + 训练 + predict，目标 < 2s
- JM 矩阵 ROI 改动 → 500ms 节流重算

### 推迟到未来
- Random Forest / Mahalanobis / UNet（顶栏占位灰）
- 折线 ROI / SLIC / SAM AI（顶栏占位灰）
- K-Means 类编号 → 语义类映射
- ROI 顶点编辑（增删拖拽）
- 训练模型 .yml 加载入口
- 混淆矩阵 PDF 导出（等 Phase 12 文档统一）

---

## Phase 11.5 Georeferencer v1.5 实施记录 (2026-06-04 完成)

### 偏离原始计划的实现

1. **Task 11.5.6 走合成 golden 路径**（subagent 环境无 USGS 交互登录）
   - 原计划：LC09 L1TP 256×256 + SRTM 30m DEM via git LFS
   - 实际：合成 256×256 非平凡 RPC 栅格（LINE_NUM_COEFF[3]=0.4，SAMP_NUM_COEFF[3]=0.3 引入 height 耦合）+ 16×16 斜面 DEM；用 first-run capture 模式生成 SHA256
   - 优点：可重现、无外部依赖、CI 友好；缺点：仍不验证算子在真实场景的绝对精度
   - 未来：等条件允许时按 `scripts/download_test_data.sh` README 升级到真实样本

2. **OpenCV 加 `calib3d`**（spec 漏列）
   - spec §2.1 列了 `core features2d imgproc`，但 `cv::findHomography` 是 `calib3d`
   - subagent 自动补上；CMake 最终 `find_package(OpenCV 4.5 QUIET COMPONENTS core features2d imgproc calib3d)`

3. **`SICNU_HAS_OPENCV` 编译宏（不是 `RS_NO_OPENCV`）**
   - spec §2.1 写成 `RS_NO_OPENCV`（无 OpenCV 时定义），但 CMake 自然方向是 "found → set HAS_OPENCV"
   - 实际：`if (OpenCV_FOUND) set(SICNU_HAS_OPENCV TRUE)`，源码 `#ifdef SICNU_HAS_OPENCV`
   - 语义一样，正向命名更清晰

4. **Task 11.5.5 精化前/后 RMS 比较**：实现了 `setRefinementRms(before, after)` API + 面板 labels，但 `recomputeFit()` 暂未两次跑 transformer 算对比（性能成本 vs 显示价值，留作 v2 polish）

5. **`updateMarkers()` 模式切换后重绘**：Task 11.5.2 的 `QgsGCPCanvasItem` 没做 `destinationPointCrs → REF 画布 CRS` 重投影；Task 11.5.3 在 `onModeChanged` 末尾遍历 `mDataPoints` 调 `updateMarkers()` 强制重读源 GCP 坐标缓解，但跨 CRS 场景仍可能漂移

### Phase 11.5 实施踩到的小坑

- **`QgsGCPList` ctor 无参 + `setParent()`**：Phase 11.4.3 的 redesign 没加 parent 形式构造，新代码沿用该模式
- **`QgsTask::taskCompleted` 信号 vs `result()` 访问**：SIFT 任务完成后 `result()` 在 taskCompleted 槽里访问；任务管理器在 taskCompleted/taskTerminated 信号发完后才释放任务，所以裸指针在槽里有效
- **OpenCV detector 不可协作中断**：cv::SIFT::detectAndCompute 一旦开始 GUI 必须等它完成（典型 5-10s on 2048 边长）。退而求其次：在 detect-src / detect-ref / match / RANSAC 阶段之间检查 `mFb.isCanceled()`
- **`cv::SIFT::create(nfeatures=0, nOctaveLayers=3, contrastThreshold)`**：OpenCV 4.4+ SIFT 进主干（不需 contrib `xfeatures2d`）
- **Catch2 `SKIP(...)`**：在 `<catch2/catch_test_macros.hpp>`；OpenCV 缺失时第一个 TEST_CASE 自动 skip，第二个（missing files）总能跑

### Phase 11.5 v1.0 已知限制

- 真实 LC09 / GF-2 样本仍未入仓（合成 golden 替代）
- 设计稿 `mimo-v2.5 ui_diff_check` 视觉 review 未跑
- SIFT 取消语义偏弱（detector 内部不可中断）
- canvas item 跨 CRS 重投影未实现
- 精化前/后 RMS 实时对比待 polish

---

## Phase 11.4 Georeferencer 设计决定 (2026-06-02)

### UI 形态偏离 QGIS 原版
- QGIS Georeferencer 是单画布 + 弹出对话框；`UI/design.html` `ArtboardGeoref` 是 ENVI 风格双画布 + 内嵌右 dock
- 决定：UI 主体重写以匹配 design.html；算法/数据模型/map tool 直搬 QGIS

### 8 种变换 (含 RPC 新增)
- 原版 7 种：Linear / Helmert / Polynomial1-3 / TPS / Projective
- 新增第 8 种：`TransformMethod::RpcPhysical = 7`，包装 GDAL `GDALCreateRPCTransformerV2`
- RPC `minimumGcpCount() == 0` —— RPC 系数从源栅格元数据读，GCP 仅用于 refine

### `.points` 文件 v2 格式
- v1（QGIS 原版）：无版本头，列 = mapX/mapY/pixelX/pixelY/enable/dX/dY/residual
- v2（本项目）：首行 `# QGEOS .points v2`，新增第 9 列 `pointType`（道路/河流/桥梁/十字/建筑/其他）
- 读 v1 时 type 列默认空字符串；写永远 v2

### 双画布同步策略
- 直接接 `extentsChanged` 会信号风暴；用 `QTimer` 16ms 单次定时器 coalesce + `mApplying` 重入守卫
- 性能基线：GF-2 (~160MB) 拖拽 30 FPS 不掉帧（在 Task 5 子任务里验证）

### GDAL 版本依赖
- `GDALCreateRPCTransformerV2` 在 GDAL ≥ 3.4 稳定；CMake `find_package(GDAL 3.4 REQUIRED)`
- Windows MSVC：`qgis_analysis` 作 STATIC 库，导出宏 `QGIS_ANALYSIS_EXPORT` 在 `qgis_analysis_export.h` 定义

### 推迟到 Phase 11.5 的 stretch
- 自动匹配 SIFT（顶栏按钮 v1 占位"敬请期待"）
- 从主地图选点辅助（image-to-map 便捷功能）

---

## Phase 11.4 Georeferencer 实施记录 (2026-06-03 完成)

### 偏离原始计划的几处实现

1. **`QgsGCPList` 重设计为 `QObject`**（Task 3 期间发现）
   - 原计划：保留 QGIS 上游的 `QList<QgsGcpPoint*>` 派生形式
   - 实际：改成 `QObject` 子类 + 内部 `QVector<QgsGcpPoint*>`，便于发射 `changed()` 信号给参数面板和 RMS scatter
   - 副作用：上层 `addPoint()` / `appendPoint()` 等用 `mPoints.size()` 取代 `count()`；遍历改用 `*this` (重载了迭代器)

2. **`QgsGCPCanvasItem` 推迟**（Task 5 期间决定）
   - 原计划：搬运上游 `QgsGCPCanvasItem` 在双画布上画 GCP 标记
   - 实际：双画布只显示底图；GCP 序号画到表格，不画到画布
   - 推迟原因：避免和 `RsTwinCanvasSyncController` 的 extent 同步逻辑耦合；v2 再加

3. **端口 recipe 收紧** (Task 1 总结后)
   - 原 sed 一行 `s/SIP_[A-Z_]+(\([^)]*\))?//g` 会破坏 `#ifndef SIP_RUN` 和 `#define SIP_NO_FILE`
   - 收紧成多步：按整行清理 `SIP_ABSTRACT/NO_FILE/EXPORT` → inline 单行清理 `SIP_INOUT/OUT/FACTORY/...` → perl 多行清理 `SIP_THROW(...)`
   - 文档化在 plan 文件的"Lessons from Task 1"一节

4. **`QgsRpcGcpTransformer::transform()` 签名陷阱** (Task 8 期间)
   - plan 文档建议 5 参数 `(double x, double y, double &fx, double &fy, bool inverse)`
   - 实际基类只有 3 参数 in-place `(double &x, double &y, bool)`；具体 transformer 实现 `GDALTransformer()` + `GDALTransformerArgs()` 两个虚函数，基类用这俩委派
   - 所以 RPC transformer **不重写** `transform()`，而是返回 `GDALRPCTransform` 函数指针和 RPC arg 给基类

5. **`CSLConstList` 类型** (Task 8 编译期发现)
   - GDAL 3.4+ 把 `GDALDataset::GetMetadata()` 返回类型从 `char**` 改成 `CSLConstList`（即 `const char* const*`）
   - 不能直接传给 `CSLSetNameValue`（会编译报错 `invalid conversion`），但可以传给 `GDALExtractRPCInfoV2`
   - 解决：本地用 `CSLConstList md = ds->GetMetadata("RPC");`

6. **Qt 测试不能用 `isVisible()` 在未 show 的窗口上** (Task 8 测试调试)
   - 测试构造 `QgsGeoreferencerMainWindow w(nullptr)` 不调用 `w.show()`
   - 在这种情况下所有子 widget 的 `isVisible()` 都返回 false（包括刚 `setVisible(true)` 的）
   - 必须用 `!isHidden()` 来读 widget 的"本地可见意图"
   - 这是 Qt 文档明确说明的："A widget is visible if it is mapped to the screen"

7. **`QComboBox` 行隐藏需要 `QListView` 视图**
   - 默认 view 不支持 `setRowHidden`
   - 在构造时显式 `mTransformCombo->setView( new QListView( mTransformCombo ) );` 才能调用

### Phase 11.4 v1.0 已知限制（用户会遇到）

- **`destCrs()` 硬编码 EPSG:32650**：参数面板还没有 CRS picker，warp 永远输出 WGS 84 / UTM zone 50N（北京-河北一带）。其他区域用户必须改源码或等 11.5。
- **Image-to-Image 模式切换不切换画布内容**：mode toggle 改 DEM section 可见性，但目标画布始终是地图，不会自动加载第二张栅格。该 stretch 推迟到 Phase 11.5。
- **RPC 合成栅格测试用单位多项式**：实际卫星栅格的 RPC 是高阶多项式 + 偏移/缩放；该测试只验证基础接线，不验证算子精度。
- **DEM Z 偏移 spinner UI-only**：spinner 值不传给 `GDALCreateRPCTransformerV2`（GDAL 没有直接的 Z-offset 参数，需要 DEM 文件级别偏移）；保留为 v1 placeholder。
- **`QgsGCPList::updateResiduals` 不区分 src/dst CRS**：测试通过用相同 CRS 调用规避；image-to-map + 不同源 CRS 场景下残差读数可能不准。

---

## Requirements

- Pure C++ remote sensing analysis platform based on QGIS engine
- Embedded GDAL/OTB tools for RS processing
- QGIS-style GUI (map canvas, layer tree, processing toolbox)
- Plugin architecture for extensibility
- Python console for user scripting (optional, not runtime dependency)
- Remote sensing specific workflows (atmospheric correction, indices, classification, etc.)

## Research Findings

### QGIS Source Architecture
- QGIS core (`src/core/`) provides: layers, rendering, CRS/coordinate transforms, geometry, data providers, expressions
- QGIS GUI (`src/gui/`) provides: map canvas, map tools, layer tree model/view, dialogs
- QGIS native (`src/native/`) provides: platform-specific integration
- QGIS uses CMake build system with modular library structure

### Processing Framework
- QGIS processing uses a provider/algorithm model:
  - `QgsProcessingProvider` registers algorithms
  - `QgsProcessingAlgorithm` defines parameters and execution
  - `QgsProcessingDialog` provides UI for parameter input
- External tools (GDAL, OTB) are wrapped via CLI command builders
- `ToolPathManager` discovers external tools on PATH or configured locations

### GDAL Tools Available
- `gdal_translate` — format conversion, subsetting, resampling
- `gdalwarp` — reprojection, mosaicking, clipping
- `gdalinfo` — metadata extraction
- `gdaldem` — DEM products (hillshade, slope, aspect)
- `gdal_grid` — gridding/interpolation
- `gdal_rasterize` — vector to raster
- `gdalbuildvrt` — virtual raster mosaic
- `gdal_contour` — contour extraction
- Plus 15+ more tools

### OTB Tools Available
- `BandMath` — band math expressions
- `Segmentation` — image segmentation
- `ExtractROI` — ROI extraction
- `OpticalCalibration` — optical calibration
- `BundleToPerfectSensor` — pan-sharpening
- `Classification` — supervised classification
- `ComputeImagesStatistics` — statistics computation
- `DEMCalibration` — DEM calibration
- Plus 19+ more tools

### Build Dependencies
- Qt6 (Core, Gui, Widgets, Xml, Network, Svg, PrintSupport, OpenGL, Concurrent, Sql, UiTools)
- QGIS core libraries (compiled from vendored source)
- GDAL/OGR (for data provider support)
- PROJ (coordinate transformation)
- SQLite + SpatiaLite
- pybind11 (Python console plugin)
- nlohmann_json (JSON handling)
- Qwt (stubs provided for histogram/plot widgets)

## Technical Decisions

| Decision | Rationale |
|----------|-----------|
| Vendor QGIS source directly | Full control over build, no external QGIS installation required |
| Use QGIS coding conventions | Consistency with upstream, easier to merge updates |
| CLI wrappers for GDAL/OTB first | Fastest path; API binding as later optimization |
| Stub Qwt headers | Avoid full Qwt dependency while keeping QGIS GUI code compilable |
| pybind11 for Python embedding | Mature, well-documented, good Qt integration |
| QT_NO_KEYWORDS globally | Avoid conflicts with Python/pybind11 `signals`/`slots` macros |
| Plugin interface via pure virtual class | Clean separation, dynamic loading capability |

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| QwtPlot vtable corruption in histogram widget | Skip QgsRasterHistogramWidget construction entirely |
| HistoryProviderRegistry crash on addDefaultProviders() | Comment out — not needed for basic dialog functionality |
| QgsCodeEditorHTML not available | Replace with QTextEdit for map tip widget |
| Python `__pycache__` and `.pyc` files in tree | Removed all Python runtime code in cleanup commit |
| Massive uncommitted changes (344 files) | Split into logical commits: C++ fixes + Python removal |

## Engineering Review Findings (2026-05-30)

### Architecture Issues
1. **main.cpp monolithic** (1057 lines) — entire main window defined inline. Split into main_window.h/cpp + layer_tree_menu.h/cpp.
2. **Processing duplication** — sicnu_native (15 algorithms) duplicates qgis_algorithms (13 algorithms). Merge into qgis_algorithms.
3. **Hardcoded paths** — `/home/kevin/projects/exp-rs` and `sample_crops.tif` in main.cpp. Fix with QApplication::applicationDirPath().
4. **Dual plugin systems** — legacy QgisPlugin (unused) coexists with SicnuPluginInterface. Remove legacy.
5. **No GDAL/OTB error handling** — CLI wrappers have no unified error reporting. Add to wrapper base classes.

### Code Quality Issues
6. **Phase 4 scope too broad** — 7+ feature areas in one phase. Split into 4A (core) and 4B (advanced).
7. **No test framework** — zero C++ tests. Set up Catch2 in Phase 3.5.
8. **Stub surface area** — 29 stub files (Qwt 17, QScintilla 12). Record risk, handle in Phase 5.

### Performance Issues
9. **GDAL/OTB CLI overhead** — each call spawns QProcess. Implement GDAL C API in Phase 4A.
10. **No progress reporting** — long operations have no feedback. Add unified progress in Phase 5.
11. **No intermediate caching** — multi-step workflows recompute from scratch. Add caching in Phase 4A.

### Codebase Statistics
- Total source files: ~4385
- Processing algorithms: 58 (across 4 providers)
- Stub files: 29 (Qwt 17, QScintilla 12)
- Python bindings: 22 classes
- Plugins: 3 (layer_tree, processing, python_console)

## Resources

- QGIS source: vendored at `src/core/`, `src/gui/`, `src/native/`
- GDAL documentation: https://gdal.org/
- OTB documentation: https://www.orfeo-toolbox.org/
- Qt6 documentation: https://doc.qt.io/qt-6/
- pybind11 documentation: https://pybind11.readthedocs.io/

## Visual/Browser Findings

_(No visual/browser findings yet — this is a CLI/code project)_

## Known Limitations (Phase 3.5)

### Stub Headers (29 files)
- **Qwt stubs** (`src/stubs/qwt/`): 29 stub headers for QwtPlot, QwtSymbol, etc.
  - Impact: QgsRasterHistogramWidget and other Qwt-dependent widgets cannot render
  - Resolution: Replace with QtCharts in Phase 5
  - Risk: Any code path constructing Qwt objects will crash at runtime

### Disabled Features
- **Python embedding**: Code commented out in main.cpp and main_window.cpp
  - PythonConsoleWidget dock disabled
  - QgisPython::initialize/loadBindings/finalize calls disabled
  - Resolution: Re-enable when pybind11 console plugin is ready

### Pre-existing Issues
- **QgsStacItemItem::stacController()**: Undefined symbol in qgis_gui
  - Impact: Linker warning (not blocking build)
  - Resolution: Needs stub or implementation in QGIS GUI source

### Architecture Notes
- Provider consolidation complete: sicnu_native merged into qgis_algorithms (28 algorithms)
- 3 providers remain: qgis_algorithms, gdal_tools, otb_tools

## Phase 4A: RS Core Algorithms

### GDAL C API Wrapper
- `GdalDatasetWrapper` wraps GDAL C API with RAII (GDAL 3.13.0)
- Key functions: `GDALOpen`, `GDALClose`, `GDALRasterIO`, `GDALGetRasterBand`
- Move-only semantics (no copy) to prevent double-close
- Lazy init via `GDALAllRegister()` (once per process)
- Band data read as `GDT_Float32` regardless of source type

### Spectral Indices
- 6 indices: NDVI, EVI, SAVI, NDWI, NDBI, MNDWI
- All operate on raw float arrays (band data)
- Division by zero → NaN (IEEE 754 compliant)
- SAVI uses L=0.5 (standard vegetation adjustment factor)

### Band Math Engine
- Recursive descent parser with AST evaluation
- Grammar: expr→term→factor, supports +, -, *, /, (), unary negation
- Band references: b1..bN (1-based), validated against provided data
- Operator precedence: * / before + - (standard math)
- Virtual `collectRefs()` on AST nodes (no RTTI needed)

### DOS Atmospheric Correction
- DOS1: `surface = radiance - min(radiance)` (dark object subtraction)
- DOS2: `surface = (radiance - path_radiance) / transmittance` (with transmittance correction)
- DN→radiance: `L = gain * DN + bias` (from metadata)
- Transmittance estimate: `T = exp(-0.1 * airmass)` (clear atmosphere, tau≈0.1, visibility~23km)
- Each band corrected independently (min found per-band)
- Shared `convertAndFindMin()` helper eliminates duplication

### GDAL Error Handler
- `GdalErrorHandler` installs custom callback via `CPLSetErrorHandler()`
- Static `s_activeHandler` pointer routes GDAL callbacks to active handler instance
- Captures: error severity (CPLErr), error number (CPLE_XXX), error message
- `GdalDatasetWrapper::lastError()` captures errors on open failure via `CPLGetLastErrorMsg()`
- Handler instances are independent — only installed handler receives callbacks
- Destructor auto-uninstalls handler (RAII pattern)

## Phase 5B: Algorithm Organization (Task 5B.13)

### Tags and GroupId Implementation
- All 46 algorithms across 3 providers now have `tags()` and `groupId()` methods
- GDAL wrapper base class provides default `groupId() = "gdal"`, individual algorithms override
- OTB wrapper base class provides default `groupId() = "otb"`, individual algorithms override
- QGIS algorithms use QGIS upstream group IDs (vectoranalysis, rasteranalysis, etc.)
- Tags use `QObject::tr()` for internationalization support

### GroupId Categories
| Provider | Group IDs |
|----------|-----------|
| GDAL | rasteranalysis, rasterconversion |
| OTB | radiometry, learning, feature, segmentation, utilities, stereo |
| QGIS | vectoranalysis, vectoroverlay, vectorgeometry, rasteranalysis, rastertools, interpolation |

### Test Coverage
- 4 test cases, 251 assertions
- Validates: non-empty group, non-empty groupId, tags present, group consistency
- Groups bounded: 3-20 groups per provider (prevents fragmentation)
