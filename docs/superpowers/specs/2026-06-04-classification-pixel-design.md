# Phase 10A: Pixel-Based Classification 设计

**日期:** 2026-06-04
**Phase:** 10A
**状态:** 设计完成，待写实现计划
**后续:** Phase 10B (OBIA — 面向对象分类) 留作独立 phase

## 1. 目标

为 SICNU GEO RS 添加像元级监督/非监督分类模块，对齐 RS 实验课 Lab #4。具体闭环：

- 在源栅格上交互式采集训练样本 ROI（多类别多 ROI）
- 实时查看类别光谱曲线 + 两两 JM 分离度评估样本质量
- 用 OpenCV ML 训练 NormalBayes (最大似然) / SVM (RBF) / K-Means
- 全图扫描预测并输出带 ColorTable 的分类栅格
- 混淆矩阵 + Kappa + per-class P/R/F1 精度评价

UI 严格按 `UI/design.html` `ArtboardClassify` 布局。OBIA 与 SLIC/SAM/Random Forest/UNet 顶栏占位灰显，留 Phase 10B。

## 2. 架构

### 2.1 依赖

- 复用 Phase 11.5 引入的 OpenCV 4.5+（已链 `core/features2d/imgproc/calib3d`），新增 `ml` 模块组件
- 复用 Phase 11.4 引入的 `qgis_analysis` 静态库 + `QgsTask` 模式
- 无新外部依赖

**OpenCV 强依赖（不 OPTIONAL）：** 无 OpenCV 时整个分类窗口禁用入口（菜单灰），不进窗口。这是有意取舍 —— 教学场景下 OpenCV 必装；OPTIONAL 会让灰显 UI 误导学生。

### 2.2 库分层

```
src/analysis/classification/                  ← 编进 qgis_analysis
├── rs_roi.h/.cpp                             · 单个 ROI（几何 + 类别 ID + 像素索引集）
├── rs_roi_collection.h/.cpp                  · QObject + QVector<RsRoi*>
├── rs_roi_io.h/.cpp                          · OGR 读写 shapefile + sidecar JSON
├── rs_class_def.h/.cpp                       · 类别（id/name/color）
├── rs_classifier_backend.h/.cpp              · 抽象基类（fit/predict）
├── rs_classifier_normalbayes.h/.cpp          · cv::ml::NormalBayesClassifier 包装
├── rs_classifier_svm.h/.cpp                  · cv::ml::SVM(RBF) 包装
├── rs_classifier_kmeans.h/.cpp               · cv::kmeans 包装
├── rs_jm_separability.h/.cpp                 · JM (Jeffries-Matusita) 距离矩阵
└── rs_accuracy_assessment.h/.cpp             · 混淆矩阵 + Kappa + P/R/F1

src/app/classification/                       ← qgis_app_classify 静态库（新）
├── qgsclassificationmainwindow.h/.cpp        · QMainWindow 主窗口
├── rs_class_table_widget.h/.cpp              · 右 dock 类别管理表
├── rs_class_quick_list.h/.cpp                · 左下 dock 类别快览
├── rs_jm_matrix_widget.h/.cpp                · JM 6×6 热图
├── rs_spectral_curve_widget.h/.cpp           · 光谱均值±σ 折线
├── rs_classifier_setup_bar.h/.cpp            · 底部 ClassifierBar
├── rs_roi_tool_point.h/.cpp
├── rs_roi_tool_rectangle.h/.cpp
├── rs_roi_tool_polygon.h/.cpp
├── rs_roi_tool_freehand.h/.cpp
├── rs_roi_tool_magicwand.h/.cpp              · 容差生长 + flood fill
├── rs_classification_task.h/.cpp             · QgsTask 包装
└── rs_accuracy_dialog.h/.cpp                 · 精度矩阵对话框
```

`qgis_app_classify` STATIC 库链 `qgis_analysis` + `qgis_core` + `Qt6::Core/Gui/Widgets` + `${OpenCV_LIBS}` (含 ml 模块)。

### 2.3 主应用入口

`src/app/main_window.cpp::setupMenu()` 在 Raster 菜单加子菜单：

