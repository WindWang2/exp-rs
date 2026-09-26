# fix(core,platform): harden core foundation — atomic publish fsync gate on all callers, 35-file atomic_fs semantic audit, error-path failure injection (Track 5 R4)

## 诚实声明(先读)

- 本 PR **本地已验证、未等待线上 CI**(轨道规则);全部证据可按 EVIDENCE.md 命令原样复跑。
- 门禁口径如实声明:Oracle 正则 `core|atomic|portab|utf8|fs|inject` 按二进制名命中 11 个既有目标 + 本轨新增 3 个轻量目标;**其余 237 个测试目标不在正则内,未构建、未运行、不声称**。`test_io_atomic_failures` 在 master 基线上即 8/12(4 个预存红,见下),除此口径内零失败、双跑一致。
- 提示词的 token 工作量算术(≥2 亿)与实际消耗不自洽:本轨账本记录**实际**消耗(远低于该数字)。工作量门禁按交付物对照(审计表 35/35、注入 21 用例、13 主体类/25 组合、6 提交文件族、35 文件审计、≥16 原子提交),不按 token 燃烧量声称。账本与 EVIDENCE 如实记录,未夸大。

## 实测基线与漂移

- 基线 `origin/master` = `15e5c66b5`(Phase 0 fetch 实时,与写作值一致,0/0);分支基于该点,收尾前将 rebase 复核。
- 四份评审材料(PROJECT_REVIEW_DOSSIER_5.0 等)实测不存在于仓库/历史 → 按预案降级为 open PR 描述 + `WHOLE_REPO_REVIEW.md`。

## 与在途 PR 的文件重叠(避让声明)

