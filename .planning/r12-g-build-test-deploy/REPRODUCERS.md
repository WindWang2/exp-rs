# R12-G REPRODUCERS — RED 复现器台账

- **原则**：每条必须是**真实跑过**的命令与**真实**退出码/输出；没跑过一律 `NOT_RUN`。
- **Worktree**：`C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test` · 分支 `hardening/r12-g-build-test-offline`
- **本方向提交**：`615a59392`（构建租约 + R1 修复）、`68e4e3f20`（零证据判负）、`19c53455d`（netcdf 头探测）、`55a1a03d6`（离线交付）、`b4ecc96fa`（文档）。均已推送，PR #1498。
- **可跑 oracle**（随本 PR 提交，均在 `.planning/r12-g-build-test-deploy/`）：

| Oracle | 命令 | 断言 |
|---|---|---|
| G06 租约 | `bash .planning/r12-g-build-test-deploy/probe_g06_guard.sh` | 持锁时入口点退 75 且 payload 从未运行 |
| R1 修复 | `bash .planning/r12-g-build-test-deploy/r1fix_repro.sh` | `--jobs 8` 退 2；REQUIRE_GUARD+NO_GUARD 退 1；守卫自身 `--parallel 8` 退 2 |
| G09 逃逸 | `python .planning/r12-g-build-test-deploy/g09_green.py control\|after` | 干净包 PASS；junction/symlink 逃逸三种形态全部 FAIL |
| G13 OTB | `bash .planning/r12-g-build-test-deploy/g13_otb_harness.sh` | 空 OTB 源退 1 且不打印成功；有内容源退 0 且打印真实计数 |

> **RED 形态提醒**：R1/R4/R9/R10 的 "RED" 不是崩溃，而是**本该失败却返回 0**——静默假绿。这正是它们危险的原因，也是必须写成显式 oracle 的理由。

## R1 · G06 — 跨进程构建租约从未被接线 ✅ 已修

**命令**：`bash .planning/r12-g-build-test-deploy/probe_g06_guard.sh`（72 行，完全 hermetic）
**方法**：① 假 build 目录 + PATH 上的 stub `cmake`（打印 `PAYLOAD-RAN` 后 exit 0）；② 后台用 `resource_guard.py` 持租约；③ 入口点以 `SICNU_BUILD_GUARD_WAIT=0` 构建同一目录；④ RED = stub 跑了，GREEN = exit 75 且 stub 没跑。

**RED（修复前，实测）** exit **0（假绿）**
```
--- entry point: .../scripts/build.sh
--- exit code: 0
--- output:
PAYLOAD-RAN --build C:\...\Temp/tmp.w49Cu4yEOx/build --parallel 1
PROBE: RED — the entry point built while another process held the lease
```
**GREEN（修复后，实测）** exit **0（断言全过）**
```
  ok: busy build dir refused (exit 75); payload never ran
PROBE: GREEN — refused with EX_TEMPFAIL(75); payload never ran
```
**修复**：`scripts/build.sh`（`GUARD=`/`guard_python()`/`guard_prefix()`/`cmd_build`/`cmd_test`/selftest 第 4 组）；`scripts/dev/resource_guard.py`（`--parallel` 改 `int`、拒绝 `<1`）
**commit**：`615a59392` · **GREEN2**：✅ 已跑（`selftest: ALL PASS` / `EXIT=0`）

## R2 · G12-2 — `ctest` 空注册 exit 0（底层能力对照）✅ 已修

**命令**（空 `CTestTestfile.cmake` fixture）
```sh
/c/Qt/Tools/CMake_64/bin/ctest.exe --test-dir <dir> -R ".*"
/c/Qt/Tools/CMake_64/bin/ctest.exe --test-dir <dir> -R ".*" --no-tests=error
```
**真实输出与退出码**
```
Test project C:/Users/wangj.KEVIN/AppData/Local/Temp/g12ctest-win
No tests were found!!!
default exit=0                    ← ladder 会记成 "passed"
Errors while running CTest → no-tests=error exit=8   ← 拒绝，可被 ladder 判为 failed
```
**修复**：`scripts/verification_ladder.py` 能力探测 + `--no-tests=error` · **commit**：`68e4e3f20`

## R3 · G12-2 — ladder 的 `run_ctest` 误判 ✅ 已修

