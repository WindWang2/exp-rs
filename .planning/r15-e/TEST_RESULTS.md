# R15-E TEST RESULTS

## 0. 环境与命令（可复现）

| 项 | 值 |
|---|---|
| OS | Windows (win32) x64 |
| 编译器 | MSVC 19.38.33130（VS2022 Community） |
| SDK | Windows SDK 10.0.22621.0 |
| Qt | 6.8.0 msvc2022_64（`C:/deps/Qt/6.8.0/msvc2022_64`） |
| 构建系统 | CMake 3.29 @ `C:/Qt/Tools/CMake_64` + Ninja @ `C:/Qt/Tools/Ninja` |
| 依赖 | 标准 vcpkg 工具链，`VCPKG_INSTALLED_DIR=build-dev/vcpkg_installed`（本轨私有副本，3.0 GB，含 opencv4 4.12.0 / jsoncpp / GDAL 3.12.4 / PROJ 9.8.1 / GEOS 3.14.1 / Protobuf 33.4.0） |
| 构建树 | `build-dev/`（本轨独立，不与其他 worktree 共用 CMakeCache/ninja） |
| 并行度 | 编译 `-j2`（`CMAKE_BUILD_PARALLEL_LEVEL=2`）；测试串行 `CTEST_PARALLEL_LEVEL=1` |
| 测试平台 | `QT_QPA_PLATFORM=offscreen` |

- 配置：`cmd /c configure_r15e.cmd` → `EXIT=0`（`Configuring done (620.4s)` / `Generating done (325.7s)`）
- 构建：`cmd /c build_r15e.cmd <targets…>`
  - 轻量批：`BUILD_EXIT=0`，`[1958/1958]`，0 error
  - 重目标批（shell 闭包）：`BUILD_EXIT=0`，`[892/892]`，0 error
- 运行：PowerShell `Start-Process <exe> -Wait -RedirectStandardOutput/Error`，记录 `EXITCODE`
  - **本轨未使用 CTest 驱动**（`sicnu_discover_tests` 已注册目标，但直接执行二进制才能给出确定的 exit code 与断言数），因此不声称「CTest 全绿」。

## 1. 两轮全量结果（逐字一致 → 可重复）

| Round | 目标 | exit | 结果 |
|---|---|---|---|
| 1 & 2 | `test_r15_E_result_artifact_honesty` | 0 | All tests passed (44 assertions in 4 test cases) |
| 1 & 2 | `test_r15_E_progress_text` | 0 | All tests passed (13 assertions in 4 test cases) |
| 1 & 2 | `test_r15_E_teaching_stats_honesty` | 0 | All tests passed (33 assertions in 6 test cases) |
| 1 & 2 | `test_r15_E_lab_pack_bounds` | 0 | All tests passed (8 assertions in 3 test cases) |
| 1 & 2 | `test_r15_E_batch_refusal_accounting` | 0 | All tests passed (62 assertions in 9 test cases) |
| 1 & 2 | `test_r15_E_task_panel_state` | 0 | All tests passed (7 assertions in 2 test cases) |
| 1 & 2 | `test_rs_result_summary`（既有，本轨改过 fixture） | 0 | All tests passed (29 assertions in 3 test cases) |
| 1 & 2 | `test_workflow_session_controller`（既有） | 0 | All tests passed (35 assertions in 5 test cases) |
| 1 & 2 | `test_teaching_lab_cockpit`（既有） | 0 | All tests passed (185 assertions in 23 test cases) |
| 1 & 2 | `test_teaching_batch_discovery_r10`（既有） | 42 | 93 assertions / 92 passed / **1 failed**（PRE_EXISTING_ENV，见 §3） |
| 1 & 2 | `test_teaching_admin_core`（既有） | 42 | 3423 assertions / 3421 passed / **2 failed**（PRE_EXISTING_ENV，见 §3） |
| 1 & 2 | `test_lab_data_pack`（既有） | 42 | 180 assertions / 179 passed / **1 failed**（PRE_EXISTING_ENV；修复前为 **2** failed） |
| 1 & 2 | `test_help_coverage`（既有） | 42 | 14700 assertions / 14679 passed / **21 failed**（PRE_EXISTING_ENV，见 §3） |

汇总：**6 个本轨新增/改动目标 + 3 个既有回归目标全绿（9/9）**；4 个既有目标有环境性失败，逐条 A/B 见 §3。

## 2. RED → GREEN

### 2.1 `test_r15_E_lab_pack_bounds`（E09）
修复前（第一次运行）：
```
EXITCODE=42
test cases: 3 | 2 passed | 1 failed
assertions: 7 | 6 passed | 1 failed
FAILED: tests/test_r15_E_lab_pack_bounds.cpp(48)
  due to unexpected exception with message:
    Exceeded stackLimit in readValue().
```
→ **RED 实锤**：`Json::Exception` 从 `PackVerifier::loadFromBytes()` 逃逸，而非声明中的 typed refusal。
修复后：`All tests passed (8 assertions in 3 test cases)`。