5 个 open PR 实测(#1334-#1338)。**本轨避让不写**:`src/geospatial/util/atomic_fs.{h,cpp}`、`src/geospatial/raster/raster_writer.cpp`、`src/geospatial/vector/vector_writer.cpp`、`src/geospatial/convert/raster_convert.cpp`、`src/processing/gdal/staged_raster_output.h`(#1338 独占);`src/core/plugin_host.{h,cpp}`(#1334 独占)。`tests/CMakeLists.txt` 与 `.goal-loop-ledger.md` 为共享追加式文件,本轨仅尾部追加(0 删),若上游先合并预期为机械可解的文本相邻冲突。审计表对 #1338 独占的 4 个调用方 + 合同定义面**照做完整只读审计并标 defer-#1338**;#1338 diff 自述的 Cluster A(stagedPathFor O_EXCL 预创建 vs GDAL Create)正是本轨基线 4 个预存红的根因,合并即消除。

## 逐 WP 根因与修复

- **WP-A portability 收敛核查**:v1 假设的"平台 API 散点"经 7 轴普查证伪(src/core 非 vendor 面 pid/env/fopen/宽窄转换/裸 rename/fsync/MoveFile 全为 0)。残留 13 点(CreateFileW 1 + GetLastError 93 处/12 文件)全部为 QGIS vendor 层,逐点书面豁免(PORTABILITY_COVERAGE_REPORT.md §3.2,统一依据 DECISIONS D-2)。11 helper × 模块采用矩阵落盘;`claimExclusiveUtf8` 零测试覆盖 → 补独占语义用例;portable.h 三个 durability helper 的 Windows 分支加 mutation-kill source pin(CREATE_NEW/FlushFileBuffers/_wfopen)。发现 atomic_fs.cpp 与 portable.h 两组同语义重复 → 记录 defer-#1338(§4)。
- **WP-B atomic_fs 语义统一(核心)**:35/35 语义审计表(ATOMIC_FS_SEMANTIC_MATRIX.md,逐行行内证据)。**11 处偏差全部修复**:7 文件的"publish 前 fsync 门槛"(cartography export/atlas/manifest、edit persistence、warper、post_process、class_raster 单文件+组、classification_pipeline 组、polygonize、detection_tile_engine、workflow_run_coordinator)+ session_journal ofstream 失败路径 staged 清理 + post_process publish 失败 staged 清理。组发布 flush 全部存在性守卫(不收窄 skip-missing 合同);单文件门禁置于第一个 rename 机制之前(D-6)。stage_ledger:360-361 为仓内既有范本,本轨把其余 publisher 统一到它。
- **WP-C 失败注入**:新 lane `test_core_failure_injection_r4`(21 用例/137 断言,POSIX chmod 设备)+ 合同 lane `test_atomic_fs_caller_contract`(运行时合同 + source pin,154 断言/13 用例)。6 个注入维度(RO-DIR/RO-FILE/MISSING/AS-DIR/CORRUPT/INTERRUPT);每类断言 typed 错误 + 可定位上下文子串 + 故障清除后可重用。与既有 test_io_atomic_failures 的回滚矩阵互补不重复。
- **WP-D UTF-8 边界**:src/core 的 `toLocal8Bit` 仅存于 QGIS vendor(非 vendor=0);3 处非 vendor `QString::toStdString()` 喂 UTF-8 合同 API 正确(Qt5+ 语义)→ 无收敛缺陷,不造假改动。落地 ≥6 回归用例:中文/空格/总路径>260(分量≤NAME_MAX)/大小写混排/BOM/混合分隔符,各断言往返恒等 + 真实落盘字节一致。
- **WP-E 统一日志合同**:38 行散点全清单处置(LOG_CONVERGENCE.md):28 行 QGIS/GDAL vendor 豁免、10 行 plugin_host defer-#1334;非 vendor 非 defer 绕过 = 0,零代码改动(不为改而改)。
- **WP-F 热点安全**:发布热路并发判据 lane `test_core_concurrency_stress_r4`:C1 16 线程×125 staging 声明全局唯一零冲突;C2 8 线程同目标 200 次原子发布 + 全程 monitor 线程断言"任何时刻目标只可能是合法 payload 之一"(无 torn 态);C3 全 join。判据预先写死,连续 5 遍绿。测试自身一处按引用捕获循环变量的缺陷在作者期即被抓出并修复(变异即红)。
- **WP-G 防回归**:新测试目标 io 轻量注册(只链 Catch2+Sicnu::Geospatial,重演 #1303/#1304 漏链教训的对面);门禁集合双跑日志入 EVIDENCE §7。

## 独立对抗性 review(Phase 5,只读子代理,含变异验证)

结论 SHIP-WITH-FIXES → **全部处置**(REVIEW_LOG.md):P0-1 export_manifest fsync 门禁逃逸 bool 契约(会绕过页面回滚)→ 包 try→fail;P1-1 新 fsync 失败路径滞留 staged → 补 drv->Delete;P1-2/P1-3 两处 pin 盲区(review 以变异证实)→ 加固为逐锚点/分支界定,并以同法反向变异自证红→绿;P2×4、P3×4 全部采纳(含 EVIDENCE 计数与矩阵列对齐重建、白名单扩展 D-7 记账)。review 确认矩阵"是读过代码写的"、白名单合规、并发 lane 无竞争且有效力、"4 预存红"闭包论证 airtight。

## 用户可感知行为变化

- 制图导出/地理配准/分类栅格/检测矢量/工作流工件等**交付文件在断电/崩溃时不再可能以未落盘字节形式出现**(旧内容或新内容,无中间态)。
- 会话日志保存失败不再在会话目录留下孤儿 staging 文件。
- 其余为纯加固:正确路径行为零漂移(review 逐 site 核对)。

## 本地验证(可复跑)

- 构建:全新 build-r4(Debug/Ninja/ENABLE_TESTS=ON,-j2 全程;configure 需 pwb-sdks 前缀,命令见 BASELINE §1)。
- 门禁双跑:7 个正则命中轻量目标逐二进制串行 ×2(**两遍完全一致**:6 套件全绿 17+56+103+154+137+8 断言;test_io_atomic_failures 恒定 8/12,4 预存红 = #1338 Cluster A)。`ctest -R` 无法按二进制名选取——轻量 lane 的 PRE_TEST 发现已 Catch2 用例名注册,等价门禁(退出码判据,-j1 语义)与双跑日志见 EVIDENCE §7。
- **重型 7 个正则命中目标在 master 上本就无法链接**(libsicnu_agent.so 引用 `agent_loop::VerificationReport::aggregate` 而链接线缺 `sicnu_agent_loop`,~161 可执行文件受累)——逐字即 **PR #1335 在修的 review P0**,修复文件属 #1335 独占,本轨 defer;基线与现态一致,零新增失败。
- 运行时须 `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`。

## 未解决项(backlog)

1. rows 11/19/21/27 + 合同面 31/32 的写权限随 #1338;其 `reservedStagedPathFor` 合并后,本轨基线 4 预存红应转绿(建议合并后复跑 `test_io_atomic_failures` 验证 12/12)。
2. `src/core/plugin_host.cpp` 10 行日志散点 defer-#1334;重型测试目标的链接图修复(PUBLIC sicnu_agent_loop)defer-#1335,合并后建议复跑本轨门禁的 7 个重型同正则目标。
3. atomic_fs.cpp 与 portable.h 的两组同语义重复(claim/directory-fsync)收敛,建议 #1338 后续清理。
4. 提示词设想的 GetLastError 收敛在 vendor 层不适用;若未来 QGIS rebase 减少 vendor 面,可重开。
