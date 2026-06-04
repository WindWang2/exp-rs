# Georeferencer v1.5 (Backlog Closeout) 设计

**日期:** 2026-06-03
**Phase:** 11.5
**状态:** 设计完成，待写实现计划
**前置:** Phase 11.4 完成（`docs/superpowers/specs/2026-06-02-georeferencer-design.md`）

## 1. 目标

关闭 Phase 11.4 留下的 7 项 v1.0 限制，让 Georeferencer 真正可生产用 + 增量 SIFT 自动匹配。具体闭环：

- 用户可以自选目标 CRS（不只 EPSG:32650）
- 画布上可见 GCP 编号标记 + 残差箭头
- Image-to-Image 模式真正能加载第二张栅格
- DEM Z-offset 进入 GDAL warp pipeline
- RPC 模式接受 GCP 精化
- 真实 RPC 样本的 golden 回归测试
- 一键 SIFT 自动找匹配点

## 2. 架构变更

### 2.1 新依赖：OpenCV 4.5+

仅 `qgis_app_georef` 静态库链接。`find_package(OpenCV 4.5 REQUIRED COMPONENTS core features2d imgproc)`。SIFT 自 4.4+ 在主干（不需 `xfeatures2d`）。

**降级策略：** CMake 中用 `OPTIONAL_COMPONENTS` 包装；若 OpenCV 不可用，编译时定义 `RS_NO_OPENCV`，运行时 SIFT 按钮灰显，其他功能正常。

### 2.2 新文件

| 文件 | 责任 |
|---|---|
| `src/app/georeferencer/rs_sift_matcher.h/.cpp` | OpenCV 封装：SIFT 检测 + BFMatcher + RANSAC，纯 Qt 类型 API |
| `src/app/georeferencer/rs_sift_dialog.h/.cpp` | SIFT 参数对话框（灵敏度、最大匹配数、内点率、阈值） |
| `src/app/georeferencer/rs_sift_task.h/.cpp` | `QgsTask` 包装 + 协作式取消 + 进度回调 |

### 2.3 端口

| 上游文件 | 目标路径 | 注 |
|---|---|---|
| `qgis_ref/src/app/georeferencer/qgsgcpcanvasitem.{h,cpp}` | `src/app/georeferencer/qgsgcpcanvasitem.{h,cpp}` | Task 11.4.5 推迟，11.5.2 落地 |
| `qgis_ref/src/app/georeferencer/qgsresidualplotitem.{h,cpp}` | `src/app/georeferencer/qgsresidualplotitem.{h,cpp}` | 同上 |

### 2.4 修改的现有文件

- `src/analysis/georeferencing/qgsrpcgcptransformer.{h,cpp}` — `setRpcOptions(QString demPath, double zOffset, bool useGcpRefinement)` + 精化数学
- `src/app/georeferencer/rs_georef_params_panel.{h,cpp}` — CRS picker 取代 QLabel；REF raster 输入框；显示精化前/后 RMS
- `src/app/georeferencer/qgsgeoreferencermainwindow.{h,cpp}` — File 菜单新增 3 项；REF 画布私有 layer store；SIFT 按钮 wire；GCP canvas item 生命周期
- `src/app/georeferencer/qgsgeorefdatapoint.{h,cpp}` — Task 5 stub 落地，构造时 `new QgsGCPCanvasItem(srcCanvas, ...)` + ref
- `src/app/georeferencer/CMakeLists.txt` — OpenCV 链接、新源文件
- `CMakeLists.txt` 顶层 — `find_package(OpenCV 4.5 ...)`

## 3. 子任务设计

### 3.1 Task 11.5.1: CRS Picker

**UI：** `RsGeorefParamsPanel` "坐标系" section 第二行 "目标" 由 `QLabel` 改为 `QgsProjectionSelectionWidget`（QGIS 标准控件，项目 `qgis_gui` 库应已含）。

**数据流：**
1. 构造时从 `QgsSettings::value("Georeferencer/lastDestCrs", "EPSG:32650")` 还原
2. `crsChanged` → 保存设置 + emit `RsGeorefParamsPanel::destCrsChanged` → 主窗口 `recomputeFit()`
3. `destCrs() const` 返回 widget 当前 CRS

**降级：** 若 `QgsProjectionSelectionWidget` 不在 `qgis_gui` 中，自写极简 `QLineEdit` + EPSG 校验 + Browse 按钮弹出 `QgsCoordinateReferenceSystem::standardCrs()` 列表。

