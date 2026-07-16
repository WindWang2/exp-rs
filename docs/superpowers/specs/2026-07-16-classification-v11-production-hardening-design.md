# Classification v1.1 — Production Hardening 设计

**日期:** 2026-07-16  
**Phase:** 10A.2（像素分类生产补强）  
**状态:** 设计已确认，待写实现计划  
**前置:**
- Phase 10A 像素分类（`docs/superpowers/specs/2026-06-04-classification-pixel-design.md`）
- Phase 10A.1 polish（`docs/superpowers/specs/2026-06-04-classification-10a1-polish-design.md`）
- 2026-07-16 分类模块 Critical 修复（魔棒精确像素、bbox 栅格化、Producer/User 纠正、NoData、KMeans k、ROI load clear、canvas CRS）

---

## 1. 目标与范围

### 1.1 背景

像素监督/非监督分类主路径已可用：ROI 采集 → 分层划分 → NormalBayes / SVM / KMeans → 分块预测 → 精度对话框 → GeoTIFF。  
生产与 Lab3 仍有明确债务：

| 缺口 | 影响 |
|------|------|
| SVM 无特征标准化 | 多波段 DN 量级差时 RBF 不稳定 |
| ROI 保存强制 EPSG:4326 | 往返 CRS 漂、依赖 QgsProject 变换 |
| Hungarian 非方阵零 pad | k≠n_classes 时标签错配 |
| 输出 GeoTIFF 无 tiled/compress | 大图难用 |
| 预览跑全图 | 交互慢 |
| 关闭无脏提示 | 丢 ROI |

### 1.2 范围内（必须交付）

| ID | 项 | 说明 |
|----|-----|------|
| P1 | SVM / 通用特征标准化 | 列 z-score：train fit → train/test/tile transform；model 旁 `.scale.json` |
| P2 | ROI 默认源影像 CRS | save/load 不强制 4326；可选导出 4326 |
| P3 | Hungarian 矩形安全 pad | pad 代价大正数；禁止假匹配 |
| P4 | GTiff 创建选项 | 默认 TILED+DEFLATE；失败回退 |
| P5 | 视口裁剪预览 | Preview 写裁剪小图 + 更新 GT；Apply 仍全图 |
| P6 | 脏关闭 + 轻量 settings | 仿 georef `RsGeorefSessionState` |

### 1.3 明确不在范围内

- Random Forest / Mahalanobis / UNet 后端（继续灰显）
- OBIA / SLIC / SAM（Phase 10B）
- 拆分 `qgsclassificationmainwindow.cpp` 大文件
- 改变换数学以外的新分类算法、网格搜参 UI
- 主应用图层树深度集成重构

### 1.4 方案选择

采用 **外科补丁 + 薄 helper（方案 A）**：

- 新增 `RsFeatureScaler`、`RsClassifySessionState`
- 扩展既有 `RsClassificationTask::Config`、`RsRoiIO`、`RsHungarianAssignment`
- 主窗口只接线，不拆文件

### 1.5 完成标准

1. 相关 ctest 全绿（含新增用例）  
2. 人工 Lab3 主路径可跑通：开图 → ROI → SVM/NB 训练 → 视口预览 → Apply 全图 → 精度对话框 → 导出/重载 ROI → 脏关闭

---

## 2. 架构

```
┌─────────────────────────────────────────────────────────────┐
│ QgsClassificationMainWindow                                 │
│  dirty/close ← RsClassifySessionState                       │
│  Preview → viewport pixel window → Task(cropped)            │
│  Apply   → full raster → Task(full)                         │
└────────────┬──────────────────────────┬─────────────────────┘
             │                          │
             ▼                          ▼
┌────────────────────────┐   ┌────────────────────────────────┐
│ RsFeatureScaler        │   │ RsClassificationTask           │
│ fit/transform/JSON     │   │ scale → fit → accuracy → tiles │
└────────────────────────┘   │ GTiff options / NoData / remap │
                             └────────────────────────────────┘
RsRoiIO (source CRS)     RsHungarianAssignment (rect-safe)
```

### 2.1 新文件

| 路径 | 职责 |
|------|------|
| `src/analysis/classification/rs_feature_scaler.h/.cpp` | 列 z-score + JSON I/O |
| `src/app/classification/rs_classify_session_state.h/.cpp` | dirty + QSettings |
| `tests/test_feature_scaler.cpp` | scaler 单测 |
| `tests/test_classify_session_state.cpp` | session 单测 |
| `tests/test_classification_preview_window.cpp` | 视口→像素窗纯函数 |

### 2.2 修改的现有文件

