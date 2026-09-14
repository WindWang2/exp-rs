<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 数据导入（io）

共 8 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:cn_product_import

国产卫星产品统一导入：自动识别 GF-1/2/6/7、ZY-3、ZY-1 02C、HJ-1/2 CCD 家族，走标准化导入计划（识别→检查→校验→组成解析→角色映射→可选定标→堆栈→溯源）。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bandCount（integer）、completeness（string）、output（raster）、productId（string）、sensorKey（string）
- 参数：apply_calibration（boolean）、bands（string）、input（string）、output（string）
- 前置条件：Product directory with CRESDA sidecar XML and TIFF (offline)；离线可用：仅解析产品自带文件；传感器画像来自 data/products/sensor_profiles。
- 适用地物：任意地物
- 适用场景：不确定产品家族时的统一入口、Agent 自动化数据准备
- 失败模式：
  - `DATASET_NOT_FOUND` — 无法解析 sidecar/TIFF 组成。处置：按结果中 missingConstituents 补齐产品文件
  - `NOT_SUPPORTED` — 产品家族未被适配。处置：结果携带具体原因；不回退到通用栅格读取
  - `INVALID_PARAMETER` — apply_calibration=true 但部分波段缺定标系数。处置：查看 bandsMissingCoefficients；改用发布定标表或不定标
- 教学概念：产品识别、sidecar 世代检测、传感器画像、可选定标、导入溯源
- 适用课程：遥感数据处理
- 典型练习：对混合目录中的国产产品统一调用导入，从结果判断完整性与缺失声明字段。

## rs:gaofen_import

高分数据导入：读取 GF-1/2/6 PMS/WFV 与 GF-7 FWD/BWD L1A 产品包（CRESDA sidecar XML + TIFF），自动解析元数据、映射波段角色并标注太阳几何与定标系数。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bandCount（integer）、output（raster）、productId（string）
- 参数：apply_calibration（boolean）、bands（string）、input（string）、output（string）
- 前置条件：Product directory with CRESDA sidecar XML and TIFF (offline)；离线可用：仅解析产品自带文件；大部分批次需另查发布定标表做 TOA 转换。
- 适用地物：任意地物
- 适用场景：高分系列数据处理入口、国内教学数据标准化导入
- 失败模式：
  - `DATASET_NOT_FOUND` — 产品目录缺少 sidecar XML 或测量 TIFF。处置：检查 CRESDA 产品包的 -MSS1.xml/-PAN1.xml 与同名 TIFF
  - `NOT_SUPPORTED` — 输入为 GF-3/GF-4/GF-5 等未适配家族。处置：查看诊断原因；SAR/高光谱产品暂不支持
- 教学概念：CRESDA 产品结构、波段角色、辐射定标声明、太阳几何
- 适用课程：遥感数据处理
- 典型练习：导入 GF-1 WFV 四波段影像并直接计算 NDVI（波段角色自动解析）。

## rs:hj_import

环境减灾 CCD 数据导入：读取 HJ-1A/1B CCD 与 HJ-2A/B CCD L1A 产品，映射 B1–B4 波段角色并标注声明元数据。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bandCount（integer）、output（raster）、productId（string）
- 参数：apply_calibration（boolean）、bands（string）、input（string）、output（string）
- 前置条件：Product directory with CRESDA sidecar XML and TIFF (offline)
- 适用地物：植被、水体、灾区
- 适用场景：环境减灾数据处理入口、灾区快速 NDVI/水体提取
- 失败模式：
  - `DATASET_NOT_FOUND` — sidecar XML 或 TIFF 缺失。处置：检查 HJ1A/HJ1B/HJ2A/HJ2B-CCD 命名产品包完整性
  - `NOT_SUPPORTED` — HJ-1 IRS 或 HJ-2 HSI 载荷不支持。处置：仅支持 CCD 光学相机产品
- 教学概念：HJ CCD 产品结构、波段角色、16m/30m 分辨率
- 适用课程：遥感数据处理
- 典型练习：导入 HJ-1A CCD 四波段并计算大区域 NDVI。

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

## rs:zy3_import

资源三号数据导入：读取 ZY-3 TLC/NAD/FWD/BWD L1A 产品，自动区分全色/多光谱（按声明波段清单），映射波段角色并标注元数据。

- 确定性：逐位一致（bit_exact）
- 模态：optical
- 输出：bandCount（integer）、output（raster）、productId（string）
- 参数：apply_calibration（boolean）、bands（string）、input（string）、output（string）
- 前置条件：Product directory with CRESDA sidecar XML and TIFF (offline)
- 适用地物：任意地物
- 适用场景：资源三号立体测绘数据处理、教学立体观察数据准备
- 失败模式：
  - `DATASET_NOT_FOUND` — sidecar XML 或 TIFF 缺失。处置：检查 ZY3_NAD/TLC/FWD/BWD 命名产品包完整性
  - `NOT_SUPPORTED` — ZY-1 02D/02E AHSI 高光谱家族不支持。处置：高光谱产品暂不支持
- 教学概念：ZY-3 产品结构、全色/多光谱区分、波段角色
- 适用课程：遥感数据处理
- 典型练习：导入 ZY-3 NAD 多光谱四波段并输出角色标注的多波段 GeoTIFF。

