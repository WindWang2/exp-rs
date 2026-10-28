# r25-A: 遥感科学算法与处理链正确性、性能及现有算法能力深化

- **BASE**: `2610cb4741e69a89124921e44c03e340e2522ee2` (origin/master @ 2026-10-28)
- **HEAD**: `<待 push 后回填>`
- **分支**: `agent/r25-A-science-processing-20261028T140339Z`
- **Issue 归属**: 任务书候选 #1538/#1550/#1631/#1646/#1687 经实时复核均为 FIXED_IN_MASTER（#1718/#1722 已修，本 PR 只出确认报告，见 ISSUE_TRIAGE.md）；本 PR 全部为新缺陷与新深化。

## 新缺陷（master-RED → HEAD-GREEN×2）

| ID | 缺陷 | 位置 |
|---|---|---|
| DEF-1 | rs:terrain_solar 纬度静默默认 40°N（南半球夏季 DEM 被当作 40°N 冬季算阴影） | rs_terrain_solar_operator.cpp |
| DEF-2 | D8 flowAccumulation float32 累积 >2²⁴ 格失真（4097² 漏斗出口报 16785408 而非 16785409） | terrain_flow.cpp + flow 产品 Float64 |
| DEF-4 | rs:spectral_derivative 忽略 WAVELENGTH_UNITS（μm 轴读作 nm：一阶导数 1000×、二阶 10⁶× 失真） | rs_spectral_derivative_operator.cpp |
| DEF-5 | SID 完全不相交支撑返回 0.0（"完美匹配"假象；sidClassify 按 lowest-index 误分类） | spectral_classification.cpp |
| DEF-6 | rs:apply_mask 复制 WAVELENGTH 但丢 WAVELENGTH_UNITS（μm 轴下游被当 nm 读） | rs_apply_mask_operator.cpp |
| DEF-7 | rs:spectral_resample 源轴原始读取（μm 源 + nm 目标 → 全 NaN "成功"产物） | rs_spectral_resample_operator.cpp |

## 算子功能深化（默认行为不变）

| ID | 深化 |
|---|---|
| FEAT-1 | SAR 地形算子（flatten/correction）几何回退链 param > SICNU_SAR_* 输入元数据 > 默认+警告；来源 provenance；输出再声明（默认猜测不戳记）；垃圾元数据带来源 fail-closed |
| FEAT-2 | rs:qa_mask `cloud_confidence`（standard 默认=历史行为 / relaxed 去 SCL-8 中概率云与 Landsat dilated cloud）+ SCL 类直方图质量摘要 |
| FEAT-3 | rs:post_classification_change 类别面积表（fromAreasKm2/toAreasKm2/changedAreaKm2 + cellAreaM2/基准命名，地理 CRS 走 #612 弧长纪律） |

## 测试
- 8 个新测试二进制 `tests/test_r25_a_*.cpp`（含光学链 E2E：qa_mask→apply_mask→NDVI NoData 传播；性能基线 report-only 车道）
- 数值 oracle：手算小格网（日长公式/线性坡/平地 θi=θ0）、独立慢算法（漏斗逐格组合计数）、metamorphic（≤2²⁴ float 契约、#783 哨兵、部分重叠 SID、显式参数优先）、生产 E2E（全经 RSOperatorRegistry）

## 验证证据
<构建/ctest 命令与 exit code、RED/GREEN×2、邻域回归、性能中位数与 VmHWM —— 实测后回填>

## 评审
- R1（独立子代理）: `<回填>`
- R2: `<回填>`

## 跨轨风险与未执行项
- 见 CROSS_TRACK_FINDINGS.md / RISK_REGISTER.md
- 未执行：GPU/TensorRT/在线 STAC/macOS/Windows 平台测试 NOT_EXECUTED（无资源）
