# Georeferencer (几何校正) 模块设计

**日期:** 2026-06-02
**Phase:** 11.4
**状态:** 设计已确认，待写实现计划

## 1. 目标

为 SICNU GEO RS 添加 Georeferencer 几何校正模块，对齐 QGIS Georeferencer + ENVI Registration 工作流，提供：

- 三种校正模式：Image → Map / Image → Image / RPC 物理模型
- 七种二维变换 + RPC 物理模型，共八种 transformer
- GCP 采集、编辑、持久化（`.points` 文件）
- 残差/RMS 实时反馈（表列 + 残差箭头叠加 + 散点图）
- GDAL warp 输出 GeoTIFF + 可选加入工程

服务于本科遥感原理实验（Lab #9/10）以及作为 RS AI Agent 的几何校正工具能力。

## 2. UI 参考

`UI/design.html` → `ArtboardGeoref`（07 · 几何校正 (GCP)），1600×1000 双画布并排布局。本设计 UI 章节严格按该 mockup 实现。

## 3. 架构与代码分层

### 3.1 新建静态库 `qgis_analysis`

```
src/analysis/
└── CMakeLists.txt                          (新)
└── georeferencing/
    ├── qgsgcppoint.h/.cpp                  (直搬自 QGIS)
    ├── qgsgcptransformer.h/.cpp            (直搬)
    ├── qgsleastsquares.h/.cpp              (直搬)
    ├── qgsgcpgeometrytransformer.h/.cpp    (直搬)
    └── qgsvectorwarper.h/.cpp              (直搬，v1 保留接口供后续使用)
```

依赖：`qgis_core` + GDAL。CMake 强制 `find_package(GDAL 3.4 REQUIRED)`（`GDALCreateRPCTransformer` 在 GDAL 3.4 之前 API 行为不稳定）。Windows MSVC 构建时，`qgis_analysis` 作为 STATIC 库 export 符号需在 `qgis_analysis_export.h` 显式声明，避免 dllexport 噪音。

### 3.2 新建应用层目录 `src/app/georeferencer/`

```
qgsgeoreferencermainwindow.h/.cpp           (重写主窗口外壳)
qgsgcplist.h/.cpp                           (直搬)
qgsgcplistmodel.h/.cpp                      (直搬)
qgsgcplistwidget.h/.cpp                     (重写表格视觉)
qgsgcpcanvasitem.h/.cpp                     (直搬)
qgsgeorefdatapoint.h/.cpp                   (直搬)
qgsgeorefdelegates.h/.cpp                   (直搬 + 加类型列 delegate)
qgsgeoreftooladdpoint.h/.cpp                (直搬)
qgsgeoreftooldeletepoint.h/.cpp             (直搬)
qgsgeoreftoolmovepoint.h/.cpp               (直搬)
qgsgeoreftransform.h/.cpp                   (直搬 + RPC 分支)
qgsimagewarper.h/.cpp                       (直搬，GDAL warp 含进度回调)
qgsmapcoordsdialog.h/.cpp                   (直搬) + .ui
qgsrasterchangecoords.h/.cpp                (直搬)
qgsresidualplotitem.h/.cpp                  (直搬)
qgstransformsettingsdialog.h/.cpp           (拆为右 dock 面板)
qgsgeorefconfigdialog.h/.cpp                (直搬)
qgsgeorefvalidators.h/.cpp                  (直搬)
qgsvalidateddoublespinbox.h/.cpp            (直搬)

qgsrpcgcptransformer.h/.cpp                 (新写：RPC 物理模型 transformer)
rs_rms_scatter_widget.h/.cpp                (新写：RMS 散点图)
rs_twincanvas_sync_controller.h/.cpp        (新写：双画布同步)
rs_georef_mode_toggle.h/.cpp                (新写：三模式分段按钮)
```

不单独成库，目标文件编入主应用可执行体（与 `src/app/dialogs/` 同处理）。

### 3.3 已有资产复用

- `src/ui/georeferencer/qgsmapcoordsdialogbase.ui` — 保留
- 其余 4 个 `.ui`（主窗口/transform settings/transform type/config）不再使用，主窗口纯 C++ 拼装以精确匹配 design.html

### 3.4 搬运规则

- 保留 `Qgs*` 类名 + 命名空间，便于跟踪 upstream
- 仅修改：include 路径、删除 `SIP_*` 宏、将 `QgisApp::instance()` 替换为构造时注入的 `QgisInterface*`
- 项目自有新组件用 `Rs*` 前缀以与搬运代码区分

## 4. UI 布局

### 4.1 主窗口（1200×800 默认，独立 QMainWindow）

按 `UI/design.html` `ArtboardGeoref` 实现：