| 路径 | 变更 |
|------|------|
| `rs_classification_task.{h,cpp}` | Config: scaler 参数、creationOptions、optional pixel window；predict 前 transform |
| `qgsclassificationmainwindow.{h,cpp}` | session、closeEvent、preview 窗、scaler 接线、ROI CRS 传入 |
| `rs_roi_io.{h,cpp}` | 默认源 CRS；API 明确 destCrs |
| `rs_hungarian_assignment.{h,cpp}` | 矩形代价 + 安全 pad |
| `rs_classifier_cv_backend` / load dialog | load model 时可选读 `.scale.json` |
| `tests/test_roi_io.cpp`、`test_hungarian_assignment.cpp`、`test_classification_e2e.cpp`、`test_classifier_svm.cpp` | 覆盖新行为 |
| CMakeLists（analysis / app_classify / tests） | 注册源与测试 |

### 2.3 依赖方向

- `RsFeatureScaler` → OpenCV Mat + Qt JSON only  
- `RsClassifySessionState` → Qt Core/Widgets only（对齐 georef session）  
- Task 依赖 scaler 值类型或可选 `std::optional` 参数，不拥有 UI  

---

## 3. 组件设计

### 3.1 RsFeatureScaler（P1）

```cpp
class RsFeatureScaler {
public:
  bool fit(const cv::Mat &trainX);          // CV_32F NxB
  cv::Mat transform(const cv::Mat &X) const;
  bool isFitted() const;
  bool saveJson(const QString &path) const; // mean[], std[]
  bool loadJson(const QString &path);

  // std_j < 1e-6 → 1.0（常数波段不炸）
};
```

**接线：**

1. `buildTrainingData` 后、`stratifiedSplit` 前或后均可；**约定：split 后对 trainX fit，再 transform trainX 与 testX**。  
2. `RsClassificationTask::run`：若 `Config.scalerFitted`，tile 拼出的 X 先 `transform` 再 `predict`。  
3. `save(model.yml)` 成功时旁路写 `model.scale.json`（同 stem）。  
4. `load` 模型时若存在 sidecar 则 load；缺失则 **不缩放**（兼容旧模型），statusBar 提示一次。

**SVM 与 NormalBayes 均走同一 scaler**（一致性 > 仅 SVM）。KMeans 同样缩放，避免量级主导距离。

### 3.2 ROI CRS（P2）

```cpp
// RsRoiIO::save(path, collection, crs)
// crs 有效 → 写出该 CRS；无效 → 回退 EPSG:4326 并 log
// RsRoiIO::load(path, collection, targetCrs)
// 读图层原生 CRS，features transform → targetCrs 后 append
```

主窗口：

- `exportRois`：`crs = m_sourceLayer ? m_sourceLayer->crs() : QgsCoordinateReferenceSystem()`  
- UI 不强制加「导出为 4326」控件于 v1.1；需要时后续加 checkbox（YAGNI）

### 3.3 Hungarian 矩形安全（P3）

输入 `cost` 可为 `n×m`（n=true classes, m=clusters）。

1. `sz = max(n,m)`  
2. pad 区填 `kPadCost = 1e9`（最小化问题下不会被选，除非别无选择）  
3. 真实子块填原代价（仍为「负共现」约定）  
4. `assign[i]` 仅当 `i < n && assign[i] < m` 写入 `remap[cluster]=class`  

单测：3×2、2×3、空矩阵。

### 3.4 GTiff 创建选项（P4）

```cpp
// Config
QStringList creationOptions = {
  "TILED=YES", "COMPRESS=DEFLATE", "PREDICTOR=2"
};
```

`GDALDriver::Create(..., papszOptions)`。若 `Create` 失败且 options 非空 → 清空 options 重试一次并 `qWarning`。  
不引入 UI 勾选于 v1.1（默认足够）。

### 3.5 视口裁剪预览（P5）

**纯函数（可单测）：**

```cpp
// map extent + gt + W/H → pixel rect clamped
struct PixelWindow { int x0,y0,x1,y1; bool valid; };
PixelWindow mapExtentToPixelWindow(const QgsRectangle &extent,
                                   const double gt[6], int W, int H);
```

- Preview：`PixelWindow` 无效 → 拒绝并提示「视口不在影像范围内」  
- Task Config：`bool cropToWindow = false; PixelWindow window{}`  
- `cropToWindow==true`：输出尺寸 = 窗宽高；`SetGeoTransform` 为窗原点；仅循环窗内 tiles  
- Apply：`cropToWindow=false` 全图  

预览层：继续 `m_layerStore` 临时图层；文件名 `classify_preview.tif`（覆盖写）。

### 3.6 Session / 脏关闭（P6）

对齐 georef：