```cpp
auto *classifyMenu = rasterMenu->addMenu(tr("Classification"));
classifyMenu->addAction(QIcon(":/icons/classify_pixel"),
    tr("Supervised Classification (Pixel-based)..."),
    this, &QgisDesktopWindow::openClassificationWindow);
auto *obia = classifyMenu->addAction(tr("Object-based Classification (OBIA) — Phase 10B"));
obia->setEnabled(false);
```

懒构造 `QgsClassificationMainWindow*`，传 `QgisInterface*`，多次打开复用同一实例。

## 3. UI 布局

按 `UI/design.html` `ArtboardClassify` 1600×1000（默认运行 1280×800，可缩放）。

```
MenuBar  文件 编辑 视图 处理[active]            ⊕ 监督分类·训练样本采集
─────────────────────────────────────────────────────────────────────────
ROI 工具栏 (36px):
 [选择] │ ROI: ⊙点 ⌒折线[灰] ▣矩形 ⬡多边形 ✎自由 │ 自动: ✦魔棒 ▦SLIC[灰] 🧠SAM[灰] │
        │ 📊查看光谱 ⚖分离度 ⬇导出ROI │       ▶快速分类预览 [✦ 应用分类...]
─────────────────────────────────────────────────────────────────────────
左 dock 240         │ 中央 (QgsMapCanvas)        │ 右 dock 380
┌图层───┐           │ ─ SRC 栅格 + RGB 波段下拉   │ ┌类别管理 (ClassTable)┐
│Sentinel│          │ ─ ROI 多边形叠加 (35% 填充) │ │ #1 林地 ROI14 8421 │
│B04/03/02│          │ ─ 顶点 handle (选中态白)   │ │ #2 草地 ROI8  ...   │
│..      │          │ ─ 编号气泡                  │ │ #3 水体 [选中]      │
└────────┘           │ ─ 浮动 mini-toolbar "当前  │ │   +ROI/光谱/改色    │
┌类别快览┐           │   类别"                    │ │ ...                  │
│🟢林地14│           │                             │ │ ➕添加类别          │
│🔵水体6 │           ├──ClassifierBar (72px)──────┤ ├JM 6×6 热图──────────┤
│..      │          │ 分类器: [MaxLik][SVM RBF]  │ │  1.0–1.9 黄绿       │
│➕添加类│          │ [RF灰][Maha灰][UNet灰AI]   │ │  < 1.0 红           │
└────────┘           │ 波段: B2·B3·B4·B8 (4)      │ │  ≥ 1.9 绿            │
                    │ 训练/验证: 70/30 · 分层    │ └─────────────────────┘
                    │      [⚖CV] [▶训练并分类] │
─────────────────────────────────────────────────┤  (底 dock 可隐藏)
底部 dock 180 (光谱曲线): 每类一条均值折线 + ±σ 半透明带
─────────────────────────────────────────────────────────────────────────
StatusBar: EPSG | 鼠标 X/Y | 总 ROI 62, 像元 32915 | 当前类: 水体
```

### 3.1 ROI 颜色（design.html 默认 6 类）

| ID | 名称 | 颜色 |
|---|---|---|
| 1 | 林地 | `#2da44e` |
| 2 | 草地 | `#a3e635` |
| 3 | 水体 | `#0969da` |
| 4 | 建成区 | `#cf222e` |
| 5 | 耕地 | `#d29922` |
| 6 | 裸地 | `#8a92a0` |

### 3.2 v1 vs Stretch

| 设计稿元素 | v1 状态 |
|---|---|
| ⊙点 / ▣矩形 / ⬡多边形 / ✎自由 ROI | 实现 |
| ⌒折线 ROI | 灰显（折线不闭合，对 ROI 无意义） |
| ✦魔棒 | 实现（4 连通 flood，光谱 L2 距离阈值） |
| ▦SLIC / 🧠SAM | 灰显 → Phase 10B |
| 📊光谱查看器 | 实现 |
| ⚖JM 分离度 | 实现 |
| ⬇导出 ROI | 实现（shapefile） |
| 分类器 MaxLik / SVM RBF | 实现 |
| 分类器 RF / Mahalanobis / UNet | 灰显占位 |
| K-Means（无监督） | 实现（不在 ClassifierBar 上，从工具菜单/对话框入口） |
| 训练/验证 70/30 + 分层抽样 | 实现 |
| 交叉验证（k=5） | 实现 |
| 快速分类预览 | 实现（viewport 内） |
| 应用分类 + GeoTIFF + ColorTable | 实现 |

