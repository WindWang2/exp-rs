<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 修改请改对应 sidecar 后重新生成。 -->

# 地形分析（terrain）

共 2 个算子。数据源：`data/processing/algorithm_meta/capability/`，本页为生成产物。

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
- 参数：nodata（numeric）、output（string）、pour_points（string）、product（enum）
- 前置条件：Projected DEM recommended; routing is cell-based (orthogonal 1, diagonal sqrt(2)).
- 局限：Filled flats are sinks (direction 0); no flat-resolution routing is attempted (documented debt for a future epsilon-gradient variant).；Full-frame memory: the DEM and two working frames are resident; the estimate states the linear bound.
- 适用地物：流域、河谷、山地
- 适用场景：流域划分、河网提取、水文站选址分析
- 失败模式：
  - `INVALID_PARAMETER` — DEM 存在洼地导致流向中断。处置：先执行填洼（fill sinks）选项或预处理
  - `INSUFFICIENT_MEMORY` — 大区域高分辨率 DEM 流向计算内存超限。处置：分幅处理或重采样降低分辨率
- 教学概念：D8 流向、汇流累积、流域
- 适用课程：GIS 原理、水文建模
- 典型练习：从 DEM 填洼后提取研究区主河网并与真实水系对比。

