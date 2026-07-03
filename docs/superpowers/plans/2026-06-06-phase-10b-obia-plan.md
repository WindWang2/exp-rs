# Phase 10B: OBIA 面向对象分类 — 实现计划

## 背景

Phase 10A 完成了像元级监督/非监督分类（NormalBayes / SVM / K-Means），293/293 测试通过。Phase 10B 在此基础上实现 OBIA（Object-Based Image Analysis）：先分割影像为同质区域，再提取区域特征，最后用分类器对区域分类。

OTB 源码已 vendor 在 `otb_ref/`，但 `add_subdirectory(otb_ref)` 被 3 个 CMake 兼容性问题阻塞（SICNU_BUILD_OTB=ON 无法编译）。**但 OTB CLI wrapper 不受影响** — 它们通过 `QProcess` 调用外部 `otbcli_*` 二进制，不依赖 vendored 编译。系统若安装了 OTB，分割功能即可用。

## 架构决策

### 分割算法选择

| 方案 | 优点 | 缺点 | 决定 |
|------|------|------|------|
| A) OTB CLI wrapper (MeanShift) | 成熟算法，wrapper 已有骨架 | 需要系统安装 OTB | ✅ 主路径 |
| B) OpenCV SLIC 超像素 | 不需外部二进制 | 需 ximgproc 模块，超像素 ≠ 多边形 | ⏳ v2 |
| C) 自写 MeanShift | 无外部依赖 | 实现复杂，OTB 已有 | ❌ 不做 |

**主路径**: 增强现有 `OtbSegmentationAlgorithm` wrapper（暴露 spatial_radius / range_radius / min_region_size 等参数），产出 label image → 提取特征 → 分类。

**降级路径**: 当 OTB 不可用时，使用高斯平滑 + 简单连通组件标记作为教学演示（功能有限但无需外部依赖）。

### 数据流

```
输入栅格 (N bands)
    ↓
[分割] OTB MeanShift / 连通组件
    ↓
Label Image (uint32, 每像素一个 segment ID)
    ↓
[特征提取] 每 segment 统计 (mean, std, min, max per band + 面积/周长/形状指数)
    ↓
Segment Feature Matrix (M segments × B bands × 4 stats + shape)
    ↓
[训练] 用户在 UI 上对 segment 标注类别 (点击选 segment → 分配 class)
    ↓
[分类] RsClassifierBackend::fit() + predict()
    ↓
分类结果 GeoTIFF (每像素 segment 级 class ID)
```

### 复用 vs 新建

| 组件 | 来源 | 说明 |
|------|------|------|
| `RsClassifierBackend` | 复用 | NormalBayes/SVM/KMeans 直接用于 segment 特征 |
| `RsClassDef` | 复用 | 类别定义完全复用 |
| `RsAccuracyAssessment` | 复用 | 精度评价复用 |
| `RsSegmentMap` | **新建** | label image + 元数据封装 |
| `RsSegmentFeatures` | **新建** | 每 segment 的统计特征提取 |
| `RsObiaTask` | **新建** | QgsTask 子类，分割→特征→分类 pipeline |
| `RsObiaMainWindow` | **新建** | OBIA 独立窗口（对齐 Georeferencer/Classification 节奏） |
| `RsSegmentSelectTool` | **新建** | 点击选 segment 的 map tool |
| OTB wrapper 增强 | **修改** | 暴露更多 MeanShift 参数 |

## 子任务

### 10B.1: 分割数据模型 + 特征提取 (算法层)

**目标**: `RsSegmentMap` + `RsSegmentFeatures` 纯 C++ 类

**文件**:
- 新建: `src/analysis/segmentation/rs_segment_map.h/.cpp`
- 新建: `src/analysis/segmentation/rs_segment_features.h/.cpp`
- 新建: `src/analysis/segmentation/CMakeLists.txt`
- 修改: `src/analysis/CMakeLists.txt` — add_subdirectory
- 新建: `tests/test_segment_features.cpp`

**RsSegmentMap**:
```cpp
class RsSegmentMap {
    QVector<quint32> labels;     // row-major, size = W*H
    int width, height;
    QSet<quint32> uniqueLabels;
    // 从 label image GeoTIFF 加载
    static RsSegmentMap fromGeoTIFF(const QString &path);
    // 获取指定像素的 segment ID
    quint32 labelAt(int row, int col) const;
    // 获取 segment 包含的所有像素坐标
    QVector<QPoint> pixelCoords(quint32 segmentId) const;
    // segment 数量
    int segmentCount() const;
};
```

