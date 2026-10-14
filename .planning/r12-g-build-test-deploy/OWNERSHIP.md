# R12-G OWNERSHIP — 所有权矩阵与共享文件冲突治理

- **方向**: R12-G（构建 / 本地验证 / 离线交付工程）
- **Worktree**: `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test` · 分支 `hardening/r12-g-build-test-offline`
- **基线**: `origin/master = 28abef358`（2026-10-13 22:40:27 +0800）
- **用途**：在动任何文件之前先查这张表。**越权写入 = 与其他方向产生不可合并冲突**，属于本轮红线。

---

## 1. 所有权总矩阵

| 路径 / 文件 | R12-G 权限 | 其他写入者 | 处置 |
|---|---|---|---|
| `scripts/**`（含 `scripts/dev/**`、`scripts/windows/**`、`scripts/bench/**`） | ✅ **可写（owner）** | — | 本方向主战场 |
| `cmake/**` | ✅ **可写（owner）** | — | 本方向主战场 |
| `packaging/**` | ✅ **可写（owner）** | — | 本方向主战场 |
| `CMakePresets.json` | ✅ **可写（owner）** | — | 本方向主战场 |
| `tests/support/**` | ✅ **可写（owner）** | — | fixture 层 |
| `tests/sicnu_test_main.cpp` | ✅ **可写（owner）** | — | 共享 test main |
| **`tests/CMakeLists.txt`** | ⚠️ **只读短租约** | **PR #1483（r12-a）、PR #1484（r12-c）** | 见 §2 |
| `tests/test_*.cpp`（A/C 改名的那些） | ❌ **禁止写入** | PR #1483 / #1484 | 不碰 |
| **`src/core/CMakeLists.txt`** | ❌ **不属 R12-G** | 其他方向 | 见 §3 |
| `src/**`（全部） | ❌ **禁止写入** | PR #1483(A) / #1484(C) | 完全不碰 |

---

## 2. ⚠️ `tests/CMakeLists.txt` — 共享敏感文件

### 2.1 冲突事实（BASELINE.md §2.2 实测）

| PR | 标题 | head | 变更文件数 | inline 评论 |
|---|---|---|---|---|
| **#1483** | hardening(r12-a): remote sensing scientific algorithms & numerical contracts hardening | `hardening/r12-a-science-numerics` (8fe92f959) | **42** | 0 |
| **#1484** | hardening(r12-c): harden ScratchRegistry lifecycle, AssetStateProvider thread-safety, and Pi test suites (#1449) | `hardening/r12-c-agent-workflow-runtime` (a50c328fd) | **10** | 0 |

两个 PR **均修改 `tests/CMakeLists.txt`**，且**均无 inline review comment**（`resolution=UNKNOWN`——REST 无法给出 GraphQL reviewThread 的 resolved 状态，不做推断）。

### 2.2 行号冲突定位

| 参照系 | 行号 | 说明 |
|---|---|---|
| 审计报告引用（改动前） | **约 423–437**（netcdf 探测块）、**570**、**607** | `AUDIT_G04_G05_G12.md` §1.1 与 `BASELINE.md` §3 |
| 本轮改动后（当前文件） | **432–445**（netcdf 块）、**578–579**、**616–617** | `git diff -U0` 实测 hunk 头 |
| #1483 触达位置 | 约 **1615 行** | 其他方向 |
| #1484 触达位置 | 约 **4115 行** | 其他方向 |

> **行距判断**：本轮改动集中在 432–617 区间；#1484 的 4115 行与之相距约 3500 行，#1483 的 1615 行相距约 1000 行。**`git merge` 的 3 行上下文窗口不会重叠**，因此本轮改动与两个 PR 在文本层面**大概率可自动合并**；但 `tests/CMakeLists.txt` 是 15640 行的巨型文件，仍需按 §2.3 的流程走短租约。

### 2.3 短租约处置规则

1. **只读优先**：本方向所有对 `tests/CMakeLists.txt` 的需求，先尝试在 `tests/support/**` 或 `scripts/**` 内解决（G02/G11 就是这么处理的）。
2. **确需写入时**：
   - 先记录**单写入者申请**（说明包 ID、改动行区间、预计行数）
   - 在**自己的分支上改**，不直接推到 `master`
   - 合并前 `git rebase` 到最新 `origin/master`，冲突则**重做**而非强解
3. **绝不抢写**：不因为"改动小"就绕过申请。

### 2.4 本轮对该文件的实际改动（`git diff --numstat` = `15/5`）

| 改动后行 | 内容 |
|---|---|
| `:432` | `find_library(NETCDF_LIBRARY NAMES netcdf)`（保留） |
| `:433` | **新增** `find_path(NETCDF_INCLUDE_DIR NAMES netcdf.h)` |
| `:434` | 守卫改为 `if(NOT NETCDF_LIBRARY OR NOT NETCDF_INCLUDE_DIR)` |
| `:436`、`:440` | FATAL_ERROR 文案带上库/头各自实际值，指引设置 `NETCDF_LIBRARY`/`NETCDF_INCLUDE_DIR` |
| `:444` | **新增** `target_include_directories(test_io_multidim PRIVATE ${NETCDF_INCLUDE_DIR})` |
| `:579` | **新增** `target_include_directories(test_io_subdataset_inventory PRIVATE ${NETCDF_INCLUDE_DIR})` |
| `:617` | **新增** `target_include_directories(test_io_fabric_multidim_11 PRIVATE ${NETCDF_INCLUDE_DIR})` |

