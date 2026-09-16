# EVIDENCE — 运行记录（append-only）

## Phase 0（2026-09-16）
- [x] git fetch origin --prune；origin/master=a5b11b7f10（prompt 快照 ebcafb4d02 已过时：#991/#992 已合并，#993/#1000 新增）。
- [x] gh pr list → open: #1009 (UNSTABLE), #1008 (DIRTY)；diff --name-only 已取，无 scope 交集（PARALLEL_OWNERSHIP.md）。
- [x] gh issue list → #1001..#1007；dedupe 结论 BASELINE.md；#1001 in-scope 修复。
- [x] ISSUES.md 只读核验：D3 缺口多已修复（T-1/T-2/T-3/C-2 temporal 10.0 已修），不实施。
- [x] subagent #1（Explore, 只读）深度 I/O inventory 完成 → BASELINE.md 缺口清单。
- [x] worktree ../exp-rs-geospatial-io-formats-11 @ origin/master a5b11b7f10，branch zcode/geospatial-io-formats-11。
- [x] .gitignore 追加 !.planning/geospatial-io-formats-11/ 两行（append-only）。
- Skills 加载计划：codebase-design / code-review / diagnosing-bugs / domain-modeling / implement-spec 均存在（.agents/skills/）；ask-matt 存在；resolving-merge-conflicts 仅冲突时加载。goal-loop 协议按 prompt 内嵌执行。

## 构建资源记录
-（每次构建回填：时间、-j 级别、60s 间隔 CPU/RSS 采样或一次性的"无法测量"声明）

## Phase 1（2026-09-16）
- 配置：dev-default + `-DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON`（pybind11 FetchContent 网络克隆 TLS 失败，环境限制）+ `-DFETCHCONTENT_SOURCE_DIR_CATCH2=<main>/build-dev/_deps/catch2-src`（离线复用已填充源）。configure exit=0。
- 构建：`cmake --build build-dev --target ... -j2`（CMAKE_BUILD_PARALLEL_LEVEL=2, nice 10）。60s 资源采样：本机后台构建多 targets 均在 <60s 完成（sicnu_geospatial 基线 ~2min 内），未观察到 RSS>70%，维持 -j2（一次性声明，进程级 RSS 采样工具链缺失已在 PERFORMANCE.md 声明口径）。
- 新增文件：src/geospatial/io/{param_guard,finalize_manifest,stage_ledger}.{h,cpp}；tests/test_io_{param_guard,finalize_manifest,stage_ledger}.cpp；tests/CMakeLists.txt +3 行；src/geospatial/CMakeLists.txt +4 行；atomic_fs.cpp sidecarsFor +1 appended suffix（.sicnu-manifest.json 进组事务）。
- 测试证据（直接运行二进制，QT_QPA_PLATFORM=offscreen）：
  - test_io_param_guard: All tests passed (17 assertions in 5 test cases), exit=0
  - test_io_finalize_manifest: All tests passed (52 assertions in 4 test cases), exit=0
  - test_io_stage_ledger: All tests passed (7 cases), exit=0
  - 回归: test_io_atomic_failures / test_io_raster_contract / test_io_paths / test_io_identity / test_io_roundtrip_matrix / test_io_vector_contract 全部 exit=0
- 独立 oracle 记录：digest 用 FIPS 180-4 "abc" 向量（ba7816b…5ad）；字节翻转→digest_mismatch；形状漂移→shape_mismatch；crash 形态（无 journal/无 staged 文件/截断/spoof driver/形状谎报）全部 fail-closed。
- ctest 说明：catch_discover_tests PRE_TEST 模式下 `ctest -R <新目标>` 在发现前无法过滤（0 匹配）；本 track gate 以直接运行测试二进制为准（同一断言集），全量回归用 `ctest`（不带 -R）或目录级。
- DECISIONS 更新：D-004 已记录；D-008（writer 不动，manifest 经 finalizeAttached/算子级 opt-in 写出；checkTargetPath 对不存在本地路径按形状分类 + 目录必须已存在）将随 P2 commit 写入 DECISIONS.md。

