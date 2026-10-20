# R19-01 TEST_RESULTS

## 环境记录（实测）
- 主机: cachyos-x8664, Linux 7.2.9, 40 核 / 62 GB（构建按仓库规则全局 -j2）
- 编译器: GNU 16.2.1；构建: CMake+Ninja, Release, ENABLE_TESTS=ON, QT_QPA_PLATFORM=offscreen
- 依赖: Qt6 6.12.0 / PROJ 9.7.1(vendor, 需 PROJ_DATA 指向 vendor share/proj) / GEOS 3.14.1(vendor) / GDAL(vendor .so, 需 pw-seismic prefix 的 libarmadillo) / OpenCV 5.0.0(系统) / netCDF(vendor) / GSL 2.8(vendor) / jsoncpp(系统)
- 依赖钉库配方与排障全过程：pipeline1.sh/configure.sh 内固化（GSL/netCDF/SQLite3/EXPAT/ZLIB/ZSTD/PCRE2_8/QCA 显式路径 + rpath-link 顺序 `$PWLIB,/usr/lib,/usr/lib/pulseaudio,$L`）
- 运行直跑二进制必需环境：`LD_LIBRARY_PATH=/usr/lib:/usr/lib/pulseaudio:$L:$PWLIB` + `PROJ_DATA=$V/share/proj`（harness 等效；顺序敏感，$L 抢先会触发 Debian/Arch libpulsecommon/libblas 混链 symbol error）
- 宿主 cmake 无 ctest 二进制 → run_tests.sh/domain_sweep.sh 直跑 Catch2 二进制（绝对路径 + spec 过滤 + 全局锁 + 每二进制 600s timeout 上限）

## Oracle 结果（全部附退出码）
| Oracle | 命令 | RED(基线) | GREEN-1 | GREEN-2 |
|---|---|---|---|---|
| O-A1 seed1 #1548 | test_io_remote_validator "*session identity resolver*" | 基线即绿: 2 assertions, rc=0（客观纠偏：#1662 报告的"仍红"不可复现）| — | sweep 全绿 |
| O-A1 seed2 #1456 | test_io_fabric_multidim_11 "*correct cols*" | 基线即绿: 13 assertions, rc=0（源注释言明红在 Windows lane）| — | sweep 全绿 |
| T1.1 #1637-2 | test_io_paths "*two-segment extensions*" | **rc=42**, 3 断言失败 | 10/10 | 10/10 |
| T1.2 #1578 | test_rs_operator "*schema-declared numeric ranges*" | **rc=42**（radius=0 静默接受）| 6/6 | 6/6 |
| T1.3 #1637-3 | test_io_products "*oversized CN metadata*" | **rc=42**（5MiB 被整读）| 6/6 | 6/6 |
| O-A2 负向矩阵 pass1 | domain_sweep（remote 家族 5 二进制）| — | 全绿 | — |
| O-A2 负向矩阵 pass2 | 直跑 5 二进制 | — | — | 全绿（232/25/314/211/27 断言）|

## 域全量 sweep（85 目标，PROJ_DATA 修正后）
- **83 PASS**（含 test_chunked_fabric_e2e 修复后 606,370 断言全绿；test_rs_operator 831 断言全体注册 operator 在生产门下零回退）
- 1 NOT_EXECUTED：test_model_runtime_bench — 头文件明示 env-gated（SICNU_MODEL_BENCH=1），门关时静默跳过（"No tests ran" rc=2 为其设计行为），不谎报通过
- 1 由 sweep 发现并修复的预存红：test_chunked_fabric_e2e 2 断言（Advisory 枚举漂移 + F-A-13 记账下的 10MiB 预算漂移）→ 501bcaedd 修复后全绿
- 首轮 sweep 22 失败全数归因为缺 PROJ_DATA 的直跑环境问题（补环境后 21 个立即全绿 + 1 个即上述预存红），非代码回归

## 评审驱动的复测（bfbd8d768 后）
- 叶库重构后三 GREEN 目标重验：test_io_paths / test_io_products / tests/test_rs_operator 全绿（sweep + pass2）
- 生产门接线后 adapter/job_engine 所属测试：test_atomic_algorithm_adapter / test_job_engine 均在 sweep PASS 列

## 性能
- 本轨改动均为元数据解析/参数校验/有界读路径，不触及像素循环；无 A/B 基准需求（N/A，PR 声明）
