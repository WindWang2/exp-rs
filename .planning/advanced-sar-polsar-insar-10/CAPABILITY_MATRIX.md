# CAPABILITY MATRIX — SAR @ baseline `7d78059d1a`

## 已有能力（不重做）

| 能力 | 内核 authority | 算子 | 契约/测试 |
| --- | --- | --- | --- |
| DN→sigma0/gamma0/beta0 定标 | `sar_calibration.{h,cpp}` | `rs:sar_calibrate` | linear/dB 域（`sar_metadata.h`）；`test_sar_kernels`/`test_sar_operators` |
| 后向散射状态转换 | 同上 | `rs:sar_backscatter` | #938 preflight |
| 双极化特征 ratio/RVI-dual/span | `sar_dualpol.{h,cpp}` | `rs:sar_dualpol_features` | 诚实声明"非 quad-pol"（sar-domain.md §1） |
| Bi-temporal 比值/对数比值变化 | `sar_ratio.{h,cpp}` | `rs:sar_ratio`, `rs:sar_change` | #929 grid/dB preflight |
| 斑点滤波（Lee 等多滤波器） | `sar_speckle.{h,cpp}` | `rs:sar_speckle` | #803 逐波段 NoData、#330 |
| GLCM 纹理 | `sar_texture.{h,cpp}` | `rs:sar_texture` | `test_sar_kernels` |
| 常几何地形几何（局部入射角、layover/shadow） | `sar_terrain_geometry.{h,cpp}` | `rs:sar_terrain_masks` | #785 look-azimuth；closed-form tests |
| 平面拟合 RTC（gamma0_rtc） | `sar_terrain.{h,cpp}` | `rs:sar_terrain_flatten`, `rs:sar_terrain_correction` | #854/#855/#934 |
| 轨道状态向量解析 + Hermite 插值 + zero-Doppler 定位 + forward range-Doppler | `sar_orbit.{h,cpp}` | （内部） | `test_sar_orbit` 圆轨道已知答案 |
| 后向逐像素几何定位（orbit 契约） | `sar_geocoding.{h,cpp}` | `rs:sar_terrain_masks product=local_incidence_orbit` | sar-domain.md §3 |
| Forward range-Doppler geocoding + 真几何 gamma0（Ulander 面积因子） | `sar_geocoding.{h,cpp}` | `rs:sar_geocode` | `test_sar_geocoding`（round-trip < 1mm） |
| N 景时序统计（median 基线、log deviation） | `sar_temporal.{h,cpp}` | `rs:sar_temporal_stats` | `test_sar_temporal_stats`；S-1：argmax=场景索引 |

## 缺口矩阵（本 Track 交付）

| 缺口 | 目标能力 | 工作包 | 参考 |
| --- | --- | --- | --- |
| complex/SLC 数据路径 | CFloat32 通道契约、amplitude/phase、逐通道 NoData、bounded tile 流 | A | SAR-domain.md 追加 §6 |
| 极化分解（S-2） | T3/C3 ensemble、Pauli、H/A/α（Cloude-Pottier）、Freeman-Durden、Yamaguchi、eigen stability | B | Lee & Pottier；known-answer tests |
| InSAR 基础链 | 配对 preflight、干涉图+相干性、Goldstein 滤波、解缠（参考实现/typed provider）、LOS 形变、geocode 复用 | C | 合成 SLC 闭合测试 |
| 时间语义（S-1） | acquisition-date 契约、event dating、first/last change、trajectory、anomaly、缺失景/不规则间隔 | D | `rs:sar_temporal_stats` additive dateMap |
| 干涉基线几何 | B⊥/B∥ 从 state vectors | E（并入 sar_orbit append） | — |

## 去重排除表（发现于其他 track / 已修复，不重做）

| 项 | 状态 | 证据 |
| --- | --- | --- |
| pair grid/dB preflight | 已合并 PR #938 | issue #929 CLOSED |
| flatten 2-band contract + NoData | 已合并 | #934/#854 CLOSED |
| 各向异性 Horn 梯度 | 已合并 | #855 CLOSED |
| look azimuth 90° 修复 | 已合并 | #785 CLOSED |
| 多波段 speckle NoData | 已合并 | #803 CLOSED |
| Lee noiseVariance 语义 | 已合并 | #330 CLOSED |
| 产品格式导入（GF/ZY/HJ adapters） | PR #956（Track 02） | 不越界 |
| lab SAR 教学链 | PR #948 | 消费方，非本 track |
| capability knowledge 覆盖 111 算子 | PR #950 | 我的 sidecar 走同一 gen-meta 流程 |

## 在飞 Track 冲突排查

- 无 SAR 相关在飞分支（BASELINE.md）。
- `zcode/scientific-contract-verification-10` worktree 存在 @ 同 baseline——契约验证 track，
  若其触碰 `src/processing/algorithms/sar/` 将在 rebase 时以我方语义为准并记录 DECISIONS。
- `tests/CMakeLists.txt` / `rs_operators_init.cpp` / `src/operators/CMakeLists.txt` 为共享热点：
  全部走独立 append-only integration commit。