- **MenuBar**：复用主应用菜单栏样式，右侧显示 "⊕ 几何校正 · <文件名>" 提示
- **Mode Toolbar (36px)**：
  - 三模式分段按钮：`[Image → Map] | Image → Image | RPC 物理模型`
  - GCP 操作：⊕ 添加 GCP / ✕ 删除选中 / ⬆ 从文件加载 / ⬇ 导出 .gcp
  - 视图：⛓ 同步缩放 / ⛶ 全图 / ✦ 自动匹配 (SIFT)（v1 占位）
  - 右侧：▶ 预览 / [✓ 应用校正]（primary 绿色）
- **中央双画布并排**：左 SRC（蓝 badge，像素坐标），右 REF（绿 badge，投影坐标）
  - 同步十字光标 + zoom box，选中 GCP 黄框
  - 参考画布额外叠加道路/行政边界 SVG（沿用主应用图层渲染管线）
  - 左下角浮动坐标读出
- **底部 GCP 表（280px）**：
  - 表头：地面控制点 · N 个 · M 启用 / 总 RMS / 变换方法 / + 添加 / 仅显示残差>1px / 导出
  - 列：启用 ☐ / # / X源(px) / Y源(px) / X参(m) / Y参(m) / ΔX / ΔY / RMS(px) / 类型
  - 选中行黄色背景 + 蓝色左竖条；残差 ≥ 1px 黄色高亮 + ⚠ 标
  - 等宽数字字体 `IBM Plex Mono`
- **右 dock 参数面板（340px）**：
  - 坐标变换：类型下拉（含 RPC 物理模型）/ 最少点数 / 实际点数 / DOF
  - 重采样：方法下拉 / 输出像元 / 输出范围 / 背景值
  - RMS 误差分布：散点图 + X/Y/Total/最大残差
  - 坐标系：源 / 目标 / 投影
  - 输出：文件名 + GeoTIFF · LZW
- **StatusBar**：选中 GCP 的源→目标映射 / 比例尺 / CRS / 残差状态文字

### 4.2 与 QGIS 原版差异（实现策略）

| 设计稿 | QGIS 原版 | 实现策略 |
|---|---|---|
| 双画布同步 | 单画布 | 两个 `QgsMapCanvas` 实例 + `RsTwinCanvasSyncController` |
| 三模式（含 RPC） | 两模式无 RPC | 新增 `QgsRpcGcpTransformer` + mode toggle 控制 transformer 候选 |
| 右 340px dock 面板 | 独立对话框 | `QDockWidget`，移植 `QgsTransformSettingsDialog` 内容 |
| RMS 散点图 | 仅残差箭头 | 残差箭头复用 `QgsResidualPlotItem`；新增 `RsRmsScatterWidget` 嵌右 dock |
| GCP "类型" 列 | 无 | `QgsGcpPoint` 增加 `mPointType`；ComboBox delegate |
| SIFT 按钮 | 无 | v1 占位 "敬请期待"，Phase 11.5 实现 |

### 4.3 主应用入口

`src/app/main_window.cpp::setupMenu()` 在 Raster 菜单 `Mosaic...` 之后追加：

```cpp
rasterMenu->addSeparator();
rasterMenu->addAction(QIcon(":/icons/georef"), tr("Georeferencer..."),
                      this, &QgisDesktopWindow::openGeoreferencer);
```

`openGeoreferencer()` 懒构造单例 `m_georefWindow`，构造时注入 `QgisInterface*`。

## 5. 数据流与 Pipeline

### 5.1 GCP 采集流程

**Image → Map / Image → Image**：
1. 用户点击 ⊕ 添加 GCP → 源/参考画布 mapTool 设为 `QgsGeorefToolAddPoint`
2. 用户在源画布点击 → `QgsMapCoordsDialog` 弹出
3. 用户在参考画布点击对应点 → 坐标自动填入对话框
4. 用户确认 → `QgsGcpPoint` 加入 `QgsGCPList` → 触发拟合

**RPC 模式**：
- 参考画布关闭点选 → 用户在源画布点击地物后，弹"输入参考坐标"对话框（手动键入或从 DEM 反算）
- 右 dock 显示 DEM 字段（必填）+ Z offset + 单位
- 变换类型下拉锁定为 RPC，禁用其他选项

### 5.2 变换拟合（每次 GCP 变化）

```
QgsGCPList::changed
  → 构造 QgsGeorefTransform(method)
    → QgsGcpTransformerInterface::createFromParameters(method)
      Linear/Helmert/Polynomial/Projective: QgsLeastSquares 求最小二乘
      TPS: GDALCreateTPSTransformer
      RPC: GDALCreateRPCTransformer (新)
    → 拟合并存储 transformer
  → 计算残差 (forward(src) - actual_dst) 与 RMS
  → 级联更新:
      - GCP 表 ΔX/ΔY/RMS 列
      - 状态栏 RMS label（< 0.5 绿 / < 1.0 灰 / ≥ 1.0 橙）
      - RsRmsScatterWidget 重绘
      - 源画布 QgsResidualPlotItem 残差箭头
      - [✓ 应用校正] enabled 状态
```

