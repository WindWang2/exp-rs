# R24-A Test Results

环境：Windows 10.0.26200 / MSVC 2022 (14.38) / Ninja / Debug / -j2 / build-r24_a
运行器：`.planning/zcode-r24-A-science/run_tests_r24a.cmd direct <test>.exe`（QT_QPA_PLATFORM=offscreen；PROJ_LIB=build-r24_a/vcpkg_installed/x64-windows/share/proj；退出码以 Catch2 汇总行为准，日志为证据）

## R24-A 新增测试（GREEN×2，两轮同命令）

| 二进制 | 结果（两轮一致） |
|---|---|
| test_r24_a_nodata_semantics | All tests passed — 42 assertions / 6 cases（#1687 内核 + 哨兵重载 + rs:ndvi 生产路径负等值对） |
| test_r24_a_temporal_pins | All tests passed — 14 / 3（whittaker n==3/4 金值 pin + 退化 n<3） |
| test_r24_a_terrain_viewshed | All tests passed — 25 / 4（亚米/度格网全可见 oracle + 度格网墙遮挡 + 单元格网回归锚） |
| test_r24_a_terrain_flow_flats | All tests passed — 16 / 2（火口 D8 溢出口 acc≥50 + 碗内零汇，算子级真实 GDAL E2E） |
| test_r24_a_temporal_guards | All tests passed — 15 / 1（重复时间戳类型化拒绝 + 无半成品输出） |
| test_r24_a_sar_incidence | All tests passed — 22 / 2（35° 对照成功；0/−30/90/95/700 拒绝；无半成品） |
| test_r24_a_help_drift | All tests passed — 6 / 2（93 目录键 ↔ 三提供者注册名；活键返回目录文案） |
| test_r24_a_e2e_temporal | All tests passed — 70 / 1（时序链 E2E：波段/日期布局、CRS/geotransform/NoData 保持、尖峰抑制、两轮逐字节一致） |
| test_r24_a_perf_viewshed | All tests passed — 47 / 2（性能证据靶，计时见 PERF_BASELINE.md，无墙钟门） |

## 邻域回归（既有套件，×2 轮）

| 二进制 | 结果（两轮一致） |
|---|---|
| test_terrain_viewshed | All tests passed（1453 assertions / 7 cases） |
| test_terrain_hydrology | All tests passed |
| test_terrain_foundation5 | All tests passed（含 rs:terrain_flow 既有 E2E） |
| test_terrain_analytics_e2e | All tests passed |
| test_adversarial_m1 | All tests passed（含 DEM NoData 填洼/流向对抗） |
| test_sar_operators | All tests passed（含 backscatter 120° 拒绝既有契约） |
| test_sar_radiometric_state | All tests passed |
| test_temporal_phenology_multi | All tests passed（既有 8 用例内核路径不受新前置守卫影响） |
| test_temporal_fit | All tests passed |
| test_whittaker_smooth | All tests passed（d16 孪生实现） |
| test_temporal_operators_10 | All tests passed（含 regularize/decompose/smooth 守卫族） |
| test_change_detection | All tests passed（dNBR 共享核语义） |
| test_chunked_operators_parity | All tests passed（流式/全幅 parity） |
| test_algorithm_meta_drift | All tests passed（sidecar 57 目录） |
| test_rs_operators | All tests passed（注册清单） |

## 未执行（NOT_EXECUTED，含理由）

- `test_d16_temporal_phenology_e2e`：已知环境性失败面 CT-R21-A-1（src/core/temporal_cube.cpp sceneInstant 对完整路径扫 `\d{8}`；本 worktree 路径含 `20261027`）。与本轨改动无交集（本轨未触 src/core 与 d16 代码）；在无 8 位数字路径下该套件在 r21-A/r23 已验证。已记 CROSS_TRACK_FINDINGS CT-R24A-7。
- 全仓 ctest 广谱运行（~1081 注册测试）：本轨按"增量 target → 邻域 ctest"纪律执行上述 15 靶 + 9 新靶；未跑平台为 macOS/Linux（本机为 Windows，交叉平台未验证——诚实声明）。
- 光谱/分类/镶嵌全家族深扫审计：第 3 个审计代理因账户限流（1302）失败，NOT_EXECUTED；#1646-6/#1687 相关光谱面已人工实读覆盖。
