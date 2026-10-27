# r23-A TEST_RESULTS(执行中填充)

格式:日期 | 命令 | 退出码 | 结果摘要

## RED 批次(master@ad98a7a704 源码 + 新测试,2026-10-26/27)

- T1: `./build-r23-A/tests/test_math_utils "*1687*"` → 2 test cases / 22 assertions:19 passed 3 failed
  - 失败断言与反例一一对应:
    1. normalizedDifference(-0.01,-0.01)→NaN(期望 0):REQUIRE(std::isfinite(out[0])) false, "index 0 value nan"
    2. normalizedDifference(-1500,-500)→NaN(期望 0.5):nanf == Approx( 0.5 )
    3. bothNormalizable(-0.01f,-0.01f)→false
  - 既有 SECTION(基础/零分母/空指针/零计数)全部通过(新旧测试共存,只有新契约失败)

## RED 批次(master@ad98a7a704 源码 + 新测试,10/10 恰好新用例失败,既有用例全绿)

| 二进制 | 结果 | 关键失败证据 |
|---|---|---|
| test_terrain_viewshed | 1457 断言:1455过/2败 | 亚米平面可见数 75 == 441(假暗区) |
| test_terrain_foundation5 | 271:270/1 | saddle 中心 profile 2.0f == Approx(1.0)(模板值×2) |
| test_spectral_roi | 42:41/1 | µm 波长 0.49000001f == Approx(490.0) |
| test_terrain_solar | 80:78/2 | 极点方位 171.19≠180、8.81≠0(噪声支配,非物理极限) |
| test_temporal_fit | 192:191/1 | **SIGABRT**(harmonics=7 越界写触发 stack-protector) |
| test_mnf_transform | 1883:1881/2 | 空选择 RMSE 0.1174 ≠ 0(且≠全枚举 0) |
| test_spectral_unmixing_fcls | 99:98/1 | 1e-7 尺度单纯形 VCA 拒绝 |
| test_spectral_table | 63:62/1 | 宽度不一致表 digest 非空(OOB 读出哈希) |
| test_image_enhancement | 6235:6234/1 | tile Inf→255 ≠ frame nodata |
| test_math_utils | 22:19/3 | 负反射率 NaN/魔数拒绝(前述) |

注:terrain_solar 实测推翻审计报告的"NaN"预测——lat=90 时 cos(lat)=6.1e-17≠0,主代码给出噪声支配的有限方位(不可复现);缺陷同真,修复用 |cosLat|<1e-12 阈值走解析极区极限(∓cosH)。

## GREEN 批次(HEAD 修复后)

### GREEN1(2026-10-27,10 二进制全部 exit=0)
| 二进制 | 断言 | 用例 |
|---|---|---|
| test_math_utils | 109 | 8 |
| test_terrain_viewshed | 1457 | 8 |
| test_terrain_foundation5 | 277 | 14 |
| test_spectral_roi | 43 | 3 |
| test_terrain_solar | 80 | 6 |
| test_temporal_fit | 212 | 27 |
| test_mnf_transform | 1883 | 8 |
| test_spectral_unmixing_fcls | 100 | 8 |
| test_spectral_table | 64 | 10 |
| test_image_enhancement | 6247 | 16 |

### GREEN2(同命令重跑)
全部 exit=0,断言数与 GREEN1 逐一相同(见上表;两轮日志 /tmp/r23a_g1_*.log、/tmp/r23a_g2_*.log)。

### 开发中发现并修正的初版修复缺陷(viewshed)
初版 ring 量化取 min(csx,csy):0.25/0.8 各向异性通过、但 0.3/0.3 仍有 12 暗元——轴向格距恰为量子的整数倍时,d/量子 的浮点商在精确整数边界上舍入上移,相邻轴向格同环。独立复现程序(dbg_viewshed.cpp,g++ 直接编译 terrain_viewshed.cpp)确认后改用 m/2 量子(商差 ≥2−ε≥1,双舍入下仍严格递减),并对 10 组对抗格网(0.25/0.25、0.5/0.5、1.5/1.5、0.125/3.0 等精确倍数与混合尺度)全部 0 暗元。教训入 LOOP_LEDGER。

## 邻域回归(M3 冲突发现与解决)
- 13 个同域族:12 绿 + test_image_fusion 失败(4 断言)——r21-A R2/M3 回归钉住旧 bothNormalizable 哨兵规则,与 #1687 契约变化真实冲突。
- 解决:新增具名缝 MathUtils::legacyNegativeSentinelFree(≤−999 legacy 未声明哨兵筛,不再拒等值负对),dNBR 融合分支改用;M3 测试重钉新缝+共享内核有限判据。
- 复跑:test_image_fusion 1727 断言绿、test_math_utils 109 绿、test_rs_operators 9114 绿、test_e2e_phase2 4107 绿、test_change_detection 888684 绿、test_spectral_formula_drift 145 绿。
- 其余邻域:test_terrain_analytics_e2e 1088、test_mnf 22、test_spectral_anomaly 90243、test_endmember_extraction 72、test_processing_integration 29、test_temporal_selection 118、test_temporal_uncertainty 75、test_band_math 130277、test_mosaic 38 —— 全绿。
