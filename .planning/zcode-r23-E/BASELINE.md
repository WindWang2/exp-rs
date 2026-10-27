# BASELINE — R23-E（跨平台构建/安装、插件执行与隔离、测试与离线交付稳定性）

执行时间窗：2026-10-26/27 UTC。执行环境：双环境单主机。

## 0. 基线 SHA（双重独立复核）

- `origin/master` = `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（2026-10-26T15:12:19Z merge #1677）
  - Windows 侧复核：`git fetch origin --prune` + `git rev-parse origin/master`（repo `C:/Users/wangj.KEVIN/projects/exp-rs`，工作树 clean，branch master）。
  - WSL 侧复核：克隆后再次 `git fetch origin --prune` + `git rev-parse origin/master`，一致。
- 起草快照值同为 `ad98a7a70...` → 执行期间无新 master 前进。
- Open PR = 0（`gh pr list --state open`），default branch = master（`gh repo view --json defaultBranchRef`）。
- Open Issues = 144（`gh issue list --state open --limit 500`）。
- 本地 `git worktree list`（Windows 仓库）：仅主 checkout（master，clean），无兄弟 agent 工作树；WSL 侧无其他 git 工作树（`~/exp-rs`、`~/exp-rs-new` 为无 `.git` 的源快照 + 旧构建缓存，非活动工作区）。

## 1. 执行环境（实测，非推测）

| 项 | Windows 侧 (Git Bash/MSYS2) | WSL archlinux 侧 |
|---|---|---|
| 内核 | MINGW64 NT-10.0-26200 | Arch Linux (WSL2) |
| 编译器 | 无（git-bash 内 gcc/g++/clang 均缺） | GCC 16.2.1 (`/usr/sbin/g++`) |
| CMake/Ninja | 无 | CMake 4.4.3 / Ninja |
| Qt | 6.8.0 msvc2022_64 (C:/deps/Qt) + VS2022 MSVC 14.38 + vcpkg（主 checkout build-dev 旧缓存证明可用） | 系统 Qt 6.11.2 (/usr/lib) |
| GDAL/PROJ/GEOS | OSGeo4W 存在 | GDAL 3.13.3 / PROJ / GEOS 系统 cmake 包 |
| gh CLI | 已认证 WindWang2 | 已认证 WindWang2（token 存储于 ~/.config/gh） |
| RAM/磁盘 | 全机资源 | 15.5 GB RAM（WSL 配置上限）、819 GB 可用磁盘 |

结论（记录于 DECISIONS D1）：主构建/CTest 环境取 **WSL archlinux GCC**（与 R21/R22 各轨 Linux 证据同一宿主家族：PR #1708 记录的宿主即 GCC 16.2.1）；Windows 侧承担 `.cmd`/PowerShell 真机脚本验证与 GitHub 操作。MSVC 全量构建未执行（NOT_EXECUTED，成本与时间预算；理由见 RISK_REGISTER R3）。

## 2. Worktree（强制独立，master 只读）

- 克隆：WSL `/home/kevin/exp-rs-r23e-clone`（自 Windows 主 checkout 本地克隆后 origin 改指 GitHub）。
- 工作树：`/home/kevin/exp-rs-E-20261026T170017Z`
- 分支：`agent/r23-e-platform-plugin-release-20261026T170017Z`
- BASE_SHA = `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`；HEAD 初始同 BASE；`git status --porcelain` clean。
- Windows 主 checkout 全程零写入（只读盘点 + 本地克隆来源）。

## 3. 基线构建（定向，D7 重规划后）

- 配置：`cmake -S . -B build-E -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DSICNU_EMBED_PYTHON=OFF -DSICNU_VENDOR_GDAL=OFF -DSICNU_BUILD_PYTHON_BINDINGS=TRUE`（复刻 `~/exp-rs-new/build` 已知可用系统依赖配置；E04 oracle 要求 SICNU_EMBED_PYTHON=OFF 为默认成立）→ **CONFIGURE EXIT 0**（2026-10-26T17:03:20Z）。
- 全量构建实测：9525 边、-j2 约 7 边/分（603 边时终止）→ 不可行（~22h），证据 `/home/kevin/r23e-build-baseline.log`。
- 定向构建（进行中）：WP1/WP5 测试目标及其依赖 = 1234 边，`flock … cmake --build build-E --parallel 4 --target exprs_plugin_host_worker isolation_plugin test_r23_e_plugin_typed_gates test_r23_e_native_terminal`（-j4 合规依据：宿主独占、RAM>12GB 空闲、无兄弟构建；ninja 进程参数 `-j 4` 留证）。日志 `/home/kevin/r23e-build-targeted.log`。
- WP4 构建（cli+worker）已 flock 排队自动衔接；随后自动跑 install smoke（w15 脚本）。
- 注：wsl.exe 控制台全部脱离期间 setsid 后台构建保持 WSL VM 存活（实测 15+ 分钟无控制台连续编译）。


## 4. 直接相关已合并 PR（已读 body/diff 摘要）

- **#1708 (r22-infra)**：修复 #1696/#1697/#1698/#1681（Closes）、推进 #1682/#1699/#1683。安装树 RPATH/`sicnu_worker` install/离线打包 --version 守卫/clang-tidy 顺序/墙钟分型 9 处。**不得回滚**；本轨复测其 smoke 并补真机 Windows `.cmd` 侧证据。
- **#1714 (r22-agent)**：修复 #1704/#1690/#1691/#1692/#1705/#1693。MCP 沙箱/env 注入/幽灵注册/IPC>1MiB/JobEngine 日志有界化。与 E 轨交集仅 `tests/CMakeLists.txt` append-only。
- **#1715**：#1714 合并带入 tests/CMakeLists.txt 冲突标记的修复 —— E12 防线对象。
- **#1662 (fix/full-review-sweep)**：62 commit 修复 124 issue，其中 E 轨领域修复：`77d70df9`(plugins #1551/#1592/#1644)、`58b935f7`(python #1555/#1556/#1608/#1609/#1647)、`1b3d28f1`(native #1557/#1617/#1618/#1641)、`496b9082`(cmake #1624)、`0847c5ec`(tests #1558)。**全部经 `git merge-base --is-ancestor` 验证在 master 上（无 merge-loss）。但该 sweep 未附任何回归测试**（4 个 E 域 commit 的 stat 零 tests/ 文件）—— 本轨核心价值点。
- #1673–#1677 (R21 A–E)：与 E 轨源区无写交集。

## 5. 测试/工具基线（已存在，避免重复造轮子）

- `tests/test_plugin_host_process.cpp`（35+ TEST_CASE）：crash 恢复、kill ladder、deadline、frame cap、cancel、并发、workDir 策略、UI schema、load/unload 基线回归、restart 耗尽、孤儿清理。**类型门（#1551/#1592 的 E1002/E6002 拒绝）无任何用例**。
- `tests/test_r22_infra_bundle_guard.sh`（#1708）：离线打包 --version 穿越弹幕 + 合法腿。
- `scripts/check_test_registration.py`、`scripts/check_presets.py`(JOBS_CAP=2)、`scripts/dev/merge_loss_audit.py`（R21-B）：E12/E05 的既有基础。
- `tests/test_worker_daemon_proxy.py`：独立 python 单测（未入 ctest）。
