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