点数不足判断：`QgsGcpTransformerInterface::minimumGcpCount(method)`（Linear:2, Helmert:2, Poly1:3, Poly2:6, Poly3:10, TPS:3, Projective:4, RPC:0）。

### 5.3 应用校正（GDAL warp）

```
[✓ 应用校正] 点击
  → 校验：启用 GCP 数 ≥ minimum / 输出路径可写 / 目标 CRS 已选
  → 在 QgsTask 线程池启动 QgsImageWarper::warpFile(...)
    - inputFile, outputFile, transform, resampling
    - destResolution, destCrs, creationOptions=["COMPRESS=LZW","TILED=YES"]
    - progressDialog
    - 内部：把 QgsGcpTransformerInterface 适配为 GDAL transformer，
      调用 GDALReprojectImage / GDALWarpRegionToBuffer 分块输出
  → 成功后：
    - 写 worldfile（若勾选）
    - 加入工程顶层组（若勾选）
    - 写 <source>.points
    - 状态栏绿条提示
    - 写一条结构化日志（详见 5.5）
```

**失败路径明确处理：**

| 失败模式 | 检测 | 用户看到 | 日志 |
|---|---|---|---|
| 输出磁盘满 | GDAL `CE_Failure` + errno `ENOSPC` | `QgsMessageBar` 红条："磁盘空间不足，warp 中止" | error level |
| 输入文件运行中消失 | `GDALOpen` 二次校验失败 | 红条："源文件不可读" | error level |
| 投影/CRS 不匹配警告 | GDAL `CE_Warning` | `QgsMessageBar` 黄条但允许继续 | warn level |
| GCP 拟合数值奇异 (Polynomial2 行列式 ≈ 0) | `QgsLeastSquares` 抛 `QgsLeastSquares::SingularException` | 红条："GCP 共线，无法拟合，请重新分布点位" | error level |
| 用户取消 | `QgsFeedback::canceled()` 轮询 | 状态栏灰条："已取消" | info level |
| RPC DEM CRS 不一致 | RPC 拟合前比对 DEM `spatialRef` vs `dstCrs` | 黄条警告："DEM CRS 与目标不一致，正射结果可能偏移" + 允许用户选择继续 | warn level |

所有路径必须经过 `QgsMessageBar`，不能用 `QMessageBox::warning` 阻塞流程。warp 运行期间 GCP 表设为只读（禁用 delegate 编辑、隐藏 ⊕/✕ 按钮），完成或取消后恢复。

### 5.4 持久化

- `.points` 文件：直搬 QGIS 读写实现，**首行加版本头** `# QGEOS .points v2`（v1 是 QGIS 原版不含 type 列；v2 含 type 列）。读取时无头当 v1，type 列默认空字符串；写出永远 v2。
- 重打开同栅格 → 自动检测 `<basename>.points` → 提示恢复
- `QgsSettings` 存：上次变换方法、上次重采样、上次输出目录、同步开关

### 5.5 结构化日志

每次 warp 在完成（或失败/取消）时，向 `QgsMessageLog` 写一条 JSON 单行，tag = `Georeferencer`：

```json
{"event":"warp_finished","method":"PolynomialOrder2","gcp_total":7,"gcp_enabled":6,"rms_px":0.847,"resampling":"Bilinear","output":"/path/out.tif","output_bytes":12480512,"duration_ms":3420,"status":"ok"}
```

`status` ∈ `ok` / `failed` / `cancelled`。失败时附 `error_code` 和 `error_msg`。该日志服务于：① 调试线索；② 未来 Phase 12 AI Agent 调用时返回机读结果。

## 6. 测试策略

