<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 数据导入（io）

共 4 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:landsat_import

Landsat 数据导入：读取 Landsat 系列（5/7/8/9）产品包，自动解析元数据完成定标、角度与 QA 波段组织。

- 确定性：逐位一致（bit_exact）
- 模态：optical、thermal
- 输出：bandCount（integer）、output（raster）、productId（string）
- 参数：bands（string）、input（string）、output（string）
- 前置条件：Scene directory with *_MTL.txt and band GeoTIFFs
- 适用地物：任意地物
- 适用场景：Landsat 存档数据处理入口、长时序生产的标准化输入
- 失败模式：
  - `DATASET_NOT_FOUND` — 产品包文件缺失或命名不合规。处置：检查 MTL 元数据文件与产品目录结构
- 教学概念：Landsat 产品结构、元数据解析、辐射定标
- 适用课程：遥感数据处理
- 典型练习：导入 Landsat 8 Collection 2 产品包并输出 TOA 反射率与 QA 掩膜。
- 可接下游：rs:atmospheric_correction

## rs:modis_georeference

MODIS 地理参考处理：把 Sinusoidal/Integerized 网格的 MODIS 数据重投影并几何对齐到用户网格。

- 确定性：逐位一致（bit_exact）
- 模态：optical、thermal
- 输出：dstCrs（string）、output（raster）、tileH（integer）、tileV（integer）
- 参数：dstCrs（string）、input（string）、output（string）、resampling（enum）、tileH（integer）、tileV（integer）
- 前置条件：Filename containing hXXvYY, or explicit tileH/tileV parameters
- 适用地物：任意地物（MODIS）
- 适用场景：MODIS 本地网格对齐、时序生产的网格统一
- 失败模式：
  - `CRS_MISMATCH` — 源/目标投影信息缺失。处置：提供完整的目标 CRS 与变换参数
- 教学概念：重投影、几何对齐
- 适用课程：遥感数据处理
- 典型练习：把 Sinusoidal 投影的 MODIS LST 对齐到区域 1km 网格。

## rs:modis_import

MODIS 数据导入：读取 MODIS HDF 产品（MOD13Q1 等），完成重投影与定标缩放（scale/offset），输出标准栅格。

- 确定性：逐位一致（bit_exact）
- 模态：optical、thermal
- 输出：bandCount（integer）、output（raster）、productId（string）、tileH（integer）、tileV（integer）
- 参数：bands（string）、input（string）、output（string）
- 前置条件：GDAL with HDF4 and/or HDF5 for NASA .hdf; GeoTIFF exports always work
- 适用地物：植被、地表温度、积雪
- 适用场景：大区域低分辨率时序生产、全球产品本地化
- 失败模式：
  - `DATASET_NOT_FOUND` — HDF 文件缺失或损坏。处置：检查下载文件完整性
  - `CRS_MISMATCH` — 目标投影参数缺失。处置：显式指定目标 EPSG/WKT 与分辨率
- 教学概念：HDF 格式、Sinusoidal 投影、scale/offset
- 适用课程：遥感数据处理
- 典型练习：导入一年 MOD13Q1 NDVI 并重投影到研究区 UTM 网格。

## rs:sentinel2_import

Sentinel-2 数据导入：解析 SAFE/JP2 产品包，输出多波段反射率（含 10/20/60m 多分辨率组织）与 SCL 掩膜。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bandCount（integer）、output（raster）、productId（string）
- 参数：bands（string）、input（string）、output（string）、resolution（enum）
- 前置条件：Unzipped .SAFE tree with GRANULE/IMG_DATA rasters
- 适用地物：任意地物
- 适用场景：Sentinel-2 处理入口、红边/SWIR 应用数据准备
- 失败模式：
  - `DATASET_NOT_FOUND` — SAFE 目录结构不完整。处置：检查 Granule 与 MTD 元数据文件齐全
- 教学概念：SAFE 格式、分辨率分级、SCL 场景分类
- 适用课程：遥感数据处理
- 典型练习：导入 L1C 产品并组织 10m 四波段子集供后续指数计算。
- 可接下游：rs:atmospheric_correction

