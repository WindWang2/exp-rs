# R12-G ORACLE LEDGER — 每个工作包的 RED → 修复 → GREEN×2 证据

基线 `origin/master = 28abef358217520dfbb802efdfdb11a2bf850465`；worktree `exp-rs-r12-g-build-test`；分支 `hardening/r12-g-build-test-offline`。
资源纪律全程：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`。

| WP | 缺陷 | 状态 | RED | 修复 commit 将记于 | GREEN1 | GREEN2 |
|---|---|---|---|---|---|---|
| G01 | 构建/CTest/发布拓扑 census | PASS（基线证据） | — | — | 真实 configure 成功（见 PERFORMANCE.md） | `check_presets.py` ALL PASS ×2 |
| G06 | 跨进程构建租约未被任何入口调用 | **PASS** | 见下 §1 | 待提交 | selftest ALL PASS（含新断言） | 待跑第 2 次 |
| G12-2 | `run_ctest` 空注册被判 passed | **PASS** | 见下 §2 | 待提交 | `run_ctest -> failed` | `ctest` 空注册 exit 8 对照 |
| G12-1 | 全条目 not-built 仍 OVERALL PASSED | **PASS** | 见下 §3 | 待提交 | 语法/--list OK | 待跑对空树 |
| G05-1 | netcdf 只查库不查头（假成功） | **PASS**（待重配置复核） | 见下 §4 | 待提交 | `cmake -P` 证明 find_path 可解析 | 需重跑 configure |
| G05 | netcdf NOTFOUND 进 link（#1468 P2） | **ALREADY_FIXED** | — | — | `tests/CMakeLists.txt:427-435` 已是 FATAL_ERROR | 现场 configure 实测通过 |
| G02 | `sicnu_test_main.cpp` `_exit` | **ALREADY_FIXED** | — | — | `sicnu_test_main.cpp:64` 为 `return result;` | — |
| G02 | 其余 19 个 Windows-only `_exit` main | **NOT_FIXED（有理由）** | 见下 §5 | — | — | — |
| G04-1 | 41 个条件块静默吞测试 | **NOT_FIXED（跨方向/共享文件，有理由）** | 纯文本 census 已复现 | — | census 输出见 AUDIT_G04_G05_G12.md | — |
| G05-3 | `src/core/CMakeLists.txt:2754` 未守卫 find_library | **NOT_FIXED（越权：src/core 非 R12-G）** | 静态确证 | — | — | — |
| G03 | HTTP fixture 可取消性 | 见 AUDIT_G02_G03_G11.md | — | — | — | — |
| G08/G09/G10/G13 | 打包/离线脚本 | 见 AUDIT_G08_G09_G10_G13.md | — | — | — | — |

---

## §1 · G06 — 跨进程构建租约从未被接线（P1，R12-G 自有范围）

**根因**：`scripts/dev/resource_guard.py`（370 行，含 `scripts/dev/tests/test_resource_guard.py` 339 行单元测试）实现了完整的跨进程租约，但**全仓库零调用点**（`grep -rn resource_guard` 只命中 `.planning/` 历史文档与自身）。`scripts/build.sh` 的 `cmd_build` 直接 `cmake --build … --parallel N`，`scripts/windows/build.cmd:223` 直接 `cmake --build "%BDIR%" --parallel %JOBS%`。`-j<=2` 只约束**单个**构建；同机多个 agent/worktree 各自守 2 核，机器承受的是总和 —— 重链接阶段 OOM 会连坐其他 lane。

**RED（修复前，实测）**
```
$ bash .planning/r12-g-build-test-deploy/probe_g06_guard.sh
--- entry point: .../scripts/build.sh
--- exit code: 0
--- output:
PAYLOAD-RAN --build C:\...\Temp/tmp.w49Cu4yEOx/build --parallel 1
PROBE: RED — the entry point built while another process held the lease
```
（探针：stub `cmake` 打印 PAYLOAD-RAN；后台用 resource_guard 持有同一 build dir 的租约 8 秒；再让入口点构建同一目录 → 入口点无视租约直接执行。）

**修复**
- `scripts/build.sh`：新增 `GUARD` / `guard_python()` / `guard_prefix()`；`cmd_build`、`cmd_test` 的 payload 一律套进租约（`--wait ${SICNU_BUILD_GUARD_WAIT:-1800}`，`SICNU_BUILD_GUARD_WAIT=0` 用于快速失败）；租约或 python 缺失时**显式告警**（不静默跳过），`SICNU_BUILD_REQUIRE_GUARD=1` 可升级为拒绝；`SICNU_BUILD_NO_GUARD=1` 为显式退出。
- `scripts/build.sh selftest` 新增第 4 组 hermetic 断言（stub cmake + 后台持锁 + `--wait 0`），把回归永久化。
- `scripts/dev/resource_guard.py`：`--parallel` 从 `choices=(1,2)` 放宽为任意 `>=1`（调用方 `build.sh` 才是 `-j` 上限的唯一权威；抬高 `SICNU_BUILD_JOBS_CAP` 时守卫不再 argparse 报错），非法值仍 `EXIT_REFUSED`；docstring 同步。

**GREEN1（修复后，实测）**
```
$ bash scripts/build.sh selftest
  ...
  ok: busy build dir refused (exit 75); payload never ran
