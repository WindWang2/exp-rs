# DECISIONS — core-foundations-r4

## D-1 在途 PR 避让(Phase 0)
PR #1338 独占 `atomic_fs.{h,cpp}` 合同面与 raster_writer/vector_writer/raster_convert/staged_raster_output;#1334 独占 `src/core/plugin_host.{h,cpp}`。本轨道对这些文件**只审计不写**;审计表标 defer-#1338。理由:file-overlap map(BASELINE §2)。

## D-2 平台 API 残留处置口径(WP-A)
`src/core` 内 CreateFileW(1)/GetLastError(93处/12文件)全部位于 QGIS vendor 移植层(`qgs*.cpp`:ogr/gdal providers、browser、vectorfilewriter、offlineediting、arrowiterator)。处置:**书面豁免**而非收敛——理由:QGIS upstream 代码,逐点改造会制造长期 merge drift;调用点处于 GDAL/OGR 错误轮询回调内,非"改不动"而是"vendor 边界约定"(比照 portable.h 头注对 sdk/exprs 等"既有本地变体"的处理先例)。`qgsfileutils.cpp:340` 的 CreateFileW 是 Windows 长路径探测(FILE_FLAG_BACKUP_SEMANTICS 打开目录),非路径编码问题,helper 无对应能力,豁免。豁免清单逐点入 PORTABILITY_COVERAGE_REPORT.md。

## D-3 证据入库方式(Phase 0)
`.planning/*` 默认 gitignore;证据文件 `git add -f` 入库(不改正 .gitignore,避免与 #1334/#1335 在 tests/CMakeLists 与 cmake/ 的改动冲突)。账本保持未跟踪(.git/info/exclude)。

## D-4 门禁口径(Phase 0)
Oracle 正则命中 11 目标;其余 237 目标不在正则内不构建。ctest catch_discover_tests 为 PRE_TEST,未构建目标不产生发现失败——以"构建的 11 目标 + 新增目标"双跑全绿为收口,PR 与 EVIDENCE.md 如实声明口径。

(后续 WP 内决策逐条追加,编号递增)

## D-5 session_journal 自建原子写不收敛到 atomic_fs::writeFileAtomic(批 1)
`src/agent_loop/session_journal.cpp` 自持 writeFileAtomic(pid+counter+rng+O_EXCL+syncFileUtf8+rename+syncDirectoryBestEffortUtf8,与 #1097/#1323 合同同构)。收敛需 agent_loop→Sicnu::Geospatial 链接依赖(现仅 header-only portable.h);#1323 的 portable 三件套正是为免该依赖。判定:刻意豁免(结构边界),仅修复其失败清理缺口(staged 空文件残留)。fs::rename 的 Windows 语义依赖 MSVC/libstdc++ 的 REPLACE 实现,与 publishStagedFile 的显式链不同源——记录为已接受差异(POSIX 主场;journal 单文件、无 sidecar)。

## D-6 fsync 门槛放置原则(批 1)
单点 fsync 放在**第一个 rename 机制之前**(覆盖主路径+回退路径),不放进回退分支(否则主路径裸奔);组发布 existence-guarded 逐成员 fsync,不收窄 publishStagedGroup/Members 的 skip-missing 合同。所有 fsync 走 atomic_fs::fsyncFile(同一 typed GeoError 失败语义),调用方零平台分支(WP-B 审查门禁)。

## D-7 白名单声明扩展(review P2-2 记账)
本轨在 `tests/test_platform_portability.cpp`(+140,纯追加)与 `tests/test_portability_source_contract.cpp`(+81,纯追加)两个**既有**测试文件内追加用例。按 prompt 白名单字面("tests/ 新测试文件 + tests/CMakeLists.txt 注册行")属扩展项;按 prompt 白名单首条 `tests/(core 对应测试)` 属内。两 diff 0 删除、不改变既有断言,作为声明扩展逐处记账于此。

## D-8 单构建目录 ninja 互斥纪律(期间事故复盘)
本轮门禁构建期间,本轨的前台快速验证构建与后台全量门禁构建在同一 build-r4 上并发(ninja 文件锁不保护整个构建过程,仅保护 restat/deplog 一致性),造成 `libqgis_gui.so`/`libsicnu_agent_loop.a` 双写损坏,级联 ~5700 步重建。教训入库:同一 build 目录**任何时刻只允许一个 ninja 进程**;临时验证构建必须等待或使用独立构建目录。本机 10 条并行轨道各自持有独立构建目录,互不影响(已核实全部 ninja 进程 cwd)。坏产物特征备查:`ld: file format not recognized`、静态归档 `nm` 缺新符号。

## D-9 重型测试目标链接图 defer-#1335
`libsicnu_agent.so` 引用 `agent_loop::VerificationReport::aggregate` 而其链接线缺 `sicnu_agent_loop`(ELF 允许 .so 带未定义符号,错误后移到下游全部可执行文件,~161 个)——master 预存,逐字即 PR #1335 的 review P0;修复文件 `src/agent/CMakeLists.txt` 属 #1335 独占 → 本轨 defer。影响:门禁正则命中的 7 个重型目标在 master 基线即不可构建(与本轨改动无关,闭包论证见 EVIDENCE §2/§4),合并 #1335 后应复跑。
