# R18-A TEST_RESULTS — 本地验证记录（Oracle-B）

环境：worktree `/home/kevin/project/exp-rs-r18-a-science`，构建树 `build-r18-a`
（Release, ENABLE_TESTS=ON）。构建纪律：全局锁 `/tmp/exp-rs-five-tracks-build.lock`
+ `resource_guard.py --parallel 2`；`SICNU_BUILD_REQUIRE_GUARD=1 SICNU_BUILD_JOBS_CAP=2
CMAKE_BUILD_PARALLEL_LEVEL=2 CTEST_PARALLEL_LEVEL=1 QT_QPA_PLATFORM=offscreen`。

运行时环境（vendor GDAL 38 + 系统 Qt 6.12 混合，详见 LOOP_LEDGER L01 系列）：
```
V=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH=/tmp/r18a-rt
export LD_PRELOAD=/tmp/r18a-rt/libwrapper2stub.so
export PROJ_LIB=$V/share/proj PROJ_DATA=$V/share/proj GDAL_DATA=$V/share/gdal
```
（/tmp/r18a-rt = curated 符号链接目录：vendor 非 Qt 库 + 系统 Qt6 + 桩库；
缺失 PROJ_DATA 时 CRS 语义测试假失败——已实证并修复口径。）

## 构建记录

| 日期 | 命令 | 退出码 | 摘要 |
|---|---|---|---|
| 10-19 | `cmake -S . -B build-r18-a ...`（默认依赖发现） | 1 | `Could not find GDAL`。教训：管道 tail 掩盖退出码。 |
| 10-19 | + vendor prefix 全套（自 R18-C 缓存） | 1 | FetchContent Catch2 克隆失败（git 本地代理不可达）→ 改用本地副本 `/tmp/r18a-fc/catch2-src`（v3.7.1=GIT_TAG）。 |
| 10-19 | + 本地 Catch2 | 0（配置） | qgis_core 链接失败：vendor libexpat.a/libsqlite3.a 非 PIC。 |
| 10-19 | EXPAT/SQLite3→vendor .so | 2 | vendor .so 是悬空符号链接（只有 .a）→ 改系统 /usr/lib .so。 |
| 10-19 | EXPAT/SQLite3→系统 .so | 2 | libz.a/libzstd.a（CMAKE_PREFIX_PATH 泄入）非 PIC → 显式系统 .so。 |
| 10-19 | +ZLIB/zstd→系统 .so | 2 | 测试可执行链接失败：系统 libcblas 缺 *_64_ 符号、vendor libgdal 需 26 个 wrapper2_*、缺 libarmadillo.so.15。 |
| 10-19 | +桩目录 /tmp/r18a-deb（libcblas→vendor OpenBLAS、wrapper2 桩 26 符号、armadillo 空桩）+ `-L -Wl,-rpath -Wl,--allow-shlib-undefined` | **0** | 5 目标全部产出（R18-C/R18-E 已验证同型方案）。 |

## RED 证据（生产码未修，仅测试在工作树）

| 测试 | 结果 | 失败内容 |
|---|---|---|
| test_image_fusion "[r18a]" | 4/4 失败 | brovey Inf pan 未洗；hpf Inf 邻居毒化 ×2 CHECK；brovey Inf MS band2 非 NaN；linearWeighted null band **SIGSEGV**（UB 实锤，运行中断致后续用例未跑） |
| test_atmospheric "*Inf*" | 3/3 失败 | dos1 −Inf→全带 +Inf；dos2 同；dos1 +Inf 未拒 |
| test_raster_spatial5 "*refuses band*" | 4 用例 16 断言失败 | band=0/-3/99 均非 InvalidParameter（band=0 被静默改 1 或落 GdalError） |
| test_mosaic "*Inf sample*" | 2/2 失败 | ±Inf 被当数据刷入画布 |
| test_image_enhancement "[r18a]" | 1/1 失败 | ChunkedProcessor 0×0 返回 true |
| test_obia_segmentation（WP1 恢复测试） | 1 失败 | multires 1e-6 epsilon 误杀 5e-7 有效像元（#1514 修复被回滚的实证） |

## GREEN #1 + #2（修复后双跑，全部同一构建）

| 套件 | GREEN#1 | GREEN#2 |
|---|---|---|
| test_image_fusion | 43 用例 / 1676 断言 全过 | 同 |
| test_atmospheric | 43 / 37498 全过 | 同 |
| test_raster_spatial5 | 13 / 87 全过 | 同 |
| test_mosaic | 10 / 38 全过 | 同 |
| test_image_enhancement | 14 / 6224 全过 | 同 |
| test_spectral_classification（恢复） | 30 / 197 全过 | 同 |
| test_obia_segmentation（恢复） | 14 / 850 全过 | 同 |
| test_fusion_quality_report | 7 / 41 全过 | 同 |

单跑回归（GREEN#1 后一次性）：
- test_change_detection：41 用例 / 888684 断言 全过
- test_perf_fusion：1 用例 / 12135 断言 全过（含 900MiB RSS 上限门）
- test_band_math：49 用例 / 130277 断言 全过

注："[r15a]" 作为 Catch2 tag 过滤器在本机 v3.7.1 匹配 0 例（名称内嵌 tag 的显示
与过滤行为不一致）；恢复用例改按名称通配运行：
`"*geotransform*,*sentinel void*,*classic -9999*,*fidelity statistics*,*both-unreferenced*"`
→ 5 用例 / 176 断言 全过（含非对称地理参考拒绝 = WP1 核心）。

## NOT_EXECUTED

- Qt GUI/桌面路径（offscreen 之外的交互）——本轨无 GUI 改动，套件自身以
  QT_QPA_PLATFORM=offscreen 覆盖。
- ASan/TSan lane（sanitizer-debug preset 未配置）——编译资源约束；补偿：改动均为
  纯标量谓词/边界洗涤，无双线程新共享状态；WP5 的 UB 修复由 RED SIGSEGV→GREEN
  直接证明。
- 线上 CI——按任务书不等待（#1491 记录 master CI 自 10-11 起无完成结果）。
