# R18-A LOOP_LEDGER — 逐轮迭代记录

格式：序号 | 假设 | 实际改动文件 | 验证命令 | 退出码 | 结果 | 下一步

---

## L00 Recon（2026-10-19）
- 假设：任务书快照（master=7825c1ebd，#1513 基线滞后）已过时，需 LIVE 重查。
- 改动：无（只读）。
- 验证：`git fetch origin --prune`（exit 0，origin/master=2d2881631）；
  `gh pr list --state all --limit 80`（exit 0，唯一开放 #1513）；
  `gh issue list --state open`（exit 0，#1467/#1491）；
  `python scripts/dev/preflight.py --fetch --json`（exit 0）；
  `overlap_scan.py src/processing/algorithms src/analysis src/operators/rs --json` → overlap_count=0。
- 结果：Recon 完成；goal-loop 技能不存在 → 原生协议降级（DECISIONS D-01）。

## L00b worktree 创建重试
- 假设：首次 `new_worktree.py` 失败为网络瞬断（SSL unexpected eof → connect timeout）。
- 改动：无。
- 验证：重试 `git fetch`（exit 0）后 `new_worktree.py --path ../exp-rs-r18-a-science
  --branch agent/r18-a-science-numerics-perf --base origin/master` → created @2d2881631。
- 结果：唯一 worktree 就绪。教训：GitHub 间歇不可达，操作需重试与证据保留。

## L00c 三路只读深读审计
- 假设：算法域存在可实证的数值/契约/性能缺陷（先读后编码）。
- 改动：无（3 个 Explore 子代理只读）。
- 验证：产出三份审计报告（processing/algorithms、analysis、operators/rs）。
- 结果：头条 = #1514 修复被 #1516 合并静默回滚（我方 git 考古三重验证）；
  另得 P2/P3 清单入 TASKS.md。
- 下一步：WP1。

## L01 构建配置（第一次：失败）
- 假设：默认路径可发现 GDAL。
- 命令：`cmake -S . -B build-r18-a -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON ...`（全局锁下）
- 退出码：CMake 报错 `Could not find GDAL`（cmake/SicnuDepDoctor.cmake:194）。
  教训①：管道 `| tail -30` 掩盖了退出码（通知显示 exit 0）；后续一律重定向到
  日志文件并 `echo EXIT=$?` 追加。
- 根因：本机 GDAL 在 vendor prefix（兄弟轨道 R18-B/C 同样显式指定
  `paleo-singlefactor-closure/vendor/prefix`）。
- 下一步：从 R18-C 的成功 CMakeCache 提取全套覆盖项重配。

## L01b 构建配置（第二次：失败——静态库链接）
- 结果：CMake 配置成功（CONFIGURE_EXIT=0，含本地 Catch2 副本 /tmp/r18a-fc/catch2-src
  v3.7.1 = 仓库 GIT_TAG）。但 RED 构建在 qgis_core 链接失败（BUILD_EXIT=2）：
  `libexpat.a/xmlparse.o: relocation R_X86_64_PC32 ... can not be used when making a shared object`。
- 根因：从 R18-C 的 CMakeCache 抄依赖时抄了静态库（libexpat.a / libsqlite3.a）；
  R18-B 命令行实际使用的是共享库 .so。静态归档非 PIC，无法进共享对象。
- 教训②：抄兄弟轨道配置要抄「命令行」而非「缓存」（缓存可能含 Find 模块回填的
  环境特定值）。

## L01c 构建配置（第三~五次：共享库依赖排障）
- L01c（EXPAT/SQLite3→vendor .so）：失败——vendor prefix 的 libexpat.so/libsqlite3.so
  是**悬空符号链接**（目标 .so.1.11.2/.0.8.6 不存在，只有 .a 归档）。
  `make: 没有规则可制作目标 .../libexpat.so`。
- L01d（EXPAT/SQLite3→系统 /usr/lib .so）：qgis_core 链接推进，但 `libz.a`/`libzstd.a`
  （vendor 静态档经 CMAKE_PREFIX_PATH 泄入）非 PIC 再次失败。教训③：CMAKE_PREFIX_PATH
  指向 vendor lib 目录会让 FindZLIB/FindZSTD 优先抓静态档——必须逐个显式覆盖。