## 4. 数据流

### 4.1 ROI 生命周期

```
用户选 ROI 工具 [▣ 矩形]  →  mCurrentClassId = mClassTable->currentId()
   ↓ (未选类: 状态栏提示 "请先选类别")
SRC 画布 mapTool = mRoiToolRectangle(currentClassId)
   ↓
用户拖出矩形 → release
   ↓
RsRoiToolRectangle::canvasReleaseEvent → emit roiDrawn(geometry, classId)
   ↓
QgsClassificationMainWindow::onRoiDrawn:
  - GDALRasterizeGeometries → 像素索引集 (uint64 row*W+col)
  - new RsRoi(classId, geometry, pixelIndices)
  - mRoiCollection->appendRoi(roi)  → emit roiAdded → emit changed
   ↓
级联更新:
  - 类别表 ROI 计数 + 像元数 + 进度条
  - 类别快览 ROI 数
  - 光谱 dock (若开): setSpectralForClass(classId) 重绘
  - JM 矩阵 (若开): schedule recompute (500ms throttle)
```

像素索引一次性算入 `RsRoi::mPixelIndices`（uint64 vector）。占用：1 万像元 = 80KB / ROI，可接受。编辑 ROI 几何时重算。

### 4.2 光谱采样

```cpp
RsRoi::sampleSpectra(GDALDataset *src, const QVector<int> &bandIndices) const
  → cv::Mat (rows=pixels, cols=bands, type=CV_32F)
```

类级光谱：`RsRoiCollection::classSpectra(classId, src, bands)` 拼接该类所有 ROI。

### 4.3 JM 分离度

```
RsJmSeparability::compute(collection, src, bands, feedback)
  → QMap<QPair<int,int>, double>

  for each (c1, c2) with c1 < c2:
    X1, X2 = classSpectra
    μ1, μ2, Σ1, Σ2 = 均值 + 协方差
    Σ̄ = (Σ1 + Σ2) / 2
    B = 1/8 (μ1-μ2)ᵀ (Σ̄ + εI)⁻¹ (μ1-μ2)
      + 1/2 ln( det(Σ̄ + εI) / sqrt(det(Σ1 + εI) det(Σ2 + εI)) )
    JM = 2 (1 - exp(-B))     ∈ [0, 2]
    result[(c1,c2)] = JM
    if feedback.isCanceled: return {}
```

ε = 1e-6 ridge 防止奇异协方差（小样本类别）。

**热图渲染：** 6×6 单元格背景按 JM:
- ≥ 1.9 → `#2da44e` 绿（高可分）
- 1.5–1.9 → `#a3e635` 黄绿
- 1.0–1.5 → `#d29922` 黄（模糊）
- < 1.0 → `#cf222e` 红（难分）

单元格写 JM 值（%.2f 等宽 Mono）。

### 4.4 训练 + 应用分类