| 用例 | 文件 | 关键断言 |
|---|---|---|
| 变换器 | `tests/test_gcp_transformer.cpp` | 7 种变换：合成 GCP fit 后 forward 误差 < 1e-6；`minimumGcpCount` 正确 |
| 最小二乘 | `tests/test_least_squares.cpp` | Helmert/Poly1-3/Projective 解与解析解 1e-9 一致；共线 GCP 抛 `SingularException` |
| RPC | `tests/test_rpc_transformer.cpp` | 带 RPC 元数据小栅格 + DEM fit 后 forward 与 gdalinfo 报告角点对齐；DEM CRS 不匹配时发 warning 信号 |
| GCP 列表 | `tests/test_gcp_list.cpp` | 增删/启用切换信号；`updateResiduals` 仅启用点；类型字段持久化 |
| 文件 I/O | `tests/test_gcp_points_file.cpp` | `.points` 写读 1:1 还原；v1 无头读入 type 字段为空；v2 头自动追加 |
| Warp 端到端 | `tests/test_image_warper.cpp` | 64×64 + 4 平移 GCP → 输出 GeoTransform 准确；像素 SHA256 与 golden 一致 |
| **Warp 取消** | `tests/test_image_warper_cancel.cpp` | 启动大栅格 warp → 200ms 后 `feedback.cancel()` → ≤ 500ms 退出，无残留输出，日志 `status=cancelled` |
| **Warp 失败路径** | `tests/test_image_warper_errors.cpp` | (a) 输出路径只读 → `CE_Failure` 红条；(b) 4 共线 GCP → `SingularException` 红条 |
| **CRS 透传** | `tests/test_warp_crs_passthrough.cpp` | 源/目标同 CRS → 不触发重投影路径，输出 GeoTransform 等于平移 |
| **UI 编辑锁** | `tests/test_georef_window_warp_lock.cpp` | warp 中 GCP 表 delegate 拒绝编辑、⊕/✕ 按钮 disabled |
| UI 烟雾 | `tests/test_georef_window.cpp` | 构造主窗口 + 注入 GCP；RMS label 非空；应用按钮 enabled；RPC 模式 DEM 字段可见；双画布同步 |
| 回归 Golden | `tests/data/georef_golden/` | GF-2 256×256 + 6 GCP + Polynomial2 → 与 golden.tif 逐像素一致（容差 1 DN） |

测试数据放 `tests/data/georef/`（GF-2 截图 + DEM + RPC tile，约 5MB，golden 用 git LFS）。**测试数据来源**：从已发布的 LC09 L1TP scene 裁剪 256×256 像素带 RPC 元数据的 GeoTIFF + 16×16 SRTM 30m DEM tile；如不可得，用合成数据（已知 RPC 系数 + 高斯地形）。在 11.4.8 开始前必须落地，否则该子任务阻塞。

按项目 TDD 节奏（[[feedback_tdd_workflow]]）：每子任务 Red-Green-Refactor，同步更新 `findings.md`/`progress.md`/`task_plan.md`。

## 7. 子任务拆分（Phase 11.4.1 – 11.4.8）

1. **11.4.1** 搬运 analysis 算法层 + 新建 `qgis_analysis` 静态库
2. **11.4.2** 搬运 `QgsImageWarper` + GDAL warp 端到端
3. **11.4.3** GCP 列表 + `.points` 持久化
4. **11.4.4** 主窗口骨架（菜单/Mode toggle/StatusBar/Raster 菜单接入）
5. **11.4.5** 双画布并排 + 同步控制器
6. **11.4.6** GCP 表格重写 + 残差列 + 类型 delegate
7. **11.4.7** 右 dock 参数面板 + 残差散点 + 应用校正
8. **11.4.8** RPC 物理模型 + DEM 字段 + 模式切换

### Stretch（移到 Phase 11.5）

- 自动匹配 SIFT（顶栏按钮 v1 占位）
- 从主地图选点辅助（image-to-map 便捷功能）

### 完工标准

- 12 个 Catch2 测试文件全绿（含取消/失败路径/CRS 透传/UI 编辑锁四个新增）
- 手工烟雾：加载 GF-2 截图 → 6 GCP → Polynomial2 → 输出 GeoTIFF → `gdalinfo` 验证 GeoTransform
- 手工 warp 失败烟雾：故意填只读输出路径 → 红条提示，无崩溃
- 手工取消烟雾：大栅格 warp 中按取消 → ≤ 1s 退出，状态栏灰条
- 一次成功 warp 在 `QgsMessageLog` 写出符合 §5.5 schema 的 JSON 日志
- design.html 视觉 review（mimo-v2.5 `ui_diff_check` 对比设计稿）

## 8. 风险与未决

- **RPC 实测数据**：见 §6 末尾"测试数据来源"段。在 11.4.8 开始前确认数据可用。
- **GDAL 版本**：`GDALCreateRPCTransformer` 在 GDAL 3.4+ 稳定 → §3.1 已加 `find_package(GDAL 3.4 REQUIRED)`。
- **双画布同步性能**：大栅格时 extentsChanged 信号风暴；同步控制器需用 `blockSignals` + `QTimer::singleShot(0, ...)` 节流；性能基线 GF-2 (160 MB) 在 11.4.5 子任务中验证 30 FPS 拖拽不掉帧。
- **GCP 类型字段 / .points 版本**：见 §5.4 v2 头方案。
- **Windows MSVC 构建**：新增 `qgis_analysis` 静态库需在 CMake 显式 `set(BUILD_SHARED_LIBS OFF)` 或检查工程其它子库的导出宏一致性；11.4.1 子任务中验证 MSVC + Linux GCC 同时通过。
- **CEO/Eng review 2026-06-02 完成**，6 处补丁已合入此文档。