**测试：** `tests/test_crs_picker_persists.cpp` — 创建 panel + `setCrs("EPSG:4326")` → 析构 → 新建 panel → 断言 `destCrs().authid() == "EPSG:4326"`。

### 3.2 Task 11.5.2: GCP 画布标记

**端口：** `QgsGCPCanvasItem` 继承 `QgsMapCanvasItem`（qgis_core 有）；`QgsResidualPlotItem` 继承 `QgsMapDecoration`。用 Task 11.4.1 收紧后的 sed 端口 recipe。

**绘制规范（按 design.html）：**
- 圆圈半径 7 px，编号 9 pt IBM Plex Mono 居中
- SRC badge 蓝（`#1f6feb`），REF badge 绿（`#2da44e`），选中态填充黄（`#bf8700`）
- 残差箭头：起点 = 实际 dst，终点 = predicted dst；箭头长度 = 残差 × scale（默认 10×，状态栏显示）

**生命周期：** `QgsGeorefDataPoint` 构造时 `new QgsGCPCanvasItem(mSrcCanvas, this)` + 对应 REF；`mGcps::pointAdded` 信号触发主窗口 `new QgsGeorefDataPoint(gcpPoint, mSrcCanvas, mRefCanvas)`；`pointRemoved` → delete。

**测试：** `tests/test_gcp_canvas_item.cpp` — 构造 canvas + canvas item with id=3；render 到 QImage 200×200；断言中心区域有蓝色像素（≥ 10 个 RGB≈(31,111,235)）。

### 3.3 Task 11.5.3: Image-to-Image 模式

**File 菜单新增 3 项：**

```cpp
fileMenu->addAction(tr("Open source raster..."), this, &QgsGeoreferencerMainWindow::openSourceRaster);
fileMenu->addAction(tr("Load reference raster..."), this, &QgsGeoreferencerMainWindow::loadReferenceRaster);
fileMenu->addSeparator();
// (existing: Load .points, Save .points)
```

**REF 画布私有 store：** 主窗口持有 `QgsMapLayerStore *mRefStore`（独立于主应用 project）。`loadReferenceRaster(path)`：
1. 构造 `QgsRasterLayer(path, ...)`；校验有效 + CRS 非空
2. 加入 `mRefStore`
3. `mRefCanvas->setLayers({layer})`
4. 在 REF badge 副标题写文件名 + CRS authid

**模式切换：** `QgsGeoreferencerMainWindow` 连 `RsGeorefModeToggle::modeChanged`：
- `ImageToMap`: REF 画布 `setLayers(mIface->mainCanvas->layers())`（共享主应用）
- `ImageToImage`: REF 画布 `setLayers({mRefRaster})` 若已加载，否则状态栏提示 "请先 File → Load reference raster…"
- `RpcPhysical`: REF 画布隐藏（central widget QSplitter 切到 SRC-only），DEM section 可见

**禁用 Apply 条件加 1 条：** Image-to-Image 模式且 REF raster 未加载 → Apply disabled。

**测试：** `tests/test_image_to_image_load.cpp` — 切到 ImageToImage → `loadReferenceRaster(syntheticPath)` → 断言 `mRefCanvas->layerCount() == 1`，layer name 匹配；切回 ImageToMap → REF 不再含该 layer。

### 3.4 Task 11.5.4: DEM Z-offset 接线

**API：** `QgsRpcGcpTransformer::setRpcOptions(QString demPath, double zOffset)`：
- demPath 非空：`papszOptions` 加 `RPC_DEM=<demPath>` + `RPC_DEMINTERPOLATION=bilinear`；若 zOffset != 0 也加 `RPC_HEIGHT=<zOffset>` 作 fallback（GDAL 在 DEM 不可读时用）
- demPath 为空：仅 `RPC_HEIGHT=<zOffset>` 作为常量高度

**参数面板：** `demZOffset() const` 返回 spinner 值；`destCrsChanged` 和 `demZOffsetChanged` 都触发 `recomputeFit()`，后者重新构造 transformer。

**测试：** `tests/test_dem_z_offset.cpp` — 合成 RPC 栅格 + 同 GCP set，构造两个 transformer，一个 zOffset=0，一个 zOffset=100；调用 `transform(32, 32, ...)` → 断言两输出经度差 ≥ 0.0001°。

### 3.5 Task 11.5.5: RPC GCP 精化