## Phase 2（2026-09-16）
- #1001 修复：IoClipOperator srcCrsOverride 回归源声明语义（sourceCrsOverride 传递 + target=源网格）；已有 CRS 输入 + override → InvalidParameter 拒绝（fail-closed，输出不落盘）。独立 oracle：期望范围/CRS 由测试独立计算（Catch::Approx margin 1e-9），不复用实现输出。
- cog_options 计划层：REPLACE 合并语义（COG driver first-match-wins，追加式 override 是死信——DECISIONS D-009）；deterministic=NUM_THREADS=1+LEVEL 固定，同栈双生成 sha256 相等（2140 assertions 含字節一致证明）；blocksize=256 经 validateCog+GDALGetBlockSize 双确认；OVERVIEWS=NONE 大图 → pre-publish 拒绝且无残留（fail-closed 语义验证）。
- convert/raster_convert 増量：makeCogWithOptions（完整选项列表入口）；makeCog 行为不变（仅将 preset 组装移到调用侧，dtupe probe 多一次只读 open）。回归：test_io_roundtrip_matrix/test_io_fidelity/test_io_probe/test_adversarial_m3 全 exit=0。
- docs/io/cog-guide.md：OVERVIEWS=ALL→AUTO 漂移修正 + 新选项层文档。
- 资源记录：本 phase 一次构建观察到 load≈11.5（并发其他 track 构建所致），本 track 构建转 nice 15 + -j1（envelope 降档规则），内存 49GB 可用，未触 70% RSS 上限。

## Phase 3（2026-09-16）
- vector_interchange：DCAP_VECTOR+DCAP_CREATE capability 路由（替换硬编码名单）；capability 报告与 GDAL 运行时元数据逐项交叉核对（测试遍历报告断言 usable⇒DCAP 实况成立）。
- subdataset_inventory：netCDF 双变量 fixture（netCDF C 库直写，nc_enddef 后写入）；inventory count=2、kind=subdataset、投影 inspectSubdataset 宽4×高3；非 SDS 选择器拒绝（trust boundary）；driver 缺失 → WARN skip（repo 惯例）。
- metadata_patch：白名单 validate-then-apply；数值/ISO-8601/band 范围预校验（7 类 refusal 全覆盖且 digest 不变——证明未触碰文件）；read-only chmod → OpenFailed；read-back 用独立只读 open；GTiff patch 后 canonical READ 路径（inspectRaster）反映全部值；manifest 连续性：patch 后 verifyDataset 仍 verified + patches[] 历史=1 + producer 保留。
- 回归：test_io_operators（convert_format capability 路由后 130 assertions 全过）、test_io_multidim、test_io_canonical_metadata exit=0。

## Phase 4–6（2026-09-16）
- 新算子接线：io:subdatasets / io:metadata_patch / io:verify_dataset（宏注册 + 显式注册双路径）；convert_format capability 路由 + inputOpensAsRaster 双能力消歧。
- test_io_gdal_matrix：宏↔编译真值对账、截断 COG typed 拒绝 + digest drift、垃圾字节拒绝、截断 GPKG typed failure（driver-gated）、40000² 逻辑 VRT 有界（budget=cells×sizeof(double)，整窗读 typed 拒绝）。
- 全量回归（两次独立时点）：21 套件全绿。

## Phase 7（2026-09-16）
- 独立对抗 review（subagent #2）：0 P0 / 5 P1 / 3 P2 / 6 P3 + 2 nits；逐条 disposition 见 REVIEW_LOG.md；P1×5、P2×3、P3 可修项全部修复。
- 修复后全量回归 21 套件全绿（见上）。

## Phase 8（2026-09-16）
- git diff --check origin/master...HEAD：clean；冲突标记扫描：clean；secret 扫描：clean。
- Oracle 6 双验证：15 个关键套件连续两遍运行，两遍全绿（PASS1 15/15，PASS2 15/15）。
- rebase origin/master @ a5b11b7f10：up to date（无新提交；fetch 多次 TLS 抖动重试后确认）。