**RsSegmentFeatures**:
```cpp
struct SegmentStat {
    QVector<double> mean;    // per band
    QVector<double> stddev;
    QVector<double> min;
    QVector<double> max;
    double area;             // pixel count
    double perimeter;        // boundary pixel count
    double shapeIndex;       // perimeter / (4 * sqrt(area))
};

class RsSegmentFeatures {
    // 从原始栅格 + segment map 提取所有 segment 的特征
    static QMap<quint32, SegmentStat> extract(
        const QString &rasterPath,
        const RsSegmentMap &segMap,
        const QVector<int> &bandIndices);
    // 转换为 OpenCV Mat (rows=segments, cols=features)
    static cv::Mat toFeatureMatrix(
        const QMap<quint32, SegmentStat> &stats,
        int &outSegmentCount);
};
```

**测试**: 5 个测试用例 — 构造/labelAt/pixelCoords/提取合成数据/特征矩阵维度

---

### 10B.2: OTB 分割 Wrapper 增强

**目标**: 增强 `OtbSegmentationAlgorithm` 暴露完整 MeanShift 参数

**文件**:
- 修改: `src/processing/providers/otb_tools/algorithms/otb_segmentation.h/.cpp`
- 新建: `tests/test_otb_segmentation_params.cpp`

**新增参数**:
- `SPATIAL_RADIUS` (int, default 5) — 空间带宽
- `RANGE_RADIUS` (double, default 15.0) — 值域带宽
- `MIN_REGION_SIZE` (int, default 100) — 最小区域大小
- `MAX_ITERATION` (int, default 100) — 最大迭代次数
- `OUTPUT_RASTER` (raster destination) — label image 输出（新增，与 vector 输出并存）

**buildArgs 增强**:
```
otbcli_Segmentation -in input.tif -mode meanshift \
  -mode.meanshift.minsize 100 \
  -mode.meanshift.maxiter 100 \
  -mode.meanshift.threshold 0.1 \
  -mode.meanshift.spatialr 5 \
  -mode.meanshift.ranger 15.0 \
  -out vector.shp \
  -mode meanshift -cleanup true
```

同时新增 label image raster 输出（OTB 支持 `-out label.tif uint32`）。

**测试**: 参数验证、buildArgs 输出正确性

---

### 10B.3: 降级分割器 (无 OTB 时)

**目标**: 当 OTB 不可用时的简单分割方案

**文件**:
- 新建: `src/analysis/segmentation/rs_simple_segmenter.h/.cpp`
- 新建: `tests/test_simple_segmenter.cpp`

**算法**: 高斯平滑 (σ 可调) → 量化 (bin 可调) → 连通组件标记 (8-连通)

**接口**:
```cpp
class RsSimpleSegmenter {
public:
    struct Params {
        int smoothKernel = 5;      // 高斯核大小
        int quantizeBins = 32;     // 量化级数
        int minRegionSize = 50;    // 最小区域
    };
    static RsSegmentMap segment(
        const float *bandData, int width, int height,
        const Params &params);
};
```

**测试**: 合成棋盘格 → 验证 segment 数量 / 均匀区域合并

---

### 10B.4: OBIA 分类任务 (QgsTask)

**目标**: `RsObiaTask` — 完整的分割→特征→训练→分类 pipeline

**文件**:
- 新建: `src/app/obia/rs_obia_task.h/.cpp`
- 新建: `src/app/obia/CMakeLists.txt`
- 新建: `tests/test_obia_task.cpp`

**Config**:
```cpp
struct Config {
    QString sourceRaster;
    QString outputRaster;
    QVector<int> bandIndices;
    // 分割参数
    bool useOtb = true;            // OTB 可用时用 OTB，否则降级
    int spatialRadius = 5;
    int rangeRadius = 15;
    int minRegionSize = 100;
    // 分类器
    std::unique_ptr<RsClassifierBackend> backend;
    // 训练数据: segmentId → classId
    QMap<quint32, int> segmentLabels;
    QHash<int, QColor> classColors;
};
```

**Pipeline**:
1. 分割: OTB MeanShift 或 RsSimpleSegmenter → label image
2. 特征提取: RsSegmentFeatures::extract() → feature matrix
3. 构建训练集: 从 segmentLabels 选取已标注 segment 的特征行
4. 训练: backend->fit(trainX, trainY)
5. 预测: backend->predict(allFeatures) → 全部 segment 的 class ID
6. 写出: 按 segment 赋值像素 → GeoTIFF + ColorTable

**测试**: 合成 8×8 栅格 + 2 类 → 验证输出非空 / 像素值正确

---

### 10B.5: OBIA 主窗口 UI

**目标**: 独立 QMainWindow，双模式 (Pixel/OBIA) 切换

**文件**:
- 新建: `src/app/obia/rs_obia_main_window.h/.cpp`
- 新建: `src/app/obia/rs_segment_select_tool.h/.cpp`
- 新建: `src/app/obia/rs_segment_info_dock.h/.cpp`
- 修改: `src/app/main_window.cpp` — OBIA 菜单接入
- 修改: `src/app/CMakeLists.txt` — 链接 qgis_app_obia

