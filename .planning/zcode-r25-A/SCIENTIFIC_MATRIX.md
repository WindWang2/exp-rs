# R25-A SCIENTIFIC_MATRIX — A 域算子族覆盖与证据

图例：🟢 深读+测试覆盖 / 🟡 本轨修复或深化 / ⚪ 未在本轮直接触碰（依赖既有回归）

| 域 | 算子/内核 | 状态 | 证据 |
|---|---|---|---|
| 地形/水文 | terrain_flow（fill/D8/accum/watershed/flat/D∞/stream/outlets） | 🟡 | DEF-2：uint64 精确累积+Float64 产物；test_r25_a_terrain_flow_precision（>2²⁴ 漏斗 + ≤2²⁴ metamorphic + 哨兵 #783 + E2E） |
| 地形/太阳 | terrain_solar（shadow_duration/hillshade_series） | 🟡 | DEF-1：纬度三级解析；test_r25_a_terrain_solar_latitude（地理/投影 CRS、无 CRS fail-closed、参数优先、越界拒绝） |
| 地形 | terrain_analysis/viewshed/landform | 🟢 | Explore 审计 + test_terrain_viewshed/hydrology 既有回归（#1718 修复完好） |
| 光学 QA | qa_mask（landsat/SCL/bitmask） | 🟡 | FEAT-2：cloud_confidence standard/relaxed + SCL 直方图；test_r25_a_qa_confidence |
| 光学掩膜 | apply_mask | 🟡 | DEF-6：WAVELENGTH_UNITS 保真；test_r25_a_spectral_guards + 光学链 E2E |
| 光学指数 | spectral_index（NDVI 族全公式） | 🟢 | 审计全公式核（EVI/SAVI/MSAVI/EVI2/BAI/ARVI/BUI/NBR/MNDWI/NDSI/NDTI/NDRE/CI/UI）；光学链 E2E NoData 传播 |
| 光谱 | spectral_derivative | 🟡 | DEF-4：单位归一化 + 未知单位类型化拒绝；test_r25_a_spectral_guards |
| 光谱 | spectral_resample | 🟡 | DEF-7：源轴单位归一化；μm 源+nm 目标精确内插 oracle |
| 光谱 | continuum_removal | ⚪🟢 | 包络对 x 轴缩放不变（审计证明）；单位对数值无影响 |
| 光谱分类 | SAM/SID/sidClassify | 🟡 | DEF-5：不相交 +inf；部分重叠契约保持（既有 pin） |
| 光谱分类 | hybrid similarity（JEIOTan 族） | 🟡 | P3：meanScore 非有限过滤 |
| 光谱库 | library match/scale（spectral_library*） | 🟢 | 审计：stable_sort+NaN 键序、度转换精确 |
| 高光谱 | PCA/MNF/MNF⁻¹/VCA/PPI/FCLS/OLS/NNLS | 🟢 | 审计：Jacobi 特征解、白化/逆变换代数核对、KKT 主元、确定性 RNG |
| 目标检测 | CEM/OSP/TCIMF/MF/ACE | 🟢 | 审计：权重公式与投影门核对 |
| 异常 | RX/local RX | 🟢 | 审计：N−1 协方差、trace 缩放脊、双窗守卫 |
| 稀疏解混 | FISTA 稀疏 | 🟢 | 审计：prox/动量/Lipschitz 上界 |
| SAR | terrain_flatten / terrain_correction | 🟡 | FEAT-1：几何回退链+provenance+输出再声明；test_r25_a_sar_geometry_fallback（平地 θi=θ0 数值 oracle） |
| SAR | calibrate/backscatter/masks/geocode/纹理/比值/极化/InSAR 族 | 🟢 | 审计面 + #1718/#1722 修复回归；masks 写元数据（FEAT-1 消费端） |
| 变化检测 | post_classification_change | 🟡 | FEAT-3：类别面积表 km²+基准命名；test_r25_a_change_area_table |
| 变化检测 | dNBR/logRatio/IR-MAD/Otsu/KI | 🟢 | 审计：直方图约定一致、MAD 状态、χ² NR 级数；#1722 修复完好 |
| 时序 | temporal smooth/phenology/trend/anomaly/monitor | 🟢 | R24-A whittaker 金值 pin + #1718 重复日期修复回归；本轮未触碰（无新缺陷证据） |
| 镶嵌/融合 | mosaic/quality_mosaic/fusion | ⚪ | 审计（mosaic_plan CRS 变换模式被 FEAT 复用）；既有回归；未发现新缺陷 |

**统计**：259 operator 目录条目按域归并为上述 24 族；🟡 12 处本轨实改，🟢 12 处深读确认（其中光谱/分类/镶嵌家族为本轮新完成的深审——R24-A 因限流未覆盖的面）。