```
[✦ 应用分类…] 点击
   ↓
校验:
  - 至少 2 类
  - 每类 ≥ 10 像元（硬阻塞）, ≥ 50 (软警告)
  - 至少 1 波段
  - 输出路径非空可写
   ↓
弹输出对话框: 路径 + 加入工程 + 保存模型 (.yml)
   ↓
启动 RsClassificationTask (QgsTask):
  run() (worker thread):
    1. 收集训练集 X (Σpx × bands), y (class labels)
    2. 70/30 分层抽样 → X_train/y_train, X_test/y_test
       (类样本 < 7 → 全做 train, 弹消息)
    3. backend = newClassifier(algorithm)
    4. backend->fit(X_train, y_train)              [progress 30%]
    5. 分块扫栅格 (256×256 tile), predict 写出 GeoTIFF
                                                    [progress 30 → 90%]
    6. y_pred_test = backend->predict(X_test)
       计算混淆矩阵 + Kappa + P/R/F1               [progress 95%]
    7. 写 ColorTable + 元数据到输出 GeoTIFF
    8. 可选 backend->save("<output>.yml")
   ↓
taskCompleted (GUI 线程):
  - 加入主应用项目 (若勾) → 主画布显示
  - 弹 RsAccuracyDialog
  - QgsMessageLog 结构化日志:
    {"event":"classify_finished","algo":"SVM","classes":6,
     "train_px":18000,"test_px":7714,"kappa":0.892,
     "overall_accuracy":0.913,"duration_ms":4280}
```

**K-Means 路径：** 无 ROI 也能跑（输入 K + 选波段）；输出 0..K-1 类编号；不算精度。v1 走最简流程。

### 4.5 快速预览

[▶ 快速分类预览]：仅在当前 SRC 画布 viewport 内采样 + 训练 + predict + 临时图层覆盖。跳过精度评价。目标 < 2s 返回。

### 4.6 精度评价

```
struct Result {
    cv::Mat confusion;          // numClasses × numClasses CV_32S
    double  overallAccuracy;    // sum(diag) / sum(all)
    double  kappa;              // (po - pe) / (1 - pe)
    QVector<double> producerAcc; // diag[i] / col_sum[i]
    QVector<double> userAcc;     // diag[i] / row_sum[i]
    QVector<double> f1;
};
```

边界：`kappa = (po == 1.0) ? 1.0 : (po - pe) / (1 - pe)`。

`RsAccuracyDialog`：
- 顶部大字号显示 Overall + Kappa
- 中央混淆矩阵：对角线 `#208830`，非对角 ≥ 10 像元红字
- 底部 per-class P/R/F1 表
- [⬇导出 CSV/PDF] 按钮

### 4.7 持久化

| 内容 | 格式 | 位置 |
|---|---|---|
| ROI 几何 + classId | ESRI Shapefile (`.shp/.dbf/.shx`) | 用户选 |
| 类别定义 sidecar | GeoJSON `<rois>.classes.json` | ROI 旁 |
| 训练模型 | OpenCV YAML | `<output>.yml`（可选） |
| 分类栅格 | GeoTIFF + ColorTable | 用户选 |
| 精度报告 | CSV / PDF | 用户选 |
| 上次会话 | `QgsSettings Classification/lastWorkspace` | QSettings |

字段名约定：`cls_id`（不用 `classId`，避开 OGR 关键字风险）。

## 5. 子任务（9 个）

| # | 子任务 | 依赖 |
|---|---|---|
| **10.1** | ROI 数据模型 + shapefile/JSON I/O | 无 |
| **10.2** | 主窗口骨架 + Raster 菜单接入 + 4 个 dock 占位 | 10.1 |
| **10.3** | 类别管理 dock + 类别快览 dock | 10.2 |
| **10.4** | 4 个手动 ROI map tool + 浮动 mini-toolbar | 10.2 |
| **10.5** | 光谱曲线 widget + 底 dock | 10.3 + 10.4 |
| **10.6** | JM 分离度计算 + 6×6 热图 + 节流重算 | 10.3 |
| **10.7** | 魔棒工具（容差生长 flood fill） | 10.4 |
| **10.8** | 3 分类器 + ClassifierBar + RsClassificationTask + 输出 GeoTIFF + 快速预览 | 10.6 |
| **10.9** | 混淆矩阵 + Kappa + 对话框 + CSV/PDF 导出 | 10.8 |

## 6. 测试矩阵