**命令**
```sh
PATH="/c/Qt/Tools/CMake_64/bin:$PATH" python -c "
import sys, pathlib; sys.path.insert(0,'scripts')
import verification_ladder as vl
print('supports --no-tests:', vl._ctest_supports_no_tests())
print(vl.run_ctest(pathlib.Path(r'<空 CTestTestfile 目录>'), '.*', 1, 60, {})[:2])"
```
**GREEN（修复后，实测）** exit **0**
```
supports --no-tests: True
('failed', "ctest matched no test for pattern '.*': the lane ran nothing, so it proves nothing (registration drift?)")
```
**RED 形态**：只判 `returncode == 0`，空匹配 exit 0 → 返回 `passed`。
**修复**：`scripts/verification_ladder.py:348-363`（能力探测）、`:386-393`（`No tests were found` 显式映射）· **commit**：`68e4e3f20`

## R4 · G12-1 — 全条目 `not-built` 仍 `OVERALL: PASSED` ✅ 已修

**命令**（`build-dev` 已 configure 未编译 → 正好是缺陷场景）
```sh
python scripts/verification_ladder.py --build-dir build-dev --lanes L1 --json /tmp/ladder-L1.json
```
**GREEN（修复后，实测）** exit **1**
```
=== L1 unit-core — observability/runtime unit suites ===
  [not-built] trace_contract (-s)
  [not-built] fault_registry (-s)
  [not-built] diagnostic_report (-s)
  [not-built] trace_chain_8 (-s)

OVERALL NOTE: no item actually executed (4 planned, 0 executed) — this run produced no evidence and is not a pass.
OVERALL: ATTENTION
EXIT=1
```
JSON：`{"schema":"exp.verification.ladder.v1","overall":"attention","executed_items":0,"planned_items":4}`
**RED 形态**：`lane_ok` 只在 `failed/timeout` 时置假 → 全 `not-built` → `overall=passed` → exit 0。
**修复**：`scripts/verification_ladder.py` 计数器 + 聚合判负。**item 层语义与 `--strict` 语义均未改动。** · **commit**：`68e4e3f20`

## R5 · G05-1 — netcdf 只探测库、不探测头 ✅ 已修

**命令**
```sh
grep -n "find_path\|NETCDF_INCLUDE_DIR" tests/CMakeLists.txt    # 修复前：0 命中
grep -n "netcdf.h" tests/*.cpp                                   # 4 个 TU 直接 include
```
**RED**：`find_path` **0 命中**，而 4 个 TU 直接 `#include <netcdf.h>`：
`tests/test_io_multidim.cpp:17`、`test_io_subdataset_inventory.cpp:16`、`test_io_fabric_multidim_11.cpp:21`、`test_io_roundtrip_matrix.cpp`（第 4 个由 R1-03 补入，见 REVIEW_R1.md）
→ 库找到即"成功"，头缺失推迟到编译期一次炸多个 target。

**修复**（`tests/CMakeLists.txt`）
```cmake
find_library(NETCDF_LIBRARY NAMES netcdf)
find_path(NETCDF_INCLUDE_DIR NAMES netcdf.h)
if(NOT NETCDF_LIBRARY OR NOT NETCDF_INCLUDE_DIR)
  message(FATAL_ERROR "netcdf is not usable: library=... header=...")
endif()
```
**commit**：`19c53455d`

## R6 · G05-1 — 头不在默认 include 路径 + GREEN2 ✅ 已闭合

**命令**
```sh
printf 'find_path(NETCDF_INCLUDE_DIR NAMES netcdf.h)\nmessage(STATUS "INC=[${NETCDF_INCLUDE_DIR}]")\n' > /tmp/g05probe/probe.cmake
/c/Qt/Tools/CMake_64/bin/cmake.exe -P /tmp/g05probe/probe.cmake
```
**RED 输出**（exit 0）：`-- INC=[NETCDF_INCLUDE_DIR-NOTFOUND]`
**旁证**：头文件物理存在 —— `C:/Users/wangj.KEVIN/projects/exp-rs/build-dev/vcpkg_installed/x64-windows/include/netcdf.h`（76361 bytes）
→ 证明「库能找到 ≠ 头能找到」，原单点 `find_library` 确实是假成功。

**GREEN2（已补，实测）**：重跑真实 configure ×2，`cmake_exit=0`，且 `NETCDF_INCLUDE_DIR` 首次进入 `CMakeCache.txt`（改动前缓存中只有 `NETCDF_LIBRARY`）。
⚠️ **过程教训**：最初那条 configure 证据是**改动前**的（`CMakeCache.txt` 生成于 01:25，早于 01:36 的改动），当时据实判为 NOT_RUN，重跑后才拿到真 GREEN。**证据的时间线必须核对，不能只看命令跑通过。**

