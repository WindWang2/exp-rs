# R20-02 TEST_RESULTS

## 环境（实测）
- 主机: cachyos-x8664, Linux 6.x, 40 核 / 62 GB；本轨全程全局锁 `-j2`（flock `$GIT_COMMON_DIR/exp-rs-zcode-global-build.lock`）
- 编译器: GCC 16.2.1（构建期 ICE ×4 由 make 重试自愈；ld 段错误 ×1 重跑恢复——均为工具链瞬时故障，产物验证无恙）
- 构建: CMake 4.4.4 / Unix Makefiles / Debug / ENABLE_TESTS=ON / ENABLE_LOCAL_BUILD_SHORTCUTS=ON；系统 GDAL/PROJ/GEOS（/usr/lib/cmake/*，与主仓 build-dev 同配方）
- 测试: QT_QPA_PLATFORM=offscreen 直跑 Catch2 二进制（本机有 ctest 亦可）

## RED 证据（pristine master 28e1891390 引擎 + 新测试；2026-10-23 04:1x-04:2x 实跑）
| 测试二进制 | 命令 | 结果 | 失败明细 |
|---|---|---|---|
| test_model_runtime | `QT_QPA_PLATFORM=offscreen ./test_model_runtime`（600s timeout） | 24 用例 3 failed / **100,410 断言 100,407 过** | F1: `InvalidInputData: mixed tile sizes in one inference batch (36x64 vs 64x64)`；F2: `CHECK( mismatchedConfidence == 0 )` 扩张值 `2048 == 0`；F4: `REQUIRE_THROWS_AS ... because no exception was thrown` |
| test_model_catalog_v2 | 同上 | 24 用例 1 failed / 436 断言 433 过 | F3×3: bad-interpolation / bad-interpolation-case / bad-resize-token `readiness == Ready(1) != InvalidManifest(2)` |
| test_workspace_services | 同上 | 17 用例 1 failed / 212 断言 209 过 | F5×3: ok 仍 true、error 无 "governance"、snap-*.snapshot 目录未清理 |
| test_fault_injection | 同上 | 9 用例 1 failed / 157 断言 156 过 | F6: `REQUIRE( second )` — put 返回 nullopt |

结论：6/6 RED 全部命中预期根因；四个二进制合计 101,215 断言中仅 13 条失败且全部是本轨新种子 → **无预存红、无回归混杂**。

## GREEN（修复后；待最终增量构建完成后回填 ×2 遍）
（pending）

## 域回归 sweep
（pending）

## 合并存续验证（运行时部分）
（pending：io_paths/io_products/rs_operator/remote 家族/chunk 家族/model 8/9 家族复跑）
