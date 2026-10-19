# R15-B 基线（BASELINE）

> 轨道 B：空间数据链路、数据集/实验存储与可恢复持久化。只审计/加固既有模块，不新增 IO 框架/产品模块/框架迁移。

## 实时事实（Step 1，2026-10-16 20:00–20:10 UTC+8 采集）

| 项 | 值 |
|---|---|
| origin/master | `46791bc6`（2026-10-16 10:59 UTC+8，Merge #1509） |
| 提示快照 master | `8b856721`（#1511）——已过期：其后 #1505、#1509、#1512 均已合并 |
| 开放 PR | **0 个**（`gh pr list --state open` 返回 `[]`） |
| 最近合并 35 个 | #1444–#1512（含 #1501–#1512 全部） |
| 开放 issue | 25 个（分类见 ISSUE_PR_REVIEW_MATRIX.md） |
| 远端分支 `hardening/r15*` | 不存在 → 新建，非续作 |
| 本轨分支 | `hardening/r15-B-geodata-persistence`（基于 `46791bc6`） |
| 工作树 | `<repo 父目录>/exp-rs-r15-b-20261016` |
| 构建树 | `build-r15-b`（Ninja, Release, ENABLE_TESTS=ON, ccache 隔离目录） |

### 与提示快照的差异（影响所有权）
- #1505 已于 2026-10-16 10:58 UTC+8 合并（merge `871f963c`），其 11 条 review thread **全部未 resolve**；
  原"#1505 独占文件只读"的前提消失。本轨仍保守处理：`resource_uri.cpp`、`offline_gate.cpp`、`range_cache.*` 不改，只加回归 oracle。
- #1509 已合并（`46791bc6`）；#1512 已合并（`f7693fce`）。不存在需要"去重/不 cherry-pick"的开放 PR。

## 环境
- OS：Linux（box），g++ / cmake 3.31 / Ninja / ccache；Qt 6.8.2，GDAL 3.10.3，PROJ 9.6，GEOS 3.13.1，SQLite 3.46.1。
- 盒子上的开发依赖曾被清理：本轮为配置成功补装了 `bison flex libprotobuf-dev protobuf-compiler libopencv-dev libjsoncpp-dev libxkbcommon-dev qtkeychain-qt6-dev`（apt）。
- 构建：`flock -w 1200 /tmp/exp-rs-agent-locks/global-build.lock -c 'CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build-r15-b -j2 --target <t>'`；内存 >70% 时降为 -j1。
- 测试：`QT_QPA_PLATFORM=offscreen ctest -j1 --output-on-failure -R <pattern>`；TMPDIR/CPL_TMPDIR/CCACHE_DIR 均为本轨私有目录。
- 本工作树使用 `git config --worktree` 设置提交身份（WindWang2 noreply），未改全局配置；为此在共享仓库 config 中开启了 `extensions.worktreeConfig=true`。

## 工具链说明
- `/teamwork-preview`、`/goal-loop`：**未安装/等价执行**。采用 Oracle-Ledger 循环：分析 → RED 测试 → 最小修复 → GREEN×2 → 记账（LEDGER.md）→ 下一项。
- 独立评审子代理不可用：评审由同一代理以独立的对抗式审查轮次完成（见 REVIEW_ROUND1/2.md），**是等价替代，不是真正独立**。

## 基线测试（修改前，master `46791bc6`）
命令：`ctest -j1 --output-on-failure -R '^(test_io_uri|test_io_atomic_failures|test_fabric_catalog::test_io_fabric_catalog|test_io_stage_ledger|test_io_paths|test_io_remote_validator)::'`
结果：**90 个中 89 通过，1 失败**（exit 8）：
- `test_io_remote_validator::the session identity resolver returns on first sight, never deadlocks`
  —— 抛出 `RemoteSourceValidator: private / loopback / link-local hosts are blocked`。
  根因：#1504（R12-B）把生产 resolver 的 `blockPrivateNetworks` 设为 true，回环夹具被拒，且拒绝以异常逃逸（违反头文件"拒绝 ⇒ 空 token"契约）。本轨 B04 处理。
- `test_platform7_library` 目标存在（`ninja -t targets` 已核实），未纳入基线构建（依赖链较重），见 TEST_RESULTS.md。
