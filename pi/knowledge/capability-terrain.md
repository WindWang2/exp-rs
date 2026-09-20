<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0154）。 修改请改对应 sidecar 后重新生成。 -->

# 地形分析（terrain）

共 5 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

## rs:terrain_analysis

地形参数分析：由 DEM 计算坡度、坡向、山体阴影等地形因子，是所有地形定量化分析的入口。

- 确定性：逐位一致（bit_exact）
- 模态：dem
- 输入：input（raster）
- 输出：height（integer）、output（raster）、product（string）、width（integer）
- 参数：cellSize（numeric）、nodata（numeric）、output（string）、product（enum）、sunAzimuth（numeric）、sunElevation（numeric）、zFactor（numeric）
- 前置条件：建议使用投影坐标系（米制）的 DEM。
- 适用地物：山地、丘陵、河谷
- 适用场景：地形因子制图、滑坡/水土流失分析的基础图层
- 失败模式：
  - `INVALID_PARAMETER` — Z 因子与 DEM 垂直单位不匹配（度 vs 米）。处置：确认 Z 因子参数与 DEM 高程单位一致
  - `CRS_MISMATCH` — DEM 使用地理坐标系导致坡度计算错误。处置：先重投影到投影坐标系（米制）再计算
- 教学概念：坡度、坡向、山体阴影
- 适用课程：GIS 原理、地形分析
- 典型练习：由 SRTM DEM 生成坡度、坡向与山体阴影三张图层并叠加判读。

## rs:terrain_flow

地形水文分析：由 DEM 计算流向、汇流累积并提取汇水区/河网，服务流域建模。

- 确定性：逐位一致（bit_exact）
- 模态：dem
- 输入：input（raster）
- 输出：output（raster）、product（string）
- 参数：include_segments（enum）、nodata（numeric）、output（string）、pour_points（string）、product（enum）、threshold（numeric）
- 前置条件：Projected DEM recommended; routing is cell-based (orthogonal 1, diagonal sqrt(2)).
- 局限：stream_network/directions run on the filled surface of this run; D∞ accumulation uses the single steepest-facet receiver (no fraction splitting, documented follow-up).；Full-frame memory: the DEM and two working frames are resident; the estimate states the linear bound.；Filled flats are sinks (direction 0); no flat-resolution routing is attempted (documented debt for a future epsilon-gradient variant).
- 适用地物：流域、河谷、山地
- 适用场景：流域划分、河网提取、水文站选址分析
- 失败模式：
  - `INVALID_PARAMETER` — DEM 存在洼地导致流向中断。处置：先执行填洼（fill sinks）选项或预处理
  - `INSUFFICIENT_MEMORY` — 大区域高分辨率 DEM 流向计算内存超限。处置：分幅处理或重采样降低分辨率
- 教学概念：D8 流向、汇流累积、流域
- 适用课程：GIS 原理、水文建模
- 典型练习：从 DEM 填洼后提取研究区主河网并与真实水系对比。

## rs:terrain_landform

地貌形态分析：多尺度地形位置指数（TPI）、Weiss 六类地貌分类与 J&S 地貌形态元（ternary pattern）分类。

- 确定性：逐位一致（bit_exact）
- 模态：dem
- 输入：input（raster）
- 输出：output（raster）、product（string）
- 参数：flat_radius（numeric）、flat_slope_deg（numeric）、flat_thresh_deg（numeric）、inner_radius（numeric）、nodata（numeric）、outer_radius（numeric）、output（string）、product（enum）、radii（string）、search_radius（numeric）
- 前置条件：Projected (metric) DEM recommended; radii are in cells.
- 局限：Full-frame memory: peak ≈ 24 + 8·scales bytes/cell (integral images + retained per-scale TPI); capped by SICNU_TERRAIN_MAX_CELLS.；Square TPI windows; geomorphon uses nearest-cell line-of-sight scans.；Full-frame memory (≈ 12 bytes/cell per scale pass); capped by SICNU_TERRAIN_MAX_CELLS.
- 适用地物：山地、丘陵、河谷
- 适用场景：地貌形态制图、多尺度地形位置分区、坡面单元划分
- 失败模式：
  - `INVALID_PARAMETER` — radii 含非正整数或超过 16 个尺度。处置：给出 1–16 个递增的像元半径
  - `INVALID_PARAMETER` — geomorphon 参数非法（search_radius ≤ 0、flat_thresh ≥ 90 等）。处置：使用默认参数或检查单位（像元/度）
  - `INSUFFICIENT_MEMORY` — 多尺度 TPI/地貌形态在大 DEM 上内存超预算。处置：减少尺度数量或提高 SICNU_TERRAIN_MAX_CELLS