### 2.2 `test_r15_E_batch_refusal_accounting`（E08/E09）
首次运行（修复 P1 双计之前）：`9 cases | 8 passed | 1 failed`，`CHECK(attributed)` 为 false —— 暴露的是**测试自身**对 `studentId` 的假设错误（`studentId` 是磁盘上的目录名，含前导点），已按「目录名逐字」修正断言并加注释说明。
修复 P1 后两轮均：`All tests passed (62 assertions in 9 test cases)`。

### 2.3 其余新目标
见 §1。`test_r15_E_result_artifact_honesty` 在修复前对应用例为红（缺失产物 enabled + 发出 `openPathRequested`；`status=failed` 仍渲染 `✓`）。

### 2.4 真实缺陷被**既有**测试验证
`test_lab_data_pack` 的 `lab pack: pack-relative inputs must stay under the verification root`：
- 修复前：`11 passed / 2 failed`，其中 line 417 为
  `REQUIRE( v.issues.at(0)["code"] == "lab.pack_input_outside_root" )` → 实得 `"lab.pack_input_missing"`。
  根因：Windows 上 `std::filesystem::path("/etc/passwd").is_absolute()` 为 **false**（有 root directory、无 root name），
  于是 POSIX 绝对路径绕过词法检查，最后被报成「缺失」。
- 修复（`src/lab_pack/lab_pack.cpp` 增加前导分隔符的词法拒绝）后：`12 passed / 1 failed`，
  剩余 1 项为 `REQUIRE(started)`（依赖 `gen_lab_packs.py` 的 pack 同步检查），与本改动无关。

## 3. 既有失败的 A/B 归因（PRE_EXISTING_ENV）

每个失败都用**未改动的 master 版本代码**在同一台机器上重跑同一断言，确认为环境既有问题，而非本轨回归。

| 目标 | 失败点 | A/B 做法 | A/B 结果 | 结论 |
|---|---|---|---|---|
| `test_teaching_batch_discovery_r10` | `(329) REQUIRE(items.size() == 1)` → 实得 4 | 把 `src/teaching_admin/batch_assessment.cpp/h` 恢复为 master 版本（`git checkout 46791bc -- …`），重建后运行 | **同一断言同样失败**（42，93/92p/1f） | 与 `QFile::link()` 在本机创建硬链接/快捷方式而非符号链接有关；与发现逻辑无关 |
| `test_teaching_admin_core` | `(516) grader_crashed`、`(1619) check.ran` | 同上（恢复 master 的 `batch_assessment.*`） | **同样 2 处失败**（42，3423/3421p/2f） | 与 grader CLI 子进程 / release preflight 的运行环境有关 |
| `test_lab_data_pack` | `(603) REQUIRE(started)` | 用主工作区 2026-10-09 的旧 master 二进制 `exp-rs/build-dev/test_lab_data_pack.exe` | **同样 (417)+(603) 两处失败** | (417) 已由本轨修复；(603) 是 `data/labs/packs/grading_corpus.pack.json` 与 `gen_lab_packs.py` 的 drift（已知技术债） |
| `test_help_coverage` | 3 cases / 21 assertions，例如 `(187) knowledge id: command.rs.bandMath` | 把 `src/help/operator_help_provider.cpp` 恢复为 master 版本重建后运行 | **同样 3 cases / 21 assertions 失败** | 8 个 `command.rs.*` 命令知识缺失，与本轨无关。**关键**：本轨新增的 `algorithm keyParameter` 校验在这些运行中产生 **0** 条报错（日志中 `grep -c "algorithm keyParameter"` = 0），即 84 个 keyParameters 全部能在 schema 中解析 |

> 上述 A/B 的原始日志：`ab-master.log` / `ab-admin.log` / `ab-help.log`（本地，不提交）。

## 4. 既有测试的改动（诚实说明）

`tests/test_rs_result_summary.cpp` 首个用例原先写入 `"/tmp/fused.tif"`（**不存在**的路径）并断言「双击它必须发出 `openPathRequested`」。
这条断言恰好固化了本 PR 要修的缺陷，因此改为在 `QTemporaryDir` 中写**真实文件**再断言，并在文件头写明原因。
缺失产物的行为由新增的 `test_r15_E_result_artifact_honesty` 以更强断言（disabled + 空 `UserRole` + 不发信号）覆盖。
改动后该文件仍 3 cases / 29 assertions 全绿。

## 5. NOT VERIFIED（明确列出，不声称完成）

1. **真机 / 真实交互**：所有 GUI 断言均在 `offscreen` 下完成，未做人工点击、DPI 缩放、主题切换的真实窗口验证。
2. **其它平台**：未在 Linux / macOS 上构建或运行；`makeHidden()` 在非 Windows 依赖点号前缀，Windows 依赖 `FILE_ATTRIBUTE_HIDDEN`，两条路径未在异平台实测。
3. **性能**（E16）：未采集启动/大栅格/滚动/筛选的 p50/p95/RSS 基线，`PERFORMANCE.md` 因此**不提供任何数字**。
4. **离线机房**（E15）：未在无网络、无 Windows 域环境的实训机房验证离线实验包。
5. **CI**：按任务书不等待远端 CI，也未以 CI 结果作为通过口径。
6. **`test_help_coverage` 的运行时 schema 一致性**：本轨的 keyParameters 校验依赖运行时算子注册表；已确认它在本次运行中未报错，但未逐一断言 84 个 id 的解析过程（另有离线脚本比对：drift = 0）。
