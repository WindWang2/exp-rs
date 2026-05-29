# SICNU GEO RS: 处理工具箱扩展设计规范

**生成时间**: 2026-05-29
**分支**: feat/p3-gui
**状态**: 设计完成

---

## 问题陈述

当前处理工具箱仅有 16 个算法（SICNU Native Provider），缺少常用的遥感处理工具。需要集成 GDAL、OTB 和 QGIS 基础算法，构建完整的遥感处理工具箱。

**目标**: 扩展处理工具箱至 100+ 工具，覆盖栅格转换、矢量处理、遥感分类、图像分割等常用操作。

---

## 设计方案

### 1. 架构概览

采用**多 Provider 架构**，每个工具集独立注册为一个 Provider：

```
QgsProcessingRegistry
├── SICNU Native Provider     (已有，16 个算法)
├── GDAL Tools Provider       (新增，QProcess 封装)
├── OTB Tools Provider        (新增，QProcess 封装)
└── QGIS Algorithms Provider  (新增，原生 QgsProcessingAlgorithm)
```

工具箱 UI (`QgsProcessingToolboxTreeView`) 自动按 Provider 分组显示。

### 2. 目录结构

```
src/processing/
├── providers/
│   ├── sicnu_native/              # 已有，重构
│   │   ├── provider.h
│   │   ├── provider.cpp
│   │   └── algorithms/
│   │       ├── vector/
│   │       │   ├── buffer_algorithm.h/.cpp
│   │       │   ├── clip_algorithm.h/.cpp
│   │       │   └── ...
│   │       ├── raster/
│   │       │   ├── clip_raster_algorithm.h/.cpp
│   │       │   └── ...
│   │       └── projection/
│   │           ├── reproject_algorithm.h/.cpp
│   │           └── ...
│   ├── gdal_tools/                # 新增
│   │   ├── provider.h
│   │   ├── provider.cpp
│   │   ├── gdal_tool_wrapper.h    # QProcess 通用封装
│   │   ├── gdal_tool_wrapper.cpp
│   │   └── algorithms/
│   │       ├── gdal_translate.h/.cpp
│   │       ├── gdal_warp.h/.cpp
│   │       ├── gdal_info.h/.cpp
│   │       ├── gdal_dem.h/.cpp
│   │       ├── gdal_contour.h/.cpp
│   │       ├── gdal_polygonize.h/.cpp
│   │       ├── gdal_merge.h/.cpp
│   │       ├── gdal_calc.h/.cpp
│   │       ├── ogr2ogr.h/.cpp
│   │       └── ...
│   ├── otb_tools/                 # 新增
│   │   ├── provider.h
│   │   ├── provider.cpp
│   │   ├── otb_tool_wrapper.h     # QProcess 通用封装
│   │   ├── otb_tool_wrapper.cpp
│   │   └── algorithms/
│   │       ├── otb_band_math.h/.cpp
│   │       ├── otb_segmentation.h/.cpp
│   │       ├── otb_classification.h/.cpp
│   │       ├── otb_feature_extraction.h/.cpp
│   │       ├── otb_extract_roi.h/.cpp
│   │       └── ...
│   └── qgis_algorithms/          # 新增
│       ├── provider.h
│       ├── provider.cpp
│       └── algorithms/
│           ├── raster/
│           │   ├── raster_calculator.h/.cpp
│           │   ├── raster_resample.h/.cpp
│           │   ├── raster_clip.h/.cpp
│           │   ├── raster_merge_bands.h/.cpp
│           │   ├── raster_ndvi.h/.cpp
│           │   └── raster_statistics.h/.cpp
│           └── vector/
│               ├── vector_buffer.h/.cpp
│               ├── vector_clip.h/.cpp
│               ├── vector_dissolve.h/.cpp
│               ├── vector_merge.h/.cpp
│               ├── vector_spatial_query.h/.cpp
│               ├── vector_attribute_query.h/.cpp
│               └── vector_reproject.h/.cpp
├── CMakeLists.txt                 # 处理库构建
└── tools/                         # 工具路径管理
    └── tool_path_manager.h/.cpp
```

### 3. GDAL 工具集成

#### 3.1 通用封装类

```cpp
// gdal_tool_wrapper.h
class GdalToolWrapper : public QgsProcessingAlgorithm
{
public:
    // 子类实现
    virtual QString toolName() const = 0;  // e.g., "gdal_translate"
    virtual QStringList buildArgs(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) = 0;

    // 通用执行
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

    // 工具路径查找
    static QString findToolPath(const QString &toolName);

protected:
    // 通用参数定义
    void addInputRasterParameter();
    void addOutputRasterParameter();
    void addExtentParameter();
    void addCrsParameter();
};
```