- L01e（+ZLIB/zstd→系统 .so）：qgis_core 链接成功（846MB）；5 个测试可执行文件链接时
  新失败：系统 libcblas.so.3 缺 `*_64_` 后缀 LAPACK 符号 + vendor libgdal 引用 26 个
  `wrapper2_*` 符号（Debian blacs 包装）+ vendor libarmadillo.so.15 不存在。
- 排障发现（两条已验证可行的兄弟方案）：
  - R18-C：`CMAKE_EXE_LINKER_FLAGS=-Wl,--allow-shlib-undefined`（test_mcp_server 已产出）；
  - R18-E：/tmp/r18e-deb 桩目录（libcblas.so.3→vendor OpenBLAS + libwrapper2stub.so 26 符号）
    + `-L/-Wl,-rpath` 注入。
- L01f（本轨最终方案，合并两者）：/tmp/r18a-deb（libcblas.so.3→vendor OpenBLAS 符号链接、
  26 符号 wrapper2 桩、空 armadillo.so.15 桩——融合测试直接链 GDAL，DT_NEEDED 需可解析；
  GDAL 的 armadillo 后端路径本轨测试不触达）+ `-L/tmp/r18a-deb -Wl,-rpath,/tmp/r18a-deb
  -Wl,--allow-shlib-undefined`。进行中。

## L01f 构建配置（最终：桩库方案，成功）
- 结果：EXIT=0，5 目标全部产出。运行时另需 curated 目录 /tmp/r18a-rt
  （vendor 非 Qt 库符号链接 + 系统 Qt6 全量符号链接，防 vendor 旧 Qt6Core 抢占）
  + LD_PRELOAD 桩 + PROJ_LIB/PROJ_DATA/GDAL_DATA（缺失时 CRS 测试假失败，已实证）。
  全口径见 TEST_RESULTS.md 头部。

## L02 WP1 验证（#1514 回滚恢复）
- 假设：恢复的测试在未恢复的生产码上 RED。
- 结果：OBIA 恢复测试失败 → 又发现 #1516 同时回滚了 rs_multires_segmenter.cpp
  （`git log a8d5cd510..HEAD -- <file>` 为空）。全仓排查：#1514 的 8 个非文档文件
  中 6 个被整体回滚（已全部恢复）；.gitignore 几条通用规则也被吞（记 CF-6，不越界修）。
- 验证：恢复后 5 用例/176 断言 GREEN×2；OBIA 850 断言 GREEN×2。

## L03 WP2/WP5/WP6a（融合）
- 改动：image_fusion.cpp 三处读边界 ±Inf 洗涤 + linearWeighted 防护/钳制 +
  PCA 零有效 errorMessage。验证：RED 4（含 SIGSEGV）→ GREEN×2；43 用例/1676 断言×2。

## L04 WP3（大气校正）
- 改动：findMin isfinite + dos1/dos2 输出谓词。验证：RED 3 → GREEN×2；37498 断言×2。

## L05 WP4（raster-spatial band 契约）
- 改动：resolveBandParam 共享校验。验证：RED 4 → GREEN×2。

## L06 WP6b/c（mosaic/chunked）
- 改动：isValid !isfinite；process() 拒绝 w/h≤0。验证：RED 3 → GREEN×2。

## L07 提交
- 5 笔原子 commit：41fd31b57（WP1）、3f2608543（WP2/5/6a）、d7d7b7adc（WP3）、
  97d78c585（WP4）、77802cf37（WP6b/c）。
- 下一步：性能批次（WP9/WP10）→ WP8b → 独立 R1 → PR。

## L08 R1 独立评审与修复
- R1（新 context）：P0×1（validEnd 悬空，F4 证伪成功）+ P1×1（rs:mosaic 生产路线）+
  P2×3（quac 输出谓词 / 流式 panWeight / dos1Histogram）+ P3×5。
- 教训④（重要）：构建竞态假绿——GREEN 后必须验证 .o mtime > 源 mtime；管道退出码陷阱再犯
  一次（`make | tail; echo $?` 捕获的是 tail）。
- 修复 commit 529481e72；11 套件 ×2 全绿；基准第 3 次一致。
- 下一步：R2 复审 → review_pack → push → PR。
