# FINAL_REPORT — r21-A（遥感科学算法轨）

**PR：https://github.com/WindWang2/exp-rs/pull/1676**（OPEN，base=master @ 911e0b17，head=51375e6fa5，
+2479/−191，74 文件，MERGEABLE；未等 CI、未启用 auto-merge、未替用户合并）

## 交付概览

- 24 个原子提交（17 项科学正确性缺陷修复 + 1 项契约对齐 + 1 项诊断增强 + 1 项 seam 对齐批次 +
  1 项性能改进 + 评审修复 ×2 + 规划文档 ×3）。
- 域：辐射/大气、指数/谱学/高光谱、SAR/InSAR、地形水文、分类/OBIA、时序、多源融合、性能。
- 5 个只读勘查代理（辐射/谱学/SAR/地形时序/分类）+ 2 个独立评审代理（R1/R2，均 fresh 上下文）。

## Oracle 核对（任务 §7）

| Oracle | 状态 | 证据 |
|---|---|---|
| O-A1 #1671 七族契约两遍绿 + 负向不 silent success | ✅ | TEST_RESULTS.md；七族两遍；负向用例在 test_sar_kernels/test_atmospheric 等 |
| O-A2 每项改动有可人工核算 oracle + 跨 block 一致 + typed diagnostic | ✅ | RED_GREEN.md 逐项；window_mean 2.6、sin15/sin35=0.451237、DOS2 0.35421（gdb 实测）、pca 变形 oracle 等 |
| O-A3 NoData/NaN/±Inf/无效 geotransform/时间重复乱序/SAR dB-linear | ✅ | 重复时间戳回归（R2-1 后补）、±Inf 门、DOS2 域、pcaFusion 边界探针（R2 28 项 RsSegmentMap + 29 项内核探针） |
| O-A4 性能同条件记录 + 无精度退化 | ✅ | PERF_BASELINE.md：dNBR 单遍融合数值等价（drift 测试 + 888,684 assertions）；无退化项 |
| O-A5 3 条端到端工作流 | ✅ | d13 辐射→谱学 e2e（67/4）、d15 分类→变化 e2e（51/4）、d16 时序→趋势/物候 e2e + e2e_science_numerics（281/20）+ e2e_phase2（4107/49） |
| O-G1 新鲜盘点 + 126 issues 再分诊 + PR 冲突矩阵 | ✅ | BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX；提交前再盘点（兄弟 PR #1673/#1674/#1675 零冲突） |
| O-G2 每项修复 RED/GREEN 两遍 + 边界反例 + 生产路径证明 | ✅ | RED_GREEN.md（含 R2 变异审计：16/27 变异被定向测试抓住，存活的已补测试或记录） |
| O-G3 性能基准同条件 + 内存上界 | ✅ | PERF_BASELINE.md（dNBR 只删分配；候选未实施项如实记录） |
| O-G4 受影响测试两遍绿 + 两个独立 reviewer | ✅ | R1（fresh）+ R2（fresh，含变异审计与独立重推导） |
| O-G5 残余 skip/NOT_EXECUTED 诚实归因 | ✅ | 见下"未执行项" |
| O-G6 PR 文件 100% 白名单 + merged patch 不回滚 | ✅ | gh 核实 74 文件零越界；#1671 七契约文件 diff 核对未触碰 |
| O-G7 无 TODO/假实现/占位 mock/假 PASS | ✅ | R2 明确核查；另修复两处评审发现的"假绿"（陈旧二进制掩盖的 P1、私有成员导致的未编译用例） |

## 关键缺陷（按科学影响）

1. SAR DEM 单位声明被 3 个算子忽略 → feet/decimeter DEM 的零多普勒地理编码位移 3.28×/10×（P2，生产）。
2. rs:kmeans 忽略 scale=false → 与文档语义不符的聚类输出（P2，生产）。
3. 分类校准门只查列数 → 概率/不确定性/meanConfidence 全静默错值（P2，生产）。
4. DOS2 无效 airmass → 全场景 NaN 仍返回 success 并盖 surface_reflectance 章（P2，生产）。
5. regularizeSeries WindowMean 只平均 2 个括号观测且不对称（P2，生产 rs:temporal_regularize）。
6. gamma0 公式三处矛盾（生产 vs 头文件 vs exposed 内核，前坡差 ~4×）→ 全部对齐 #1146 sin 形式。
7. #1591 RsSegmentMap 缓存竞态（P2，QGIS 渲染线程可达）。
8. continuumRemoval 零包络发明 1.0 数据；OLS unmix 零和丰度违反 sum-to-one（生产）。
9. 其余：Rayleigh 1+3δ、NaN/Inf 门洞、DOY 366、bands 整数门、RF row-0、look_azimuth、半深外推、
   monitor 零方差 CUSUM、OBIA 重映射哨兵、paint 契约矛盾、dNBR 单遍融合。
10. 驳回 1 项（mosaic_balancing 累计增益门为误报，归纳复核证明门已查累计）。

## 未执行项（诚实归因）

- **全量 ctest 套件**（~450 目标）：本机无 ctest（cache 记录陈旧）+ 全量串行需数小时；
  以目标域 ~156 个测试族（sar/terrain/temporal/mosaic/分类/谱学/辐射/e2e）+ 构建 100% 替代。
- **Tsan**：preset 无 TSan 且本机 Qt/GEOS 双版本环境复杂；#1591 以线程 hammer + 一致性断言验证。
- **Win/mac**：单 Linux 主机。
- **fast_6s 水汽 0.5 系数**（R20 记档）：无权威 RT oracle，不改物理常数（NOT-FIXABLE-WITHOUT-ORACLE）。
- **rs_simple_segmenter 31-bit 哈希碰撞**（R20 记档）：需统计量化，未触碰。
- **test_d16_temporal_phenology_e2e 在本 worktree 路径下红**：根因为 src/core/temporal_cube.cpp
  sceneInstant 的 parseIsoDate 在完整路径扫描 \d{8}，worktree 名含 20261023 被误当采集日期；
  已在无数字路径实证同代码 675 断言全绿（非本轨回归），修法属 02 轨 → CROSS_TRACK CT-R21-A-1。
- **wall-clock 基准**：dNBR 改进的微基准未跑（benchmark 可执行不覆盖 dNBR；如实记录为未测量）。

## 跨轨移交

- CT-R21-A-1（→02 轨）：sceneInstant 路径日期误解析（含重现与建议修法）。
- CT-R21-A-2（知会）：paint 契约变更为"允许 0"。

## 环境记录（可复现）

- worktree：/home/kevin/project/exp-rs-r21-A-20261023T184835Z（分支 agent/r21-a-scientific-algorithms-20261023T184835Z）
- 构建：build-r21-A（Debug, tests ON）；依赖来自 paleo vendor prefix + 系统 Qt 6.12；
  链接需 wrapper2 stub（/tmp/r21a-deb）与 --allow-shlib-undefined。
- 测试运行器：.planning/r21-A/run-tests.sh（GDAL 依赖 SONAME shim /tmp/r21a-libs + PROJ_LIB + stub）。
- 教训（已固化）：增量构建会因 mtime 未变保留陈旧目标——源码改动后必须重链目标再跑（R2 P1 由此发现）。