**布局** (对齐 design.html ArtboardClassify):
- 左 dock: 图层 + segment 列表 (可点击选 segment)
- 中央: QgsMapCanvas (显示分割结果 + 原图叠加)
- 右 dock: 类别管理 + 分类器设置
- 底 dock: segment 信息 (光谱曲线 / 特征值)
- 工具栏: 分割参数 / 运行 / 导出

**RsSegmentSelectTool**: 点击 map → 获取像素 segment ID → 高亮该 segment 所有像素 (QgsRubberBand)

**接入**:
```cpp
// main_window.cpp — 替换 OBIA placeholder
classifyMenu->addAction(tr("Object-based Classification (OBIA)..."),
                        this, &QgisDesktopWindow::openObiaWindow);
```

**测试**: 窗口构造无崩溃 + 分割参数面板 widget 存在

---

### 10B.6: 集成测试 + 构建验证

**目标**: 全套测试 + 编译通过

**文件**:
- 新建: `tests/test_obia_integration.cpp`
- 修改: `tests/CMakeLists.txt`

**测试**:
1. OTB wrapper 参数构建验证
2. Simple segmenter → SegmentMap → Features → 分类 pipeline
3. 分类结果 GeoTIFF 可被 GDAL 打开
4. 全套 ctest 无回归

**验证**:
- `cmake .. && make -j$(nproc)` ✅
- `ctest --output-on-failure` ✅ (293+ 全绿)
- OTB 可用时: 完整 MeanShift pipeline
- OTB 不可用时: 降级到 SimpleSegmenter，菜单不灰显

---

## 文件清单

### 新建 (~15 文件)

| 文件 | 说明 |
|------|------|
| `src/analysis/segmentation/rs_segment_map.h/.cpp` | Label image 数据模型 |
| `src/analysis/segmentation/rs_segment_features.h/.cpp` | Per-segment 特征提取 |
| `src/analysis/segmentation/rs_simple_segmenter.h/.cpp` | 降级分割器 |
| `src/analysis/segmentation/CMakeLists.txt` | 算法层 CMake |
| `src/app/obia/rs_obia_main_window.h/.cpp` | OBIA 主窗口 |
| `src/app/obia/rs_obia_task.h/.cpp` | OBIA 分类任务 |
| `src/app/obia/rs_segment_select_tool.h/.cpp` | Segment 选择 map tool |
| `src/app/obia/rs_segment_info_dock.h/.cpp` | Segment 信息 dock |
| `src/app/obia/CMakeLists.txt` | UI 层 CMake |
| `tests/test_segment_features.cpp` | 特征提取测试 |
| `tests/test_simple_segmenter.cpp` | 降级分割器测试 |
| `tests/test_obia_task.cpp` | OBIA pipeline 测试 |
| `tests/test_otb_segmentation_params.cpp` | OTB wrapper 参数测试 |
| `tests/test_obia_integration.cpp` | 集成测试 |

### 修改 (~5 文件)

| 文件 | 改动 |
|------|------|
| `src/analysis/CMakeLists.txt` | add_subdirectory(segmentation) |
| `src/processing/providers/otb_tools/algorithms/otb_segmentation.h/.cpp` | 增强参数 |
| `src/app/main_window.cpp` | OBIA 菜单接入 |
| `src/app/CMakeLists.txt` | 链接 qgis_app_obia |
| `tests/CMakeLists.txt` | 新增测试 |

## 测试矩阵

| 测试文件 | 用例数 | 覆盖 |
|----------|--------|------|
| test_segment_features | 5 | 构造/label访问/像素坐标/特征提取/矩阵维度 |
| test_simple_segmenter | 4 | 棋盘格/均匀区域/参数调整/minRegionSize |
| test_obia_task | 3 | pipeline/取消/OTB不可用降级 |
| test_otb_segmentation_params | 3 | 参数列表/默认值/buildArgs |
| test_obia_integration | 2 | 端到端/GDAL输出验证 |
| **总计** | **17** | |

## 执行顺序

```
10B.1 (数据模型) → 10B.2 (OTB wrapper) → 10B.3 (降级分割) → 10B.4 (OBIA Task) → 10B.5 (UI) → 10B.6 (集成)
```

每步 Red-Green-Refactor + commit。

## 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| 系统未装 OTB | MeanShift 不可用 | SimpleSegmenter 降级 + 菜单提示 |
| OTB 输出格式不一致 | label image 解析失败 | 测试覆盖 + GDAL 验证 |
| 大栅格分割慢 | 用户体验差 | 分块处理 + 进度回调 |
| segment 数量过多 | 特征矩阵过大 | minRegionSize 参数控制 |

## Done When

- 17 个新测试全绿 + 293 旧测试无回归
- OTB 可用时: MeanShift 分割 → 特征 → 分类 → GeoTIFF 输出
- OTB 不可用时: SimpleSegmenter 降级，菜单仍可用
- OBIA 窗口可独立启动，segment 点击选择 + 类别分配 + 分类运行
- 结构化日志 `event=obia_finished` JSON 到 QgsMessageLog