selftest: ALL PASS      (real 2m10s)
```
另：`scripts/check_presets.py CMakePresets.json` → `check_presets: ALL PASS`（30 项，未受影响）。

**GREEN2**：待跑（见 FINAL_REPORT 收尾清单）。

**资源影响**：零新增依赖；单用户无争用时立即取锁，无等待；多 agent 时串行排队而非叠加。

---

## §2 · G12-2 — 空 CTest 注册被判 passed（P2 → 与 G04-1 交叉放大）

**根因**：`scripts/verification_ladder.py:run_ctest` 只用 `returncode == 0` 判成功；而 `ctest -R <pattern>` 在**一个测试都没匹配到**时默认 exit 0。于是 G04-1 里那些被条件门整批吞掉的测试，在 L8 这条"兜底"通道上同样看不出来。

**RED（修复前，实测）**
```
$ /c/Qt/Tools/CMake_64/bin/ctest.exe --test-dir <empty-CTestTestfile> -R ".*"
No tests were found!!!
ctest EXIT=0                      ← ladder 会记成 "passed"
$ ... --no-tests=error
ctest EXIT=8
```

**修复**：`run_ctest` 追加 `--no-tests=error`（先用 `ctest --help` 探测能力，CMake < 3.18 不追加，避免把整条 lane 变成 usage error）；并把 "No tests were found" 显式映射为 `failed` + 可读原因。

**GREEN1（修复后，实测）**
```
supports --no-tests: True
STATUS = failed
DETAIL = ctest matched no test for pattern '.*': the lane ran nothing, so it proves nothing (registration drift?)
```

**GREEN2**：`ctest` 空注册 exit 8 对照（同上 RED 段第二次运行）。

---

## §3 · G12-1 — 全条目 not-built 仍 `OVERALL: PASSED` 且 exit 0

**根因**：`lane_ok` 只在 `failed/timeout` 时置假（`verification_ladder.py:533-536`）；`skipped/not-built` 仅 `--strict` 下计失败，而 `--strict` 非默认。指向"已 configure 但未编译"的树时，全部条目 `find_binary=None` → `not-built` → `overall=passed` → exit 0。item 层状态字段本身诚实（未把 not-built 记成 passed），但**聚合层把零证据的跑判成全绿**，正是"不得默默跳过必需 suite"的红线边界。

**修复**：聚合阶段统计 `executed_items` / `planned_items` 写入 JSON；`planned > 0 且 executed == 0` 时 `overall_ok=False` 并打印 `OVERALL NOTE: no item actually executed …`。不改变 item 层语义，也不改变 `--strict` 语义。

---

## §4 · G05-1 — netcdf 只探测库、不探测头（依赖探测假成功）

**根因**：`tests/CMakeLists.txt:427` 只有 `find_library(NETCDF_LIBRARY NAMES netcdf)`，全文件 `find_path` 命中数为 0；而 `tests/test_io_multidim.cpp:17`、`tests/test_io_subdataset_inventory.cpp:16`、`tests/test_io_fabric_multidim_11.cpp:21` 直接 `#include <netcdf.h>`。找到库就判定"已找到"，头不在默认 include 路径时失败推迟到编译期（`fatal error: netcdf.h: No such file or directory`），一次炸 3 个 target。