## R7 · G02 — 20 个 TU 仍 `_exit`（静态 RED）❌ 未修

**命令**
```sh
grep -rn --include=*.cpp -E "^[[:space:]]*_exit[[:space:]]*\(" tests/ \
  | grep -v "tests/fixtures/isolation_plugin/" | grep -v "tests/helper_external_process.cpp"
# 期望：0 行    当前：20 行
```
**命中位置**（全部形如 `#ifdef _WIN32 _exit(result); #else return result;`）：
`test_accuracy_panel_wiring.cpp:19`、`test_active_view_host_data_context.cpp:100`、`test_active_view_host_viewport.cpp:21`、`test_colormap.cpp:44`、`test_data_phase1_acceptance.cpp:101`、`test_data_project_roundtrip.cpp:132`、`test_fault_injection.cpp:214`、`test_layer_adoption_safety_net.cpp:95`、`test_log_panel_main.cpp:31`、`test_paths.cpp:17`、`test_project_context_reap.cpp:510`、`test_python_engine.cpp:81`、`test_python_plugin_host.cpp:253`、`test_python_plugin_manager.cpp:99`、`test_python_processing_provider.cpp:80`、`test_raster_processing_dialog_base_main.cpp:42`、`test_vector_layer_properties_lazy.cpp:73`、`test_workflow_pipeline.cpp:16`、`test_workspace_snapshot.cpp:29`、`test_plugin_host.cpp:46`

| 分类 | 说明 |
|---|---|
| 有意豁免 | `test_fault_injection.cpp:214`（fork 子进程）、`helper_external_process.cpp:106`（子进程度量 fixture） |
| 真泄漏 | **仅 2 个**：`test_data_project_roundtrip.cpp`、`test_project_context_reap.cpp` |
| 其余 17 个 | 不使用 `processScratchDir`，#1468 的「%TEMP% 无界累积」对其不成立 |

**commit**：—（未修，属跨方向所有权，理由见 WORK_PACKAGES.md §2.1、OWNERSHIP.md §4）

## R8 · ⚠️ `test_resource_guard` 单元测试 1 项失败（既有 flaky）❌ 未修

**命令**：`python -m unittest discover -s scripts/dev/tests -p test_resource_guard.py -t .`
**输出**（exit **1**）
```
FAIL: test_second_writer_refused_fail_fast
AssertionError: 0 != 75 :
Ran 16 tests in 53.196s
FAILED (failures=1)
```
**归属性证明（已实测，非推断）**

| 实验 | 结果 |
|---|---|
| 工作区（含 R12-G 改动）单独重跑 ×3 | 失败 3/3（6.502s / 6.270s / 6.254s） |
| `git show HEAD:scripts/dev/resource_guard.py` 导出**原版**后跑同一套 | **同样失败**：`Ran 16 tests in 53.812s` / `FAILED (failures=1)`，同一断言 |

**诱因证据**：本机 `python -c "pass"` 启动 ≈ **0.95 s** > 用例 payload 的 `sleep(0.6)`。第二个进程的解释器尚未启动完，持锁者已释放 → 拿到锁 → `rc_second=0`。测试文件注释（`:110-113`）已承认该路径「在并行 agent 下 flaky」。
**处置**：❌ 未修（不污染 regression 基线），且**CI 爆破半径为零**（未注册进 ctest、无 workflow 引用）。记入 G06 的 P2 待办。

## R9 · G14 — `perf_observatory_verify.py` 的 O1/O3 真空绿 ✅ 已修

**RED 命令**（空注册构建目录）
```sh
PATH="/c/Qt/Tools/CMake_64/bin:$PATH" python scripts/bench/perf_observatory_verify.py --build <空目录>
```
**RED（修复前，实测，exit 1）**
```
PASS O1   run 1: 13/13 green (offline, fixed seed)     ← 一个测试都没跑，却报 13/13
PASS O1   run 2: 13/13 green (offline, fixed seed)
FAIL O2   0 + 0 records validate against sicnu-perf-observatory/1 — a run produced 0 / 0 records, expected 13
PASS O3   structural indicators identical across the two runs   ← 0 条 vs 0 条，空集恒等
```
**GREEN（修复后，实测）** 同一场景 → `FAIL O1`，不再出现 `13/13`；O3 先断言记录数下限。
**根因**：`:145-170` 存在**第三份独立**的 `run_ctest`（另两份在 `verification_ladder.py` 等处），没有 `--no-tests=error`；O1 打印硬编码 `13/13`；O3 在 0 vs 0 之间比较。
**只有 O2 带了 `EXPECTED_RECORDS = 13` 显式计数下限**，正确报 FAIL，因此整体 exit 为 1、假绿暂未逃逸——但 O1/O3 措辞本身误导。
**修复**：`scripts/bench/perf_observatory_verify.py`（能力探测 + `--no-tests=error` + `No tests were found` 显式映射；O1 改打印真实计数；O3 比较前断言记录数下限）· **commit**：`68e4e3f20`