**数学方法（v1 线性偏移）：**
1. 先用源栅格元数据构造 base RPC transformer（与 Task 11.4.8 一致）
2. 对每个启用 GCP：`forward(src_px, src_py, predLon, predLat)` → 计算残差 (residLon, residLat) = (actualLon - predLon, actualLat - predLat)
3. 平均所有 GCP 的残差 → (meanLonOff, meanLatOff)
4. 重新构造 transformer，`papszOptions` 加 `LAT_OFFSET=<rpc.LAT_OFF + meanLatOff>` 和 `LONG_OFFSET=<rpc.LONG_OFF + meanLonOff>` 覆盖（GDAL 的 `GDALRPCInfoV2` 字段可通过 papszOptions 覆盖）
5. 若 < 3 启用 GCP，跳过精化

**API：** `QgsRpcGcpTransformer::updateParametersFromGcps(src, dst, invertYAxis)` 不再忽略 src/dst；实现上文步骤。`setRpcOptions` 新增 `bool useGcpRefinement = true`，false 时只用元数据。

**面板显示：** `RsGeorefParamsPanel` "RMS 误差分布" section 显示两行：「精化前 RMS」「精化后 RMS」，差值高亮（绿色表示精化改善）。

**测试：** `tests/test_rpc_gcp_refine.cpp` — 合成 RPC + 3 GCP 带已知偏移（target = predicted + (0.01°, 0.005°)）→ 精化后 forward 经度应比未精化的更接近 target；RMS 应更小。

### 3.6 Task 11.5.6: 真实 RPC golden 样本

**数据来源：** USGS EarthExplorer 下载 LC09 L1TP scene → 用 `gdal_translate -srcwin 0 0 256 256` 裁剪 + 保留 RPC.txt 元数据；从 SRTM 30m 裁切对应区域 16×16 DEM tile。总 ≈ 3MB。

**入仓方式：** `tests/data/georef/real_rpc/{landsat_256.tif, dem.tif, golden_warp.tif, golden_warp.sha256}` 通过 git LFS。提供 `scripts/download_test_data.sh` 让没 LFS 的开发者也能拉。

**Golden 生成（一次性 setup）：** 在 GDAL 3.8.x 下手工跑 warp → 保存输出 → 计算 SHA256 → 写入 `golden_warp.sha256`。文档记录 GDAL 版本与命令。

**测试：** `tests/test_rpc_golden.cpp`：
- TEST_CASE "real RPC golden warp matches sha256" — 加载样本、构造 transformer、跑 warp、读输出、计算 SHA256 → 严格匹配。失败时附带：① 像素 diff（≥ 95% 像素差 ≤ 1 DN 也算 pass，便于 GDAL 小版本浮动）；② 生成 diff 图保存到 tmp 供人工比对。

**风险：** GDAL 版本浮动可能导致严格 SHA256 不匹配；用像素差容差作 fallback。

### 3.7 Task 11.5.7: SIFT 自动匹配

**Pipeline：**

```
用户点 [SIFT 自动匹配] (toolbar)
  → RsSiftDialog 弹出:
      - 灵敏度 slider (0.01-0.1, default 0.04, OpenCV contrastThreshold)
      - 最大匹配数 (10-500, default 100)
      - 最小内点率 (0.1-0.9, default 0.5)
      - RANSAC 阈值 (1-10 px, default 3.0)
      - 下采样最大边长 (512-4096, default 2048)
  → 用户确认 → 启动 RsSiftTask
  → 任务线程内:
      1. 读 SRC raster (GDAL → cv::Mat, 灰度 8-bit, 下采样)
      2. 读 REF raster 同上
      3. cv::SIFT::create(0, 3, contrastThreshold) -> detectAndCompute
         (检查 isCanceled() 在 detect 后)
      4. cv::BFMatcher(NORM_L2, crossCheck=true) -> matches
         (检查 isCanceled())
      5. cv::findHomography(srcPts, dstPts, RANSAC, threshold) -> inlier mask
      6. 过滤前 N 个 inlier matches (按 distance 排序)
      7. 反推 keypoint 到原栅格坐标 (乘 scale factor)
      8. 返回 QVector<QPair<QgsPointXY srcPx, QgsPointXY dstWorld>>
  → 任务完成 (GUI 线程):
      - 弹结果 dialog: "找到 N 对匹配, 内点 M 个 (内点率 r), 是否全部采用?"
        前 5 对预览（小缩略图 + 序号 + 距离）
      - 用户 OK → 批量 mGcps->appendPoint(...) for each inlier
      - QgsMessageLog 写: {"event":"sift_match","matches":N,"inliers":M,"inlier_ratio":r,"duration_ms":d}
```

**RsSiftMatcher API：**