#### 3.2 工具路径查找顺序

1. 应用目录: `{app_dir}/tools/gdal/{tool_name}`
2. 环境变量: `SICNU_GDAL_PATH`
3. 系统 PATH: `{tool_name}`

#### 3.3 GDAL 工具列表

| 工具 | 描述 | 类型 |
|------|------|------|
| gdal_translate | 栅格格式转换 | 栅格 |
| gdalwarp | 栅格重投影和裁剪 | 栅格 |
| gdalinfo | 栅格信息查询 | 栅格 |
| gdaldem | 地形分析（坡度/坡向/山体阴影） | 栅格 |
| gdal_calc.py | 栅格计算器 | 栅格 |
| gdal_merge.py | 栅格合并 | 栅格 |
| gdal_retile.py | 栅格切片 | 栅格 |
| gdal_contour | 等值线生成 | 矢量 |
| gdal_polygonize | 栅格转矢量 | 矢量 |
| gdalbuildvrt | 虚拟栅格构建 | 栅格 |
| gdaltindex | 栅格索引 | 矢量 |
| gdalmanage | 栅格数据管理 | 栅格 |
| ogr2ogr | 矢量格式转换 | 矢量 |
| ogrinfo | 矢量信息查询 | 矢量 |
| ogrtindex | 矢量索引 | 矢量 |

### 4. OTB 工具集成

#### 4.1 通用封装类

```cpp
// otb_tool_wrapper.h
class OtbToolWrapper : public QgsProcessingAlgorithm
{
public:
    virtual QString applicationName() const = 0;  // e.g., "Segmentation"
    virtual QStringList buildArgs(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) = 0;

    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

    static QString findOtbcliPath(const QString &appName);
};
```

#### 4.2 OTB Applications 列表

| Application | 描述 | 类型 |
|-------------|------|------|
| BandMath | 波段数学运算 | 遥感 |
| ConcatenateImages | 影像拼接 | 遥感 |
| ExtractROI | 感兴趣区域提取 | 遥感 |
| DynamicConvert | 动态范围转换 | 遥感 |
| Rescale | 重缩放 | 遥感 |
| Convert | 格式转换 | 遥感 |
| MeanShiftSmoothing | 均值漂移平滑 | 分割 |
| LSMS | 大尺度均值漂移 | 分割 |
| Segmentation | 图像分割 | 分割 |
| TrainVectorClassifier | 分类器训练 | 分类 |
| ImageClassifier | 图像分类 | 分类 |
| KMeansClassification | K均值分类 | 分类 |
| FeatureExtraction | 特征提取 | 特征 |
| HaralickTextureExtraction | 纹理特征提取 | 特征 |
| RadiometricIndices | 辐射指数计算 | 特征 |
| OrthoRectification | 正射校正 | 几何 |
| BundleToPerfectSensor | 全色锐化 | 几何 |
| Superimpose | 影像配准 | 几何 |
| BinaryMorphologicalOperation | 形态学操作 | 图像处理 |

### 5. QGIS 基础算法

原生 `QgsProcessingAlgorithm` 实现，直接使用 QGIS C++ API。

#### 5.1 栅格操作

| 算法 | 描述 | 核心 API |
|------|------|----------|
| RasterCalculator | 栅格计算器 | QgsRasterCalculator |
| RasterResample | 重采样 | QgsRasterFileWriter |
| RasterClip | 栅格裁剪 | QgsRasterClipper |
| RasterMergeBands | 波段合并 | QgsRasterPipe |
| RasterNDVI | NDVI 计算 | QgsRasterCalculator |
| RasterStatistics | 栅格统计 | QgsRasterBandStats |

#### 5.2 矢量操作

| 算法 | 描述 | 核心 API |
|------|------|----------|
| VectorBuffer | 缓冲区分析 | QgsGeometry::buffer() |
| VectorClip | 矢量裁剪 | QgsGeometry::intersection() |
| VectorDissolve | 要素融合 | QgsGeometry::combine() |
| VectorMerge | 矢量合并 | QgsVectorLayerUtils |
| VectorSpatialQuery | 空间查询 | QgsSpatialIndex |
| VectorAttributeQuery | 属性查询 | QgsExpression |
| VectorReproject | 投影变换 | QgsCoordinateTransform |

