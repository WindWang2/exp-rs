# hardening(r21-A): 遥感科学算法正确性收敛 — 17 项缺陷修复 + dNBR 单遍融合（双轮独立评审）

## Base / Head

- base: `master` @ `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（执行时刻远程事实基线；Open PR=0，Open Issues=126 已全量再分诊）
- head: `agent/r21-a-scientific-algorithms-20261023T184835Z` @ `51375e6fa5f531ac61a70eb5dc6bfcfe01738efe`
- 读取的 PR/issues：#1671（本轨前作，七契约全部 merge-loss 复核在位）、#1663/#1534、#1550/#1538/#1590/#1591/#1631/#1646-6（R20 判"已修未关"，本轮逐条实读确认，不重复修）、#1467（跨轨）、#1541/#1544/#1573-1577/#1642/#1646 其余项/#1558（跨轨移交）

## 工作包（19 commits，全部 RED→GREEN×2）

### 科学正确性（生产路径实错）
1. **rs:atmospheric_dos2 无效 airmass 静默全 NaN 成功**（AG1-1, P2）：airmass≤0/非有限 → T=0 → 全部有效像素 NaN，run 返回 success 且盖 surface_reflectance 章。operator typed refusal + 两处流式内核门镜像内存 dos2() 纪律。
2. **rs:kmeans 忽略 scale=false**（AG5-1, P2）：flag 在 scaler fit+transform 之后才读取，scale=false 与 true 输出逐字节相同；schema 承诺可关。先读 flag 门控 fit。
3. **分类校准门只查列数不查类 id**（AG5-2, P2）：calibration {1,2,4} 配 RF 训练 {1,2,3} → 第 3 列概率/不确定性/meanConfidence 全静默错值。改比 backend classOrder 身份，fail-closed。
4. **SAR DEM 单位声明被 3 个算子忽略**（AG3-1, P2）：SICNU_DEM_UNIT 只被 flatten 家族尊重；rs:sar_geocode / terrain_mask / remove_topographic_phase 读原始高度，feet/decimeter DEM 的零多普勒地理编码/地形相位位移 3.28×/10×。新增 sar::demUnitScaleToMetres 并接入三算子。
5. **continuumRemoval 零包络返回假 1.0 数据**（AG2-F2, P3→生产）：0/0 分支曾写 1.0（"在包络上"），黑目标被发明数据；改发 nodata，与姊妹核一致。
6. **OLS unmix 零和丰度静默输出**（AG2-F3, P3→生产）：LS 解全负 → clip 后 sum=0 → 全 0 丰度违反 sum-to-one 契约（rs:spectral_unmixing 默认路径）；改发 NaN 并写入头文件契约。
7. **OBIA kmeans 重映射泄漏 raw cluster id**（AG5-3, P3）：无训练段的簇保留 raw id（超出标签空间）；镜像像素管线的 0 哨兵。
8. **regularizeSeries WindowMean 只平均 2 个括号观测**（AG4-1, P2）：且不对称（等距丢左保右）。oracle：obs{0,8,16,24,32} vals{1,1,1,5,5} t=16 r=16 → 实测 3.0/2obs，期望 2.6/5obs。
9. **monitor 对零方差像素喂 z=0 伪造 CUSUM**（AG4-2, P3）：恒定像素 drift=0.5×60 景 → S=−30 假警报；sd==0 跳过（temporal_monitoring.h 有限 z 契约）。
10. **terrain_solar 拒绝 DOY 366**（AG4-3, P3）：闰年 12/31（QDate::dayOfYear 产生 366）硬失败；接受 1..366 + Spencer 级数 clamp。
11. **fast_6S Rayleigh 相函数系数**（AG1-2, P3）：1+δ 应为 1+3δ（Hansen & Travis 1974）；旧式积分 0.961，90° 散射路径反射率低 ~5%。
12. **DN→TOA / DOS2 的 NaN/Inf 门洞**（AG1-3, P3）：`sinElevation<=0` 放行 NaN（返回 true 全 NaN，违反 fail-closed 契约）、`esun>0` 放行 +Inf（全 0 场景）。
13. **radiometric bands 非整数项静默回退全波段**（AG1-4, P3）：bands=[2.5] 丢弃后空=全波段；改 InvalidParameter。
14. **RF getVotes row-0 身份校验缺失**（AG5-5, P3）：注释承诺、只比 count；实现逐列 id 校验，失配拒绝。
15. **SAR terrain_flatten legacy look_azimuth NaN**（AG3-4, P3）：主别名有 isfinite、legacy 无 → NaN 方位产 NoData gamma0 但 mask=1"valid"；补齐。
16. **continuum 半深交叉外推出谱外**（AG2-F4, P3）：截断翼（带隙/扫描窗）线性外推出 run 边界（65/200nm on 100..120nm 网格）；clamp 到 run 边。
17. **#1591 RsSegmentMap 缓存竞态**（P2）：mutable 缓存在 const 方法无同步写入，RsObjectHierarchy::level() 交出内部 const 引用（QGIS 渲染线程可达）。QReadWriteLock 双检 + 显式拷贝/移动（只搬 label image）+ int64 计数（原 int 计数 >2^31 溢出）。

### 契约对齐 / 诊断增强
18. **gamma0 地形扁平化公式三处矛盾**（AG3-2, P2）：生产实现 sinθi/sinθ0（#1146 正确），但 sar_terrain.h、两个算子 purpose/preflight 文案、exposed terrainRadiometricFactor 内核仍写并实现被 #1146 判为有偏差的 cosθ0/cosθi（前坡差至 ~4×）。全部对齐 sin 形式 + 前坡 oracle。
19. **flattenRamp wrap 域可靠性诊断**（AG3-3, P3）：拟合 wrap 相位，多条纹 flat-earth 斜坡静默不移除（slope≈0 成功返回）。schema 记前置条件 + 圆合成长度诊断 + warning + SICNU_SAR_RAMP_RELIABILITY=low 元数据。
20. **内存核 seam 与流式路径对齐**（AG2-D1/D2/F1/F5/F6）：pcaFusion/pca/mnf 均值逐 band vs 协方差 listwise 偏差、五内核 ±Inf 只测 isnan、histogramMatch 总体 vs 样本标准差。全部对齐流式纪律（生产流式路径本就正确，seam 为 tests/panel）。
21. **RsClassRaster::paint 接受 class id 0**（AG5-3 下游）：头文件"ids>=1"与"0 写 NoData"及 OBIA"缺席段画 0"自相矛盾；对齐为允许 0、拒绝负值。

### 性能（数值等价）
22. **dNBR 单遍融合**（perf）：原每 block 两次 normalizedDifference 写堆缓冲再读回（2 次 O(n) 往返 + 2 次分配/block）；内联合遍。等价性由独立文献公式 drift 测试 + change_detection 888,684 assertions 钉死。

### 驳回（诚实记录）
- AG5-4 mosaic_balancing"累计增益门"：主代理归纳复核确认 perBand 本就是 BFS 累计映射（ref=identity，子场景回归父的累计校正值），门检查的已是累计增益 → **误报，无代码变更**。

## 回归/基准

- 构建：Debug + tests ON，100%（EXIT=0）。
- O-A1（R20 #1671 七契约族）：两遍全绿。
- 本轨新增/受影响 13 族 + sar/terrain/temporal/mosaic/分类/谱学/辐射/e2e ~156 族底网：两遍全绿（证据见 .planning/r21-A/TEST_RESULTS.md）。
- 环境说明：本机无 ctest，测试经 .planning/r21-A/run-tests.sh（vendor GDAL 依赖 shim + PROJ_LIB + wrapper2 stub）逐二进制串行执行。
- 已知环境性失败（非本轨回归，已实证）：test_d16_temporal_phenology_e2e 在含 8 位数字的路径下失败——`src/core/temporal_cube.cpp` sceneInstant 的 parseIsoDate 在完整路径上扫描 \d{8}，本轨 worktree 名 `...20261023T184835Z` 的 `20261023` 被当作采集日期；同一代码在无数字路径下 675 断言全绿。已登记 CROSS_TRACK_FINDINGS CT-R21-A-1（src/core 属 02 轨）。

## 评审

R1：见 .planning/r21-A/REVIEW_R1.md（fresh reviewer，独立上下文，含其复跑测试）
R2：见 .planning/r21-A/REVIEW_R2.md（R1 修复后二次独立评审 + 对抗性边界）

## 剩余风险与未执行项

- Tsan 不可用（preset 无 TSan + 本机 Qt/GEOS 双版本环境复杂）；#1591 以线程 hammer + 结果一致性断言验证。
- Win/mac 不可用（单 Linux 主机）。
- fast_6s 水汽 0.5 不对称系数（R20 记档）无权威 RT oracle，本轮不改物理常数（NOT-FIXABLE-WITHOUT-ORACLE）。
- rs_simple_segmenter 31-bit 哈希碰撞（R20 记档）：确定性碰撞、影响需统计量化，未触碰。
- 全量 ctest 套件（~450 目标）未跑（ctest 不可用 + 全量串行耗时数小时）；以目标域 ~156 族 + 构建 100% 替代。

## 文件所有权

100% 在白名单内：src/analysis/**、src/processing/algorithms/**、src/operators/rs/**、tests/test_*、.planning/r21-A/**（markdown + run-tests.sh）、.gitignore 一段 append-only 白名单。#1671 既有 merged patch 语义经逐文件 diff 核对，无静默回滚。