```cpp
class RsSiftMatcher {
public:
    struct Params { double contrastThreshold = 0.04; int maxMatches = 100;
                    double minInlierRatio = 0.5; double ransacThreshold = 3.0;
                    int maxImageSide = 2048; };
    struct Match { QgsPointXY srcPx; QgsPointXY dstWorld; double distance; };
    struct Result { QVector<Match> inliers; int totalMatches = 0; double inlierRatio = 0; };

    explicit RsSiftMatcher(QgsFeedback *fb = nullptr);
    Result run(const QString &srcRaster, const QString &refRaster,
               const QgsCoordinateReferenceSystem &refCrs,
               const Params &params);
};
```

`refCrs` 用于把 REF 像素坐标转世界坐标（通过 refRaster 的 GeoTransform）。

**OpenCV 集成边界：** 仅 `rs_sift_matcher.cpp` `#include <opencv2/...>`；header 用 Qt 类型。这样 OpenCV 不污染其他 translation unit。

**测试：**
- `tests/test_sift_matcher.cpp`:
  - TEST_CASE "synthetic translation: 50 keypoints, inlier ratio > 0.6" — 合成 SRC + REF（REF = SRC 平移 (+50, +30) 像素 + 加 5% 高斯噪声）→ 运行 matcher → 断言 `inliers.size() >= 20`，`inlierRatio >= 0.6`，估算平均平移 ≈ (50, 30) 误差 ≤ 1 像素
  - TEST_CASE "OpenCV unavailable: graceful empty result" — `#ifdef RS_NO_OPENCV` 路径 → 断言 `result.inliers.isEmpty()` 且无崩溃

**取消语义：** OpenCV SIFT 内部不支持 cooperative cancel；在主要 stages 间检查 `feedback.isCanceled()`：① detect SRC 后；② detect REF 后；③ match 前；④ RANSAC 前。最坏情况用户等到 detector 跑完（2048×2048 约 5-10s）。

**进度：** task 期间报告 4 个阶段（25% / 50% / 75% / 100%）。

## 4. 数据流总览

### 4.1 模式切换 → 数据流

```
RsGeorefModeToggle::modeChanged(Mode m)
  ↓
QgsGeoreferencerMainWindow::onModeChanged(m):
  ImageToMap   → mRefCanvas->setLayers(mIface->mainCanvas->layers())
                 mParamsPanel->setRpcMode(false)
                 mGcpDock visible, paramDock visible
  ImageToImage → mRefCanvas->setLayers(mRefStore->mapLayers())  
                 若空: statusBar 提示 + Apply disabled
                 mParamsPanel->setRpcMode(false)
  RpcPhysical  → mCentralSplitter 切到 SRC-only 模式（隐藏 REF）
                 mParamsPanel->setRpcMode(true)  → DEM section 可见
                 模式 toggle 隐藏 sync zoom
  ↓
recomputeFit()  // CRS / DEM / refinement 变化都触发
```

### 4.2 GCP 生命周期（含画布标记）

```
用户 Add GCP 工具点击 SRC → showCoordDialog
  → 用户输入 dst → dialog accepted(QgsGcpPoint p)
  → mGcps->appendPoint(p)  // emit pointAdded
  → 主窗口监听 pointAdded:
      new QgsGeorefDataPoint(point, mSrcCanvas, mRefCanvas)
        构造时 new QgsGCPCanvasItem 给 SRC + REF
        item 加入对应 canvas scene
  → recomputeFit()
      → updateResiduals (写回 point->setResidual(...))
      → 通知 dataPoints 更新残差箭头 (paint)
      → setRmsValues → params panel + scatter
      → mGcpTable model dataChanged → 表格刷新
```

### 4.3 SIFT 流程（含取消）

```
toolbar Auto-match → openSiftDialog()
  → RsSiftDialog::accepted(params)
  → RsSiftTask(srcRaster, refRaster, refCrs, params)
  → QgsApplication::taskManager()->addTask(task)
  → run() (worker thread):
      - GDAL read + downsample → cv::Mat (gray 8-bit)
      - SIFT detect SRC -> checkCancel
      - SIFT detect REF -> checkCancel
      - BFMatcher -> checkCancel
      - findHomography RANSAC -> checkCancel
      - 反推坐标 -> result
  → taskCompleted (GUI thread):
      - 弹预览 dialog
      - 用户 OK → 循环 mGcps->appendPoint(...)
      - QgsMessageLog 结构化日志
  → taskTerminated (cancelled):
      - statusBar "已取消"
```