## R10 · G09-1 — Windows junction 逃逸被判为包内内容 ✅ 已修（P1 安全）

**命令**：`python .planning/r12-g-build-test-deploy/g09_green.py after`
**方法**：自建夹具——真 junction `bundle/jlink → bundle 外的 outsidedir`，然后把**包外文件**按 `jlink/secret.txt` 登记进 `files[]`，且 `bytes`/`sha256` 都取自包外真实内容。这才是攻击场景；仅"junction 存在但 files[] 未登记其内容"时旧版也会 FAIL（unlisted file），**不是**有效反例。

**RED（修复前，实测）** exit **0**
```
=== HEAD (before fix) exit=0 ===
    BUNDLE VERIFY PASS ...\bundle (3 files, 0.0 MB / ceiling 250 MB)
```
**GREEN（修复后，实测）** exit **1**
```
  path escapes bundle through a link: jlink/secret.txt -> ...\outsidedir\secret.txt
  reparse point directory escapes bundle: jlink -> ...\outsidedir
link fixture: junction (Windows)
  islink=False isdir=True reparse=True     ← 根因：islink() 对 junction 返回 False
```
**根因**：junction 的重解析 tag 是 `0xA0000003`，`os.path.islink()` 不报，`os.walk` 直接下降进入指向包外的 junction，把包外文件当包内文件枚举和哈希；containment 又只做字符串比较。
**修复**：`scripts/verify_bundle_manifest.py`（prune 重解析点 + realpath 基准比对 + 显式 finding）· **commit**：`55a1a03d6`
**回归**：`tests/fixtures/bundle_manifest/conformance.py` → **18 checks, 0 failed**（改前有 1 条既有 red，见 R12）

## R11 · G09-2 — PS7 上每个正确 bundle 都被判 FAIL ✅ 已修（P1）

**命令**（同一正确 bundle，两个 PowerShell 宿主）
```sh
pwsh -NoProfile -File scripts/bundle_manifest.ps1 -Verify <bundle>
powershell -NoProfile -File scripts/bundle_manifest.ps1 -Verify <bundle>
```
**RED（修复前，实测）**：5.1 `PASS`（exit 0）/ PS7 `FAIL`（exit 1，`size_ceiling_mb must be a non-negative integer`）——**同一份正确清单结论相反**。
**根因**：`$ceiling -isnot [int]`，而 PS7 的 `ConvertFrom-Json` 把 JSON 整数解成 `Int64`。同文件 `:142` 已显式接受 `[int]/[long]`，两处不一致。受影响文件包括**随包分发给教师的** `packaging/bundle/VERIFY.ps1`。
**修复**：两处均接受 `[int]` 与 `[long]` · **commit**：`55a1a03d6`
**GREEN**：5.1 与 PS7 均 `PASS`（exit 0）。

## R12 · 审查中新发现并修复的两项 ✅ 已修

| 项 | RED（修复前，实测） | 修复 |
|---|---|---|
| `packaging/bundle/VERIFY.ps1` 抛未处理异常 | `%TEMP%` 为 8.3 短名（`WANGJ~1.KEV`）时 `Get-RelPath` 字符串前缀比较失配 → `path outside bundle root` 未捕获 | 两侧 `[IO.Path]::GetFullPath` 归一后比较 |
| 绝对路径 required 条目跨平台结论不一致 | Linux 下 `isabs('/etc/passwd')`=True → 判 unsafe；Windows 下 ntpath 返回 False → **降级**成 `required missing` | 先判盘符/UNC 形态再归一化，Windows 与 Linux 结论一致 |

第二项同时让既有一致性用例 `unsafe required absolute` 由红转绿（该条原是**既有** red，非本次引入）。
**commit**：`55a1a03d6`

