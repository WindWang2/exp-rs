# R25-A ORACLES — 每项缺陷/深化的可验证验收

## 缺陷（master-RED → HEAD-GREEN×2）
| ID | Oracle | RED 判别 | 测试 |
|---|---|---|---|
| DEF-1 terrain_solar 纬度 | day 355 悉尼(−33°)夏季 vs 40°N 冬季日光样本数 13 vs 9；UTM 推导 17 vs 19；无 CRS fail-closed | daylightSamples=9（错） | test_r25_a_terrain_solar_latitude.cpp |
| DEF-2 D8 累积 | 4097²=16,785,409（奇数>2²⁴）漏斗汇流计数精确；Float64 产物 dtype | float32 得 16,785,408 | test_r25_a_terrain_flow_precision.cpp |
| DEF-4 derivative 单位 | μm 轴 460..950nm 线性坡 dr/dλ=1/490；中点轴 465nm；未知单位类型化拒绝 | 导数=2.04（1000×） | test_r25_a_spectral_guards.cpp |
| DEF-5 SID 不相交 | [1,0,0] vs [0,1,0] → +inf；部分重叠保持有限契约；sidClassify 不再选类 0 | SID=0.0（完美匹配假象） | 同上 |
| DEF-6 apply_mask 单位 | 掩膜输出带 WAVELENGTH_UNITS=Micrometers | units 丢失 nullptr | 同上 |
| DEF-7 resample 源轴 | μm 源轴 + nm 目标 500/600/700 → 精确坡值 (λ−460)/490 | 全 NaN（范围外） | 同上 |

## 功能深化
| ID | Oracle | 测试 |
|---|---|---|
| FEAT-1 SAR 几何回退链 | param > SICNU_SAR_* 元数据 > 默认+警告；平地 localIncidence==声明 θ0（40 not 30）；输出再声明几何（40/90）；垃圾元数据带来源 fail-closed | test_r25_a_sar_geometry_fallback.cpp |
| FEAT-2(计划) W1 光学链诊断深化 | 见 W1 复核结论后定 | 待定 |

## 数值 oracle 四类
1. 手算小格网：DEF-1 日长公式、DEF-4 线性坡、FEAT-1 平地 θi=θ0（foundation5 pin）
2. 独立慢算法：DEF-2 漏斗逐格组合计数（行前缀 (H−r)·W）
3. metamorphic：DEF-2 ≤2²⁴ float API 精确不变 + #783 哨兵保持；DEF-5 部分重叠契约保持；DEF-1 显式参数优先保持
4. 生产 E2E：全部经 RSOperatorRegistry 实跑 GDAL 产线
