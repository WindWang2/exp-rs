# R24-A Cross-Track Findings（跨轨问题移交 + 审计处置记录）

## 移交 B 轨（data/model runtime / processing framework）

- **CT-R24A-1** `src/processing/framework/algorithm_preflight.cpp:146,284` 与 `agent_workflow_executor.cpp:138` 直接 GDALAllRegister()/GDALOpen() 绕过 ensureGdalInit() 一次性初始化约定（#1646-1）。复现：`grep -n "GDALAllRegister" src/processing/framework/*.cpp`。影响：JobEngine runner 线程上 std::call_once 之外重复注册。
- **CT-R24A-2** `src/processing/framework/local_worker_pool.cpp:336-337` releaseWorker 无锁读 m_running/maxJobsPerWorker（#1646-2，TSan 可检出的良性竞争但无注释）。
- **CT-R24A-3** `src/processing/gdal/gdal_dataset_wrapper.cpp:68-82` 移动构造/赋值不转移 m_lastError——open() 失败诊断在移动后静默丢失（#1646-3）。影响所有把 wrapper 移入容器的调用方。
- **CT-R24A-4** `src/processing/framework/json_params_converter.h:19-20,60-61` 重复前向声明（#1646-4，纯卫生）。

## 移交 C 轨（agent/app/Pi）

- **CT-R24A-5** `src/agent/spatial_tools/temporal_spatial_tools.cpp:322` `temporal:anomaly_alert` 气候基期零方差月份喂 z=0 伪造"无异常"（同族缺陷 r22 已在 rs:temporal_monitor 修掉：rs_temporal_monitor_operator.cpp:299-307 有修复注释与守卫）。复现：两年 7 月 NDVI 均 0.70、目标年 0.10 → z=0 不告警。建议镜像 monitor 的零方差纪律（跳过或结构化拒绝）。
- **CT-R24A-6** `src/agent/spatial_tools/terrain_spatial_tools.cpp:353-361` viewshed agent 工具把度单位 cellSize 原样传入内核（本轨修内核后该工具自动受益，但工具层对地理 CRS 无提示——C 轨可补 warning）。
- **CT-R24A-7**（继承）CT-R21-A-1：`src/core/temporal_cube.cpp` sceneInstant 在完整路径上扫描 `\d{8}`，含 8 位数字的工作目录使 test_d16_temporal_phenology_e2e 误判采集日期。本 worktree 路径含 `20261027`，该环境因素在本轨测试时同样存在（不算回归）。

## 本轨审计处置记录（不修的也要留痕）

| 审计发现 | 处置 | 理由 |
|---|---|---|
| whittakerSmooth n==3 "对角元 5λ"（时序审计#2） | **驳回（误报）** | 主代理手算复核：n==3 时 `main[n-2]` 即 `main[1]`，双次 −1λ 恰得正确 4λ；y=(0,1,0),λ=10 代码矩阵解 = 理论解 (20/61,21/61,20/61)。已加金值 pin 测试防未来误修（test_r24_a_temporal_pins.cpp）。 |
| SAR 地形算子忽略已声明 SICNU_SAR_INCIDENCE_DEG/HEADING_DEG（SAR 审计#3） | 本轨记录为后续候选，本轮不实施 | 参数回退链（param > 元数据 > 默认+warning）是契约扩展，默认值互相矛盾（30/35）需产品决策；独立可测但超出本轮最小闭环。留待下一轨或 issue 化。 |
| rs:terrain_solar 纬度静默默认 40°N（SAR 审计#5） | 同上，记录不实施 | 投影 CRS 下纬度不可从 geotransform 直接得，需 CRS 变换或限制地理 CRS；契约设计问题。 |
| piecewiseLinearTrend 单观测段 slope=0（时序审计#4） | 记录不实施 | 存在辩护空间（与 slopeStdErrors NaN 约定不一致是真），但影响面小且改动涉内核语义；不凑数。 |
| detectHarmonicBreaks splitRss==0 → F=0（时序审计#5） | 记录不实施 | 内核未接线任何 operator（grep 证实仅 tests 引用），不可达生产路径。 |
| D8/D∞ float32 累积精度 >2^24 格（SAR 审计#6） | 记录不实施 | 需 ≥2²⁴ 汇水格才显效；当前守卫预算 2^28 下的确存在，但重构成 double/int64 累加是独立性能-精度权衡，宜单独工作包。 |
| 光谱/分类/镶嵌家族深度审计 | NOT_EXECUTED | 第 3 个 Explore 代理因账户限流失败（错误码 1302）。本轨已覆盖 spectral 域的 #1687（共享核）与 #1646-5/#1646-6（实读确认已修）；未做全家族扫描，诚实记录。 |
