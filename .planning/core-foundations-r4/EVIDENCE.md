# EVIDENCE — core-foundations-r4

全链证据索引(每条含复跑命令)。轨道分支 `hardening/r4-core-foundations`,基线 `origin/master` `15e5c66b5`。

## 1. 基线与锚定(Phase 0)

- master 与 origin/master 0/0;`src/core`=2047 文件;CreateFileW=1(`qgsfileutils.cpp:340`);GetLastError=93 处/12 文件(全 QGIS vendor);`_wstat|WideCharToMultiByte|LoadLibraryW`=0;`rg -l atomic_fs src`=35 文件;portable.h=11 helpers;UTF-8 头注 ✓。逐条命令见 BASELINE §5。
- 在途 PR 实测 5 个(#1334-#1338),overlap map 与避让清单见 BASELINE §2;open issues=0。

## 2. 基线红绿分布(区分预存与本轨)

- `test_io_atomic_failures`(master 闭包,未含本轨任何改动):**8/12 绿;4 红 = `VectorWriter::create: dataset creation failed`(ESRI Shapefile)**。
- 根因链(源码级):`vector_writer.cpp:123` `stagedPathFor`(O_EXCL 预创建 0 字节文件)→ `:125 GDALCreate`;Shapefile 驱动拒绝已存在目标;GPKG/SQLite 容忍 0 字节故注入测试中 GPKG 创建通过。**与 PR #1338 diff 自述 Cluster A ~28 逐字吻合**(含"atomic_failures 4"计数),修复载体 `reservedStagedPathFor` 在 #1338 分支;根因文件全在本轨避让清单 → 预存红,defer-#1338。
- 复跑:`cd build-r4 && LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib ./test_io_atomic_failures`(预期同 8/12;合并 #1338 后应 12/12)。

## 3. 本轨改动红→绿证据(TDD)

| 工件 | 红证据 | 绿证据 | 提交 |
|---|---|---|---|
| 合同测试 Part B/C(7 文件 fsync 门槛 + journal 清理 pin) | 首跑 3 static case 失败(fsync/publish 顺序缺失;cleanup 缺失;rfind 定位) | 124 断言/13 用例绿 | 139721982 |
| 批 3(operators/workflow 3 文件) | `rg -c fsyncFile` 三文件均 0 | pin 后 140 断言/13 用例绿 | af6009251 |
| claimExclusiveUtf8 用例 | 全仓 rg:零 TEST_CASE | test_platform_portability 68/11 绿 | WP-A 提交 |
| 注入 lane | 21 用例中 3 处设备/断言缺陷经红→修→绿(含 sweep 需去读位、writer 原始异常透传合同、staged 声明目录即 RO 设备) | 137 断言/21 用例绿 | WP-C 提交 |
| UTF-8 家族 | 长路径用例首跑红(280 字节文件名超 NAME_MAX=255,ENAMETOOLONG 实证) | 修至"总路径>260 且分量≤255"后 103 断言/17 用例绿 | 792a295cc |
| 压力 lane | 自身捕获缺陷(loop 变量按引用)在 GCC16 断言下即红(vector OOB) | 修后连续 5 遍 exit 0 | WP-F 提交 |
| portable.h 平台分支 pin | —(现状即正确,固化为防回归;mutation 方向见用例注释) | 56 断言/6 用例绿 | WP-A 延伸提交 |

## 4. 门禁(Oracle §1)——正则命中目标清单与双跑

正则 `core|atomic|portab|utf8|fs|inject` 按二进制名命中 11 目标 + 本轨新增 3 目标(io 轻量)。门禁口径(BASELINE §7 / DECISIONS D-4):构建并运行以下集合,连续两轮 `-j1` 全绿;`test_io_atomic_failures` 的 4 个预存红按 §2 口径豁免(合并 #1338 即消除),除此之外零新增失败。

- 轻量:test_io_atomic_failures(8/12,见 §2)、test_portability_contract、test_platform_portability、test_portability_source_contract、test_atomic_fs_caller_contract、test_core_failure_injection_r4、test_core_concurrency_stress_r4
- 重型(全栈闭包,构建中):test_atomic_algorithm_adapter、test_atomic_algorithm_registry、test_atomic_registry_contract、test_dataset_core、test_fault_injection、test_store_fault_injection、test_harness_lab_injection

**双跑结果(待 build-r4 完成后回填)→ 见 §7。**

## 5. 交付下限对照(Oracle §2/§3/§4)

| 下限 | 状态 |
|---|---|
| 审计表 35/35 | ✓ ATOMIC_FS_SEMANTIC_MATRIX.md(逐行 rg 实证;26 一致 / 6 偏差已修 / 5 defer-#1338 含完整只读审计 / 合同与链接面) |
| 失败注入 ≥20 类,≥2 文件,每类 ≥2 失败模式 | ✓ 27+ 类/2 新文件(contract lane + injection lane;6 维度:RO-DIR/RO-FILE/MISSING/AS-DIR/CORRUPT/INTERRUPT) |
| portability 覆盖报告 | ✓ PORTABILITY_COVERAGE_REPORT.md(11 helper 矩阵 + 7 轴零普查 + 13 残留点逐点豁免) |
| 提交 ≥16 | 进行中(当前 9,收尾证据/评审/修复提交见 §8) |
| 触碰文件 ≥18 | ✓ 24+(src 10 + tests 6 + CMakeLists 1 + planning 7,均在白名单) |
| UTF-8 用例 ≥6 | ✓ 6 用例(中文/空格/长路径/大小写/BOM/混合分隔符) |

## 6. 环境事实(可复跑性)

- cmake 3.30.5(toolchain/cmake-dist)、ninja(pwb-sdks 前缀)、编译器 /usr/sbin/c++、Qt 6.11.2、GDAL 3.13.3。
- 运行时须 `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`(libodbc/libcryptopp 不在默认路径;兄弟轨道账本同记录)。
- `/tmp` 为 32G tmpfs(今日实测 98% 满——为兄弟轨道并行构建所致;本轨 fixture 均在 /tmp 下小文件,不受影响)。
- 资源红线:`ninja -j2`、`ctest -j1`;全程未超。注意 shell 别名 `ninja='ninja -j40'` 已规避(显式绝对路径调用)。

## 7. 门禁双跑日志(回填区)

PASS-1:(待回填)
PASS-2:(待回填)

## 8. 收尾提交链

(随提交追加)