**性质**：纯**加法**（新增 3 行 `target_include_directories` + 1 行 `find_path` + 守卫条件扩展 + 文案更新），**未删除任何既有测试注册**，与 G04-1 的 41 个条件门零交集。

---

## 3. ⚠️ `src/core/CMakeLists.txt` — 不属 R12-G

### 3.1 事实

| 项 | 内容 |
|---|---|
| 缺陷 | G05-3（P1）：`src/core/CMakeLists.txt:2754-2756` 两条 `find_library`（`setupapi` / `version`）无 `REQUIRED`、无 `if()` 守卫 |
| 后果 | `PATHS $ENV{LIB}` 在非 VS 开发者命令行下为空 → `SETUPAPI_LIBRARY-NOTFOUND` 字面量进 `qgis_core` 链接行 → 链接期 cryptic fail，下游数百测试 target 连锁失败 |
| 与本方向关系 | 落在 R12-G 的 Windows 离线 lane **上**，但**不在 R12-G 的所有权范围里** |

### 3.2 处置

**不修改。** 作为跨方向发现归档，交 `src/core` 的 owning direction。理由：

1. **所有权**：`src/**` 已被 PR #1483（R12-A）与 #1484（R12-C）占用写入，R12-G 与之并行写入会产生三方冲突。
2. **验证预算**：改这条需要一次完整 Windows 构建 + 链接才能证伪，本轮禁止重型编译。
3. **已有同类范式**：`tests/CMakeLists.txt:428`（netcdf）与 `cmake/SicnuFeatureProbes.cmake:117-120`（显式 SKIP + 计数）都是现成的修复模板，owning direction 照抄即可。

### 3.3 附带建议（不实现）

仓库现有的静态门禁 `check_platform_link_guards`（`tests/test_build_wiring_drift.cpp:2041-2065`）只认 `ws2_32`/`wsock32`，覆盖不到 `setupapi`/`version`/`netcdf`。建议把"未守卫的 `find_*` 结果进 `target_link_libraries`"整体纳入门禁——但这属于共享文件改动，需走 §2.3 流程。

---

## 4. 跨方向 `_exit` 缺陷（20 个 TU）的所有权处置

### 4.1 不归 R12-G 的部分

| 文件 | 所有权 | R12-G 动作 |
|---|---|---|
| 19 个 Windows-only `_exit` 测试 main（`tests/test_*.cpp`） | 各测试所属方向的 owner | ❌ **不修改** |
| `tests/helper_external_process.cpp:106` | 有意（子进程度量 fixture） | 建议显式豁免注释 |
| `tests/test_fault_injection.cpp:214` | 有意（fork 子进程） | 建议显式豁免注释 |

### 4.2 为什么不在 R12-G 自有的 `tests/support/**` 层"顺手兜底"

看似可以在 `tests/support/` 加一个 atexit 钩子来清理临时目录，但：

1. `_exit()` **不运行 atexit**——钩子根本不会被调用，技术路径不成立。
2. 真正的修法只有两条：改成 `return`（会让栈上 `QgsApplication` 在 `exit()` 链里析构，而 QGIS 全局缓存/注册表的静态析构顺序正是当初引入 `_exit` 要规避的，`tests/support/qt_lifecycle.h:2-21` 有明确记载），或按 `qt_lifecycle.h` 的 heap-app + 有序 teardown 范式改造。两条都需要改那些 TU 本身。

→ **处置**：作为跨方向发现完整记录（file:line + 5 条辨析），交 owning direction 在能跑 Windows suite 时改造。本轮**不产出任何相关代码改动**。

### 4.3 经核查为误报的部分（勿重复修补）

| 误报 | 证伪证据 |
|---|---|
| "`processScratchDir` 被 `_exit` 跳过导致泄漏" | 全仓仅 `test_io_multidim.cpp:34`、`test_quality_mosaic_operator.cpp` 用 `processScratchDir`，二者都走 `Catch2WithMain`（即修好的共享 main），**与 20 个 `_exit` 二进制零交集** |
| "20 个 TU 都会无界累积 `%TEMP%`" | 其中 **17 个不使用** `processScratchDir`；真正会漏临时目录的只有 **2 个** |

---

## 5. 本轮改动的所有权合规核验

| 文件 | 权限 | 改动量 | 合规 |
|---|---|---|---|
| `scripts/build.sh` | ✅ owner | +104 −0 | ✅ |
| `scripts/dev/resource_guard.py` | ✅ owner | +12 −4 | ✅ |
| `scripts/verification_ladder.py` | ✅ owner | +53 −1 | ✅ |
| `tests/CMakeLists.txt` | ⚠️ 只读短租 | +15 −5 | ✅ 纯加法、零测试删除、与 #1483/#1484 行距 ≥1000 行 |

**越权改动：无。**
**测试删除：无。**
**未提交状态**：4 个文件均为工作区改动，尚未 commit。
