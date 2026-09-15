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