## R13 · G13-2 — OTB 标为 required 却无条件打印成功 ✅ 已修（P1）

**命令**：`bash .planning/r12-g-build-test-deploy/g13_otb_harness.sh`
**RED（修复前，实测）**：空 OTB 源目录 → `OTB bundled successfully` + **exit 0**（实际复制 0 个文件）。
**GREEN（修复后，实测）**：空源 → **exit 1** 且不再打印成功；有内容源 → exit 0 且 `OTB bundled successfully (4 files)`。
**根因**：`packaging/build-appimage.sh` 注释写 `required — OTB is part of the application`，但四个 `cp` 全是 `2>/dev/null || true`，成功消息无条件打印。
**修复**：统计实际复制文件数，0 个按 required 语义退 1；GDAL（optional）改为报告真实计数而非硬失败 · **commit**：`55a1a03d6`

## R14 · R1 三个 P1 的复现 ✅ 已修

**命令**：`bash .planning/r12-g-build-test-deploy/r1fix_repro.sh`

| P1 | RED（修复前，实测） | GREEN（修复后，实测） |
|---|---|---|
| R1-01 `guard_prefix` 在进程替换里 `fail` | `SICNU_BUILD_REQUIRE_GUARD=1` 时只杀子 shell → **无租约构建仍 exit 0** | 改为命令替换在主 shell 决策 → **exit 1，payload 未跑** |
| R1-02 `--jobs` 从不进 `resolve_jobs` | `--jobs 8` 直接放行，绕过 `-j<=2` 上限 | `--jobs 8` → **exit 2 被拒**；`SICNU_BUILD_JOBS_CAP=99 --jobs 8` 仍正确放行 |
| R1-06 REQUIRE_GUARD + NO_GUARD 静默取先 | 后者静默覆盖前者 | 显式冲突 → **exit 1** |

另修 R1-07：selftest 原先用 `sleep 8` 与持锁窗口赛跑（确定性失败 `got 1`，根因是持锁者退出后守卫拿到锁却找不到 payload `cmake`），改为等待 payload 写出的 marker。
**commit**：`615a59392`

## R15 · G13-1 AppImage 校验和 ❌ BLOCKED（本机离线）

`packaging/appimage-tool-checksums.txt:11-12` 仍为 `PENDING`，`fetch_tool` 必 `return 1`；且第 36 行的返回早于 49-60 行的 `--tools-dir` 分支，**连预置工具目录也失败**，`build-appimage.sh` 当前不可能产出 AppImage。
**阻塞原因**：登记校验和需要联网抓取固定 tag 的产物并计算 sha256，本机离线无法完成。
**供有网机器执行的修复步骤**：① 取 `appimagetool` 与 `runtime` 的固定版本下载地址；② 分别计算 sha256；③ 填回 `appimage-tool-checksums.txt` 对应行；④ 把第 36 行的提前 `return 1` 移到 `--tools-dir` 分支之后，使预置路径可达；⑤ 重跑 `build-appimage.sh` 验证能产出 AppImage 并实际启动一次。

## R16 · G10 全量 ❌ NOT_RUN（本机无法执行 cmd）

`SICNU_GENERATE_SAMPLES` 不校验存在性（sh 退 127 / cmd 退 9009，掩盖明确诊断）、`gen_samples.cmd` 缺 `--out` 缺值校验（sh 版有 exit 2，孪生分叉）、`GENERATE_SAMPLES.cmd:55/56/66` 非默认 `--out` 跳过校验却打印 "verified" exit 0、`run_classroom_batch.py:254` 静默丢弃 .csv/.json 提交、`verify_lab_outputs.py:17-18` 依赖缺失抛原始 traceback、`GRADE_ALL.sh:88` 对任意非零 rc 都说 "isolated error rows"。
**未修原因**：主体在 `.cmd`/`.bat`，本机 `cmd.exe` 被宿主安全策略阻断，**改了无法验证**，按"不虚报"纪律记为 NOT_RUN 而非假装修好。

## R17 · L4 ctest ❌ BLOCKED（环境级）

`test_paths` 真实编译成功（`test_paths.exe` 2.39 MB，L3 PASS），但 ctest 运行全部退 `0xc0000135`（DLL 未找到）。已尝试三类 PATH 补全——Qt bin、vcpkg debug bin、VC DebugCRT——均无效；`dumpbin /dependents` 解码后仍未定位缺失项。**已 3 次修复尝试，停止猜测，记为环境级 BLOCKED**，不谎报为测试失败。