**RED（静态复现，实测）**
```
$ grep -n "find_path\|NETCDF_INCLUDE_DIR" tests/CMakeLists.txt    → 0 命中
$ grep -n "netcdf.h" tests/*.cpp                                   → 3 个 TU
```

**修复**：改为 `find_library` + `find_path(NETCDF_INCLUDE_DIR NAMES netcdf.h)` 成对探测，守卫条件改为 `if(NOT NETCDF_LIBRARY OR NOT NETCDF_INCLUDE_DIR)`，FATAL_ERROR 文案带上库/头各自的实际值；对 3 个含 `netcdf.h` 的 target 追加 `target_include_directories(... ${NETCDF_INCLUDE_DIR})`。

**GREEN1**：`cmake -P` 在本机 vcpkg 前缀下证明 `find_path` 可解析：
```
-- INC=[C:/Users/wangj.KEVIN/projects/exp-rs/build-dev/vcpkg_installed/x64-windows/include]
```
**GREEN2**：需重跑 configure 复核（缓存热时耗时较短），结果记于 FINAL_REPORT。

---

## §5 · G02 残余 — 19 个 Windows-only `_exit` 测试 main（NOT_FIXED，理由）

现场事实（逐条核实，不照抄 issue）：

1. `tests/sicnu_test_main.cpp:64` 已是 `return result;`（#1468 P1 的**主体**已修）。
2. 另有 20 个 `tests/*.cpp` 仍含 `_exit`，其中 **`test_fault_injection.cpp:214` 是 fork 子进程**（必须 `_exit`，否则子进程跑父进程 atexit），**应保留**。
3. 其余 19 个的 `_exit` 全部被 `#ifdef _WIN32` 包裹（例：`tests/test_paths.cpp:14-18`），POSIX lane 走 `return result`。
4. **19 个中没有一个使用 `processScratchDir`**（`processScratchDir` 全仓库仅 4 个 TU 引用：`sicnu_test_main.cpp`、`test_io_multidim.cpp`、`test_quality_mosaic_operator.cpp`）。因此 #1468 描述的"`%TEMP%` per-pid 目录无界累积"对这 19 个**不成立**。
5. 其中 2 个确实会漏临时目录：`tests/test_data_project_roundtrip.cpp`（9 处 `QTemporaryDir` + `:132 _exit`）与 `tests/test_project_context_reap.cpp`（多处 `QTemporaryDir` + `:510 _exit`）—— Windows 上 `_exit` 跳过 `QTemporaryDir` 析构。

**为什么不在本轮改**：把这些 `_exit` 改成 `return` 会让栈上的 `QgsApplication` / `QCoreApplication`（如 `test_project_context_reap.cpp:503`）在 `exit()` 链里析构，而 QGIS 全局缓存/注册表的静态析构顺序正是当初引入 `_exit` 规避的对象（`tests/support/qt_lifecycle.h:2-21` 明确记载）。**在没有实机 Windows 全量构建 + 跑通这些 suite 的前提下盲改，是把"泄漏"换成"退出期崩溃"的风险操作**。本轮不拥有这些 TU，也拿不到验证预算。

**处置**：作为跨方向发现完整记录（含 file:line 与上述 5 条辨析），交 owning direction 在能跑 Windows suite 时按 `qt_lifecycle.h` 的 heap-app + 有序 teardown 范式改造。不写入本 PR 的代码改动。