| 子任务 | 测试 | 关键断言 |
|---|---|---|
| 10.1 | test_roi_collection | appendRoi 信号；按 classId 过滤；像素索引集 size 与栅格化一致 |
| 10.1 | test_roi_io | shapefile round-trip：classId/颜色/几何还原；JSON sidecar v1 兼容 |
| 10.2 | test_classification_window | 构造无崩溃；菜单/工具栏/四个 dock 都 findChild 成功 |
| 10.3 | test_class_table_widget | 6 类显示；ROI 数/像元数/进度条；选中类发 currentClassChanged |
| 10.4 | test_roi_tool_polygon | 模拟点击序列 → 闭合多边形 → emit roiDrawn 携带正确几何 |
| 10.5 | test_spectral_curve | 合成 3 类 × 3 ROI × 10 像元 → 渲染 QImage 非空，类切换重绘 |
| 10.6 | test_jm_separability | 完全相同 JM≈0；完全分离 JM≈2；中度重叠 JM≈1.4±0.2 |
| 10.7 | test_roi_tool_magicwand | 合成栅格中心均匀块 → 点击中心 flood 全块，不溢出 |
| 10.8 | test_classifier_normalbayes | 三高斯 2D → 训练 → 已知样本 ≥ 0.95 |
| 10.8 | test_classifier_svm | 同上，RBF C=10 gamma=0.5 ≥ 0.95 |
| 10.8 | test_classifier_kmeans | 三高斯 K=3 → cluster center 与真值 ≤ 0.5σ |
| 10.8 | test_classification_e2e | 64×64 三类 + 9 ROI → 全图 → GeoTIFF 正确 + ≥ 90% 像素正确 |
| 10.8 | test_classification_cancel | 2048² → 200ms cancel → ≤ 500ms 退出无残留 |
| 10.9 | test_accuracy_assessment | Kappa/Overall/P/R/F1 与 sklearn 参考 ≤ 1e-6 |
| 10.9 | test_accuracy_dialog | 注入混淆矩阵 → 对角加粗 + ≥10 非对角红字 + CSV 导出存在 |

总新增 15 文件 / ~25 TEST_CASE。预计总测试 251 → 276。

## 7. 风险

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| 1 | OpenCV SVM 大样本训练慢（10万像元 > 10s）| 中 | 中 | 快速预览只采 viewport |
| 2 | 多边形栅格化半像素归属歧义 | 中 | 低 | GDAL 中心点规则；测试断言固定 |
| 3 | shapefile `classId` 字段名冲突 | 低 | 低 | 用 `cls_id` |
| 4 | JM 协方差奇异（极小样本类） | 中 | 中 | ε=1e-6 ridge；测试加极小样本 case |
| 5 | 大栅格 predict 内存峰值 | 中 | 中 | 强制 256×256 tile + 流式写出 |
| 6 | 波段下拉 vs X 矩阵列序错位 | 中 | 高 | 入口存 mBandIndices；统一 pixel × band；E2E 测试覆盖 |
| 7 | 分层抽样在小类（< 7 像元）失败 | 低 | 低 | < 7 全做 train + 弹消息 |
| 8 | Kappa 单类极端分母为 0 | 低 | 低 | `kappa = (po==1.0) ? 1.0 : (po-pe)/(1-pe)` |

## 8. Done When

- 276+ Catch2 测试全绿（251 + 25+）
- 手工烟雾：Sentinel-2 加载 → 6 类 30+ ROI → JM 全 ≥ 1.5 → SVM 训练 → 应用 → 混淆矩阵总精 ≥ 0.85
- 快速预览 < 2s
- 输出 GeoTIFF 在主应用打开正确显示分类色
- 结构化日志 `event=classify_finished` 落到 `QgsMessageLog` tag `Classification`

## 9. 已知未决

- **Random Forest 占位策略**：v1 灰显，v2 (Phase 10B) 一起做？还是 v1 立马 stretch 补一个？— v1 灰显
- **训练模型 .yml 加载入口**：v1 只写不读；v2 加"加载已训模型 → 仅 predict"路径
- **混淆矩阵 PDF 导出**：MVP 用 CSV 即可；PDF 等 Phase 12 文档导出统一处理
- **K-Means 类编号 → 语义类映射**：v1 输出无名类，用户手工改色/改名；v1.5 加"K-Means 自动建类"按钮
- **ROI 编辑（增删顶点 / 拖拽）**：v1 只能整 ROI 删除重画；v1.5 加顶点编辑