- 教学概念：地形位置指数 TPI、多尺度分析、Weiss 地貌分类、地貌形态元 geomorphon
- 适用课程：GIS 原理、地貌学
- 典型练习：用不同尺度组合对同一山区做地貌分区，讨论尺度对分类结果的影响。

## rs:terrain_solar

太阳地形分析：按太阳轨迹（显式给出或按日期/纬度以本地太阳时生成）计算地形阴影时长分数，并生成逐时相山体阴影序列。

- 确定性：逐位一致（bit_exact）
- 模态：dem
- 输入：input（raster）
- 输出：output（raster）、product（string）
- 参数：day_of_year（numeric）、end_hour（numeric）、latitude（numeric）、nodata（numeric）、output（string）、product（enum）、start_hour（numeric）、step_hours（numeric）、sun_track（string）
- 前置条件：Projected (metric) DEM recommended; cell size comes from the geotransform.
- 局限：Full-frame memory (~16 bytes/cell for shadow duration); capped by SICNU_TERRAIN_MAX_CELLS.；No curvature/refraction on shadows (v1); quantized 1° azimuth sectors.；Track length capped at 1024 samples.
- 适用地物：山地、河谷、建筑场地
- 适用场景：日照/阴影时长制图、场地太阳能潜力评估、山体阴影动画帧生成
- 失败模式：
  - `INVALID_PARAMETER` — sun_track 全为夜间样本（elevation ≤ 0）或为空。处置：提供含白天样本的轨迹，或调整生成时段
  - `INVALID_PARAMETER` — sun_track 超过 1024 个样本。处置：增大采样步长（step_hours）
  - `INSUFFICIENT_MEMORY` — 超大 DEM 阴影时长计算内存超预算。处置：分幅处理或提高 SICNU_TERRAIN_MAX_CELLS
- 教学概念：平行光线阴影、太阳轨迹、阴影时长、山体阴影
- 适用课程：GIS 原理、地形分析
- 典型练习：对比冬至/夏至同一山地的阴影时长图，解释坡向对日照差异的影响。

## rs:terrain_viewshed

地形视域分析：单观察点视域与多观察点累计覆盖（环扫 R3，可选地球曲率/大气折射校正），服务通视与选址评估。

- 确定性：逐位一致（bit_exact）
- 模态：dem
- 输入：input（raster）
- 输出：output（raster）、product（string）
- 参数：curvature（enum）、nodata（numeric）、observer（string）、observer_height（numeric）、observers（string）、output（string）、product（enum）、radius（numeric）、refraction_k（numeric）、target_height（numeric）
- 前置条件：Projected (metric) DEM recommended; curvature correction refuses geographic CRS.
- 局限：Full-frame memory (~22 bytes/cell); capped by SICNU_TERRAIN_MAX_CELLS (2^28 cells default).；Permissive R3 merge may slightly overestimate visibility on convex ridgelines.；Observers on NoData are refused; rays stop at NoData cells.
- 适用地物：山地、城市周边、河谷
- 适用场景：瞭望塔/基站选址、通视与景观视线分析、多观察点覆盖评估
- 失败模式：
  - `INVALID_PARAMETER` — observer 位于栅格外或 NoData 单元上。处置：给出栅格内的 'col,row' 且避开 NoData 区域
  - `INVALID_PARAMETER` — curvature=true 但输入为地理坐标系（度）。处置：先投影到投影坐标系（米）再启用曲率校正
  - `INSUFFICIENT_MEMORY` — 超大 DEM 全帧视域内存超预算。处置：设置 radius 限制分析范围或提高 SICNU_TERRAIN_MAX_CELLS
- 教学概念：视域、地平线、地球曲率与折射、累计可视度
- 适用课程：GIS 原理、空间分析
- 典型练习：在山地区域为瞭望塔选址：单点视域与多塔累计覆盖对比，解释山脊遮挡。

