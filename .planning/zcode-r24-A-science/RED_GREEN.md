# R24-A RED→GREEN Evidence

基线：origin/master@5fe642ec6339807823aa56612ba94668592a738d（未修代码）
工作树：agent/r24_a-20261027T022732Z
构建：MSVC 2022 / Ninja / Debug / -j2，build-r24_a
运行：`.planning/zcode-r24-A-science/run_tests_r24a.cmd direct <test>.exe`（QT_QPA_PLATFORM=offscreen，PROJ_LIB 指向 vcpkg_installed share/proj）

## RED（修复前，未修源码；日志 red_test_r24_a_*.log）

| 测试 | 结果 | 失败断言（要点） |
|---|---|---|
| test_r24_a_nodata_semantics | 3/5 用例失败 | :87/:88 bothNormalizable(-0.01,-0.01)==false；:102/:105 等值负对 ND 输出 NaN；:160 rs:ndvi 生产路径负等值对 → NaN |
| test_r24_a_temporal_pins | 全过（金值 pin，设计即绿） | whittaker n==3/4 手算金值 (20/61,21/61,20/61)、(10/21,11/21,…) |
| test_r24_a_terrain_viewshed | 4/4 用例失败 | 亚米格 9/441 可见（假不可见楔形）；度格网全幅假不可见；度格网墙后该挡不挡处反向错误 |
| test_r24_a_terrain_flow_flats | 2/2 用例失败 | 火山口溢出口 acc≈1（期望 ≥50）；碗内 49 格全 dir=0 汇 |
| test_r24_a_temporal_guards | 1/1 失败 | 重复时间戳未被拒绝（9 景版本前为 3 景触发更早守卫，修正后 RED 落在"未抛类型化错误"） |
| test_r24_a_sar_incidence | 2/2 失败 | flatten/correction incidenceDeg∈{0,−30,90,95,700} 未抛 RSOperatorError（35° 对照组通过 → fixture 有效） |
| test_r24_a_help_drift | 全过（守卫，设计即绿） | 93 键 ↔ 三提供者注册名一致 |
| test_r24_a_e2e_temporal | 全过（E2E 基线契约） | 70 断言：波段/日期布局、CRS/geotransform/nodata、两轮逐字节一致 |
| test_r24_a_perf_viewshed | 全过（计时打印） | BEFORE：viewshedR3 1024² median **283.4 ms**（3.70 Mpix/s）；D8 plain 2992.7 ms / +resolveFlats 6204.8 ms（+107%） |

## 修复（6 项，全部最小外科手术）

1. **#1687**（math_utils.h/.cpp + spectral_indices.cpp）：移除 `a==b&&a<0` 等值负对拒绝（(a−b)/(a+b)=0/(2a)=0 有定义）；`kUndeclaredSentinelCeiling` 命名常量单一所有者（spectral_indices isSentinelSample 收编）；新增哨兵感知重载 `bothNormalizable(a,b,ndA,ndB)` / `normalizedDifference(…,ndA,ndB)`（精确匹配声明哨兵，非有限哨兵禁用该侧）；AccumulatorStats validCount 契约文档化。
2. **viewshed 环归一化**（terrain_viewshed.cpp）：环号从 ceil(地图距离) 改为 ceil(d/minCell)（cell 单位）；修复亚米格假不可见楔形与度格网全幅假不可见。桶内精确排序（首版 +43~54% 开销）被证明冗余后移除——前驱不变量（对角内邻对 a,b≥1 格必降环、轴上格由轴向内邻覆盖；R1/R2 评审独立证明）保证 counting sort 语义，性能回到基线之下。
3. **D8 平地解析门控接入**（rs_terrain_flow_operator.cpp）：D8 产品链在 fill 后探测平地（O(8n) 扫描），有平地才跑 resolveFlats（避免无平地场景 +81~107% 的稳定开销）；火口汇水面积正确路由到溢出口。
4. **phenology_multi 重复时间戳守卫**（rs_temporal_phenology_multi_operator.cpp）：镜像 decompose/smooth 的类型化拒绝（"strictly increasing … duplicate_policy=reject … rs:temporal_regularize"），输出创建前失败。
5. **SAR 角域校验**（rs_sar_terrain_flatten/correction_operator.cpp）：incidenceDeg∈(0,90) 区间拒绝，消息与 masks/backscatter 同契约；修掉 95° 伪 RTC 与 0° 全幅 NoData"成功"。
6. **help 目录防腐烂**（algorithm_help_catalog.h/.cpp + 测试）：新增 catalogKeys() 访问器；drift 测试断言全部目录键 ↔ 注册算法名一致（#1646-5 后半）。

## GREEN×2（修复后同命令两轮；日志 green1_/green2_test_r24_a_*.log）

| 测试 | 第 1 轮 | 第 2 轮 |
|---|---|---|
| nodata_semantics | All passed (42 assertions / 6 cases) | All passed |
| temporal_pins | All passed (14 / 3) | All passed |
| terrain_viewshed | All passed (25 / 4) | All passed |
| terrain_flow_flats | All passed (16 / 2) | All passed |
| temporal_guards | All passed (15 / 1) | All passed |
| sar_incidence | All passed (22 / 2) | All passed |
| help_drift | All passed (6 / 2) | All passed |
| e2e_temporal | All passed (70 / 1) | All passed |
| perf_viewshed | All passed（计时见 PERF_BASELINE） | All passed |

邻域回归（既有套件）见 TEST_RESULTS.md。
