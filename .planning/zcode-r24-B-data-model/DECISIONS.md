# R24-B DECISIONS（缺省工程决定，执行而非询问）

| # | 决定 | 理由 |
|---|---|---|
| D1 | 构建配方复制 R23-D 已验证组合：vcpkg toolchain (`C:/deps/vcpkg`) + 系统 Qt (`C:/deps/Qt/6.8.0/msvc2022_64`) + Ninja + MSVC2022，但 `CMAKE_BUILD_TYPE=RelWithDebInfo` | 兄弟轨验证可用；RelWithDebInfo 兼類性能测量（WP-B9/O-B4）与调试 |
| D2 | 并行度默认 `-j2`（CMAKE_BUILD_PARALLEL_LEVEL=2, CTEST_PARALLEL_LEVEL=1）；仅当空闲 RAM ≥12GiB 且无其他构建者时短暂 `-j4` | 侦察时空闲 9.4GB < 12GiB；CLAUDE.md 纪律 |
| D3 | 23 个候选 Issue 全部 FIXED_IN_MASTER → 本轨不重做；工作重心 = 残余缺口 G1-G4 + 对抗审查发现的 LIVE 缺陷 + WP 体系化测试 | 避免重复已合并工作（任务 §1.6） |
| D4 | GAP-G2（lineage 导入孤岛）为主攻实现项：exporter 输出方向保持的 `run_edges`（from/to 节点对），importer 在既有 store 事务内重挂 + 环预算；旧 bundle（无 run_edges）维持现有命名警告路径（向后兼容） | #1581 修复自认的 limitation；属现有模块功能补完而非新框架 |
| D5 | E2E/metamorphic 测试优先复用既有测试基建（Catch2 + `sicnu_discover_tests` + 现有 fixture helper），不新建测试框架 | 任务 §5 |
| D6 | 性能测量固定 fixture、清缓存口径、5 次重复、记录 CPU+RSS+方差；不做紧墙钟阈值断言 | 任务 §4 |
| D7 | 目标测试名前缀 `test_r24_b_*`；注册块 `# BEGIN R24-B`/`# END R24-B` 置于 tests/CMakeLists.txt 末尾 | 跨轨避让规则 |
| D8 | 本轨不运行任何真实外网/云服务；远程面用本地假 HTTP server fixture | 任务 §5 测试隔离 |
| D9 | Issue 不主动关闭（GitHub 状态治理非本轨授权范围）；PR 正文映射 FIXED_OPEN 证据 | 保守 |
| D10 | Windows 本机为唯一实跑平台；Linux/macOS 标 NOT_EXECUTED | 任务 §4/§9 |