## 5. 测试矩阵

| 子任务 | 测试文件 | 关键断言 |
|---|---|---|
| 11.5.1 | `tests/test_crs_picker_persists.cpp` | setCrs → 重建 → 还原；destCrsChanged 触发 recomputeFit |
| 11.5.2 | `tests/test_gcp_canvas_item.cpp` | construct + setId + render QImage 非空，中心区域有 badge 色像素 |
| 11.5.3 | `tests/test_image_to_image_load.cpp` | mode 切换 + 加载 raster → REF layerCount==1；模式切回 → REF 恢复 |
| 11.5.4 | `tests/test_dem_z_offset.cpp` | z=0 vs z=100 → forward 经度差 ≥ 0.0001° |
| 11.5.5 | `tests/test_rpc_gcp_refine.cpp` | 3 GCP 带偏移 → 精化后 RMS < 未精化 RMS |
| 11.5.6 | `tests/test_rpc_golden.cpp` | 真实样本 warp → SHA256 或 ≥ 95% 像素差 ≤ 1 DN |
| 11.5.7 | `tests/test_sift_matcher.cpp` | 合成平移 → inlier ≥ 20，平移误差 ≤ 1 px；OpenCV 缺失 graceful |

总测试 239 + 7 = 246（11.5.7 至少 2 TEST_CASE）。

## 6. 风险

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| 1 | OpenCV 在 Windows/macOS CI 装失败 | 中 | 高 | CMake `find_package(OpenCV ... OPTIONAL_COMPONENTS)`；`RS_NO_OPENCV` 编译时定义；SIFT 按钮灰显 |
| 2 | 真实 RPC 样本数据分发 | 中 | 中 | git LFS + `scripts/download_test_data.sh` 兜底 |
| 3 | RPC refinement 数学不稳健 | 中 | 中 | 线性 bias 方法（最稳）；< 3 GCP 跳过；面板提示 |
| 4 | `QgsProjectionSelectionWidget` 不存在 | 低 | 低 | 降级自写 QLineEdit + EPSG 校验 |
| 5 | SIFT 大图 OOM | 低 | 中 | 下采样到 2048 边长；进度报告 |
| 6 | `QgsGCPCanvasItem` 端口拉依赖 | 低 | 中 | Task 5 已勘察；爆破时自写极简 QGraphicsItem |
| 7 | DEM Z-offset 无 GDAL 直接 API | 低 | 低 | 退化为常量高度模式 |
| 8 | GDAL 版本浮动导致 golden SHA 失配 | 中 | 低 | 测试用像素差容差作 fallback |

## 7. 子任务执行顺序

按依赖图：

```
11.5.1 CRS Picker         ─┐
11.5.2 GCP Canvas Item    ─┤
11.5.3 Image-to-Image     ─┼─ 并行可
11.5.4 DEM Z-offset       ─┘
                           ↓
11.5.5 RPC GCP Refine  (依赖 11.5.4)
                           ↓
11.5.6 Real RPC Golden (依赖 11.5.4 + 11.5.5)
                           ↓
11.5.7 SIFT (独立，建议最后做以隔离 OpenCV 集成风险)
```

实现时按 11.5.1 → 11.5.2 → 11.5.3 → 11.5.4 → 11.5.5 → 11.5.6 → 11.5.7 顺序，每步独立 TDD + commit。

## 8. Done When

- 246+ Catch2 测试全绿（含 11.5 新增 7+ 个）
- 手工烟雾：真实 RPC 样本 → SIFT 自动找 ≥ 15 GCP → CRS picker 选 EPSG:4326 → Apply → 输出 GeoTIFF 验证
- 画布显示 GCP 编号标记 + 残差箭头
- design.html 视觉 review（双栅格 image-to-image 模式）
- CMake 在无 OpenCV 环境下 SIFT 按钮灰显，其他功能不受影响
- 结构化日志：SIFT 完成后 `QgsMessageLog` `Georeferencer` tag 写入 `event=sift_match` JSON 行

## 9. 已知未决

- **Headless CI 中的 OpenCV 测试** — SIFT 测试需要 OpenCV 可用；若 CI 环境装不上需要标记 `[!skip]`
- **真实 RPC 样本的法律分发** — Landsat L1TP 是公共领域，SRTM 同；入仓应无问题，文档记录来源 URL + 下载日期
- **`QgsProjectionSelectionWidget` 端口路径** — 待 11.5.1 实施时确认在项目 `qgis_gui` 库中是否已有；不在则用降级方案