### 6. CMake 打包策略

#### 6.1 GDAL 工具下载

```cmake
# cmake/DownloadGdalTools.cmake
include(FetchContent)

set(GDAL_TOOLS_VERSION "3.8.0")
set(GDAL_TOOLS_URL "https://github.com/OSGeo/gdal/releases/download/v${GDAL_TOOLS_VERSION}/gdal-${GDAL_TOOLS_VERSION}-linux-x86_64.tar.gz")

FetchContent_Declare(
  gdal_tools
  URL ${GDAL_TOOLS_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/tools/gdal
)

FetchContent_MakeAvailable(gdal_tools)

# 安装到应用目录
install(DIRECTORY ${CMAKE_BINARY_DIR}/tools/gdal/bin/
        DESTINATION tools/gdal
        USE_SOURCE_PERMISSIONS)
```

#### 6.2 OTB 工具下载

```cmake
# cmake/DownloadOtbTools.cmake
set(OTB_VERSION "9.1.0")
set(OTB_URL "https://www.orfeo-toolbox.org/packages/OTB-${OTB_VERSION}-Linux64.tar.gz")

FetchContent_Declare(
  otb_tools
  URL ${OTB_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/tools/otb
)

FetchContent_MakeAvailable(otb_tools)

install(DIRECTORY ${CMAKE_BINARY_DIR}/tools/otb/bin/
        DESTINATION tools/otb
        USE_SOURCE_PERMISSIONS)
```

#### 6.3 工具路径管理器

```cpp
// tool_path_manager.h
class ToolPathManager
{
public:
    static QString gdalToolPath(const QString &toolName);
    static QString otbToolPath(const QString &appName);
    static bool isGdalAvailable();
    static bool isOtbAvailable();

private:
    static QString s_appDir;
    static QString s_gdalPath;
    static QString s_otbPath;
};
```

### 7. Provider 注册

在 `SicnuMainWindow::initialize()` 中注册所有 Provider：

```cpp
void SicnuMainWindow::loadProviders()
{
    auto *registry = QgsApplication::processingRegistry();

    // 已有
    registry->addProvider(new SicnuNativeAlgorithms());

    // 新增
    registry->addProvider(new GdalToolsProvider());
    registry->addProvider(new OtbToolsProvider());
    registry->addProvider(new QgisAlgorithmsProvider());
}
```

---

## 实施计划

### 阶段 1：基础设施（1-2 天）
1. 创建目录结构
2. 实现 ToolPathManager
3. 实现 GdalToolWrapper 基类
4. 实现 OtbToolWrapper 基类
5. CMake 下载脚本

### 阶段 2：GDAL 工具（2-3 天）
1. 实现 GdalToolsProvider
2. 实现核心 GDAL 工具（gdal_translate, gdalwarp, gdalinfo）
3. 实现完整 GDAL 工具集
4. 测试

### 阶段 3：OTB 工具（2-3 天）
1. 实现 OtbToolsProvider
2. 实现核心 OTB 工具（BandMath, Segmentation, Classification）
3. 实现完整 OTB 工具集
4. 测试

### 阶段 4：QGIS 算法（2-3 天）
1. 实现 QgisAlgorithmsProvider
2. 实现栅格操作（6 个算法）
3. 实现矢量操作（7 个算法）
4. 测试

### 阶段 5：集成测试（1 天）
1. 注册所有 Provider
2. 测试工具箱显示
3. 测试算法执行
4. 修复问题

---

## 验证标准

1. **工具箱显示**：4 个 Provider 正确分组显示
2. **GDAL 工具**：所有 GDAL 工具可通过 QProcess 正确调用
3. **OTB 工具**：所有 OTB Application 可通过 QProcess 正确调用
4. **QGIS 算法**：所有原生算法正确执行
5. **路径管理**：工具路径正确查找（打包路径 > 环境变量 > 系统 PATH）
6. **错误处理**：工具不存在或执行失败时给出清晰错误信息
7. **性能**：工具执行不影响 UI 响应（异步执行）

---

## 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| GDAL/OTB 二进制下载失败 | 高 | 提供离线安装选项，支持系统 PATH 回退 |
| OTB 版本不兼容 | 中 | 版本锁定，提供版本检测 |
| QProcess 执行超时 | 中 | 实现超时机制和取消支持 |
| 工具参数解析错误 | 中 | 严格验证参数，提供清晰错误信息 |
| 跨平台兼容性 | 中 | 平台检测，提供平台特定的工具包 |