```cpp
class RsClassifySessionState {
  bool isDirty() const;
  void markDirty();
  void clearDirty();
  void saveWindow(QWidget*);
  void restoreWindow(QWidget*);
  struct Snapshot {
    QString lastSourcePath, lastOutputPath, lastRoisPath, lastModelPath;
    int classifierKind = 0;
    double trainRatio = 0.7;
    double wandTolerance = 20.0;
  };
  void saveSnapshot(const Snapshot&);
  Snapshot restoreSnapshot();
};
```

- `m_rois::changed` / classDef 变更 → `markDirty`（load/save 时 suppress）  
- `closeEvent`：dirty → Save / Discard / Cancel；Save 走 `exportRois` 路径逻辑  
- task 运行中关闭：确认后 `task->cancel()` 再关（若 task 指针可追踪；否则仅提示）

Settings 键前缀：`Classification/`。

---

## 4. 数据流（训练 / 预览 / 应用）

### 4.1 Apply（全图）

```
buildTrainingData → stratifiedSplit
  → scaler.fit(trainX); trainX/testX = transform(...)
  → backend.fit(trainX, trainY)
  → Task: accuracy(test) → full tiles predict(transform each tile) → GTiff+options
  → save model? optional existing path + scale.json
```

### 4.2 Preview（视口）

```
同上训练，但 Task.cropToWindow=true, window=mapExtentToPixelWindow(canvas.extent())
输出小图 + GT → 临时图层
不弹精度对话框（与现网一致：预览可不强调 accuracy；若 test 非空仍可算但不强制 UI）
```

**约定：** Preview **仍计算 accuracy 若 test 非空**，但不自动弹对话框（避免打断预览）；Apply 成功且有 metrics 才弹 `RsAccuracyDialog`。

### 4.3 Load model

```
load YAML → backend; if scale.json exists → scaler.loadJson
Task skips fit; tiles transform with loaded scaler
```

---

## 5. 错误处理

| 场景 | 行为 |
|------|------|
| train 样本 &lt; 10 | 现状：拒绝训练 |
| 全常数波段 | std→1；可训练但 JM/可分性差 |
| ROI 变换失败 | save/load 失败弹窗 + MessageLog |
| Hungarian n=0 或 m=0 | remap 空；预测用原始 cluster id |
| Create+options 失败 | 回退无选项 Create |
| 视口与影像不相交 | Preview 拒绝，statusBar 文案 |
| dirty 关闭 Save 失败 | ignore close |
| scale.json 损坏 | 忽略缩放，警告，继续预测 |

---

## 6. 测试计划

| 测试 | 断言 |
|------|------|
| `test_feature_scaler` | fit 后列 mean≈0；常数列 std 处理；transform 幂等参数 |
| `test_roi_io` | 合成 UTM 几何 save/load 坐标误差 &lt; 1e-6（同 CRS） |
| `test_hungarian_assignment` | 3×2、2×3 映射不落到 pad 假类 |
| `test_classification_preview_window` | extent→pixel clamp 边界 |
| `test_classify_session_state` | dirty + snapshot round-trip |
| `test_classifier_svm` | 显式不同量级波段：scaled fit 可达到 ≥0.9（合成） |
| `test_classification_e2e` | 输出文件存在；可选检查压缩元数据（若 GDAL 可查） |

手工 Lab3：见 §1.5。

---

## 7. 实现顺序（供 writing-plans）

1. `RsFeatureScaler` + 单测 → 接入 Task + mainwindow train 路径  
2. Hungarian 矩形安全 + 单测  
3. ROI CRS save/load + 单测  
4. GTiff creation options + e2e 冒烟  
5. `mapExtentToPixelWindow` + Preview 裁剪  
6. `RsClassifySessionState` + closeEvent  
7. Lab3 手工 + 全量 classify ctest  

---

## 8. 决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| 实现策略 | 外科补丁 + 薄 helper | 不拆主窗、对齐 georef session 模式 |
| 缩放范围 | 所有后端统一 | 避免 NB/SVM/KMeans 路径分裂 |
| 预览输出 | 裁剪小图 + GT | 快；叠图依赖用户 pan 到视口 |
| ROI CRS | 默认源 CRS | 修 I9 根因 |
| 新算法 | 不做 | YAGNI / 范围门 |

---

## 9. 风险

| 风险 | 缓解 |
|------|------|
| 旧模型无 scale.json | 明确不缩放 + 提示 |
| pad 代价仍被选中（极端） | 单测 + pad=1e9 |
| 视口 CRS 与源不一致 | 已设 canvas CRS=layer；window 计算用同一 gt |
| mainwindow 继续膨胀 | v1.1 接受；后续阶段再拆 |

---

## 10. 开放问题（实现期可定默认）

无阻塞项。下列默认已定：

- Preview 不强制弹 accuracy 对话框  
- 无 UI 切换「导出 4326」  
- 无 UI 切换压缩算法  
