# R15-E BASELINE — 实时事实冻结

- 冻结时间：2026-10-17 01:15 GMT+8（2026-10-16 17:15 UTC）
- 轨道分支：`hardening/r15-E-desktop-teaching-help`
- 本轨 worktree：`exp-rs-r15-e-20261017`（相对 projects 目录，绝对路径见 FINAL_REPORT）
- BASE_SHA（origin/master）：`46791bc65c6f552ebbcf723bb91892736d56f319`
- 创建时 HEAD：同 BASE_SHA（干净工作区）

## 1. 与任务书 2026-10-16 快照的重大偏差（必须记录）

任务书写于 #1512 仍 OPEN 时。实际执行时：

| 任务书快照 | 实时事实 | 影响 |
|---|---|---|
| #1512 OPEN（Qt/QGIS/MapSpec/i18n，31 文件） | **#1512 已 MERGED** 于 2026-10-16T02:35:45Z（merge commit `f7693fce1`） | 任务书「禁止抢改 31 文件」的锁定已失效；按规则改为「先完整读 diff 与测试，重新判断真正残留」 |
| #1505 OPEN（security/concurrency） | **已 MERGED** 2026-10-16T02:58:58Z（`871f963cf`） | 不再冲突 |
| #1509 OPEN（JsonCpp typed safety） | **已 MERGED** 2026-10-16T02:59:21Z（`46791bc65`，即当前 master HEAD） | 不再冲突 |
| origin/master `8b856721…` | **实际为 `46791bc65…`** | 基线整体后移 |
| 3 open PR | **2 open PR**：#1514（r15-A）、#1513（r15-B，Draft） | 冲突面缩小 |

结论：本轨的排他边界需按实时状态重算，见 `OWNERSHIP.md`。

## 2. master 最近合并序列（自上而下最新）

| SHA | PR | 标题 | 合并时间 |
|---|---|---|---|
| `46791bc65` | #1509 | fix(json): harden JsonCpp type safety boundaries (#1447,#1488) | 2026-10-16T02:59:21Z |
| `871f963cf` | #1505 | fix(security,concurrency): SSRF/temporal cube/python pool/GPU/sci broker | 2026-10-16T02:58:58Z |
| `f7693fce1` | #1512 | hardening(r14-d): Qt/QGIS 工作台、图层编辑交互与 MapSpec 制图质量收敛 | 2026-10-16T02:35:45Z |
| — | #1511 | hardening(r14-e): 本科遥感实验教学、自动评测与科学证据链收敛 | 2026-10-15T15:43:11Z |
| — | #1510 | hardening(r13-l): workbench docks thread reaper, matrix table bounding, pipeline durability | 2026-10-15T15:41:48Z |
| — | #1508 | hardening(r14-b): 模型推理/GPU/分块流水线 | 2026-10-15T15:40:14Z |
| — | #1507 | hardening(r14-c): Pi/MCP harness / workflow | 2026-10-15T15:38:52Z |
| — | #1506 | hardening(r14-a): 遥感科学算法 | 2026-10-15T15:37:28Z |
| — | #1504 | R12-B geodata durability | 2026-10-15T15:35:52Z |

## 3. OPEN PR（2）

### #1514 · `hardening/r15-A-science-processing`（非 Draft，updated 2026-10-16T15:00Z）
head `7fc4382fe880cc9d59848ef3e6e3d16c0cdf589a`。生产文件：
- `src/analysis/segmentation/rs_multires_segmenter.cpp`
- `src/operators/rs/rs_sam_classify_operator.cpp`
- `src/processing/algorithms/image_fusion.cpp`
- tests: `test_image_fusion.cpp`, `test_obia_segmentation.cpp`, `test_spectral_classification.cpp`

→ 全部为算法层，**与本轨无文件重叠**。本轨只读，不触碰。

### #1513 · `hardening/r15-B-geodata-persistence`（**Draft**，updated 2026-10-16T17:15Z）
head `53941117b86399916241c76d53aabe619241e3ef`。生产文件集中在：
`src/cli/cli_commands.cpp`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/geospatial/**`（含 `atomic_fs.*`、`resource_uri.*`、`vector_reader.cpp`、`raster_reader.cpp`、`offline_gate.*`、`http_fetch.cpp` 等）、tests/CMakeLists.txt 与多个 `test_r15_B_*`。

→ 与本轨核心目录（src/app、src/gui、src/ui、src/help、src/teaching*、src/lab*）**无文件重叠**，但 `tests/CMakeLists.txt` 是本轨也需要动的文件 → 按单文件短租纪律处理。

## 4. OPEN ISSUES（25，状态 ≠ 事实）

| # | 标题摘要 | 本轨相关性 | 初判归类 |
|---|---|---|---|
| 1499 | python_worker_process_pool 多余 `}`、acquireWorker 超时不可达 | 无关（python 层） | OUT_OF_SCOPE |
| 1495 | CI clang-tidy continue-on-error | 无关（CI） | OUT_OF_SCOPE（不碰 Actions） |
| 1494 | 孤儿测试 `test_challenger_m1_2_harness.cpp` 未注册 | 弱相关（测试注册） | 待复核，若与 UI 无关则 OUT_OF_SCOPE |
| 1491/1490 | CI 无结果 / concurrency 组替换 | 无关 | OUT_OF_SCOPE |
| 1489 | lupdate 只扫 src/app，zh_CN.ts 缺大量 tr() | **E12 相关**；但 #1512 已改 `resources/translations/sicnu_zh_CN.ts` 与 `test_i18n*` | 需实测残留，不预先改 .ts |
| 1488 | canonical_json asInt64 抛 LogicError | 已被 #1509 承接 | COVERED_BY_MERGED_PR |
| 1486 | worker pool 数据竞争 | 已被 #1505 承接 | COVERED_BY_MERGED_PR |
| 1485 | SSRF 内网判定可绕过 | 已被 #1505 承接 | COVERED_BY_MERGED_PR |
| 1468 | Windows injected main _exit / fixture server 无超时 / netcdf | 弱 | 待复核 |
| 1467 | core-fork local-modification cluster | 无关（D/算法轨） | OUT_OF_SCOPE |
| 1466 | 教学脚本路径 / grader / discovery / 非原子写 / 无界输出 | **E08/E09 核心线索** | 报告已确认修复 → 需回归验证 |
| 1465 | help terminology / std::equal 越界 / 去重后缀未应用 | **E10 核心线索** | 报告已确认修复 → 需回归验证 |
| 1464 | recordedAtUtc 字典序比较 / 可选参数忽略 | 弱（verify 层） | 待复核 |
| 1463 | agent_ops journal/bench/recipes | D 轨 | OUT_OF_SCOPE |
| 1462 | python/plugins/platform | 无关 | OUT_OF_SCOPE |
| 1460 | cli/sdk external_process | 无关 | OUT_OF_SCOPE |
| 1459 | workflow/runtime | C 轨 | OUT_OF_SCOPE |
| 1458 | experiment portable bundle / studio version gate | 弱相关（studio） | 待复核 |
| 1456/1455/1454/1449/1448/1447 | geospatial / operators / task-center / concurrency / stores / json | 分别由 #1504/#1505/#1509 承接 | COVERED_BY_MERGED_PR |

> 归类最终以实证为准；本文件在 M4 前会随证据更新。

## 5. 本地并行轨道（同一台机器，资源共享）

| worktree | 分支 | HEAD |
|---|---|---|
| `exp-rs` | master | `46791bc65`（与 origin/master 一致） |
| `exp-rs-r10-algorithms` | hardening/r10-algorithm-processing-quality | `8616585e7` |
| `exp-rs-r15-a-20261016` | hardening/r15-A-science-processing | `7fc4382fe` |
| `exp-rs-r15-c-20261016` | hardening/r15-C-model-runtime | `46791bc65` |
| `exp-rs-r15-d-20261017` | hardening/r15-D-pi-mcp-workflow | `46791bc65` |

→ **C 轨与 D 轨刚创建、尚无提交**；A 轨已提交。本轨构建必须独立 build 树、`-j2`、测试 `-j1`。

## 6. 本机构建环境（实测）

- MSVC `14.38.33130`（VS2022 Community），Windows SDK `10.0.22621.0`
- Qt `6.8.0 msvc2022_64` @ `C:/deps/Qt/6.8.0/msvc2022_64`（**注意：不是 C:/Qt**）
- CMake @ `C:/Qt/Tools/CMake_64/bin/cmake.exe`，Ninja @ `C:/Qt/Tools/Ninja/ninja.exe`
- 依赖前缀：`C:/deps/qca-install`、`C:/deps/kc-install`、`C:/deps/vcpkg/installed/x64-windows`、`C:/deps/qscintilla-install`
- Catch2 源：`C:/deps/catch2-src`

### 已实测环境地雷（本轨新增）

| # | 症状 | 根因 | 处置 |
|---|---|---|---|
| 1 | `vcpkg install failed … Access is denied` on `vcpkg-running.lock` | 开始时并行轨道的 vcpkg 进程**正持有**锁（`mv` 报 `Device or resource busy` 证实是 live process）。**执行中该锁已释放**（再次 `mv` 成功） | 起步阶段先用「不挂工具链 + `CMAKE_PREFIX_PATH`」绕行；锁释放后改为**标准 vcpkg 工具链**（见下） |
| 2 | `CMAKE_CXX_COMPILER not set` | 未载入 MSVC 环境 | configure 脚本内显式设置 VS/SDK 变量 |
| 3 | `rc /fo … no such file or directory` / `LNK1104 kernel32.lib` | 沙箱**拦截 `reg.exe`**（`vcvars64.bat` 内部调用），Windows SDK 与 `LIB` 半缺失 | 不用 `vcvars64.bat`，在 `env_r15e.cmd` 里直接拼 `PATH/INCLUDE/LIB`（VS 14.38 + SDK 10.0.22621.0） |
| 4 | `CMake unable to find Ninja` | `cmd //c` 下 PATH 传递不稳 | 显式传 `-DCMAKE_MAKE_PROGRAM` |
| 5 | `json/json.h: No such file or directory` | 绕过工具链时 `find_package(jsoncpp)` 退化为裸库名（无 include/lib 路径） | 保留 `INCLUDE`/`LIB` 兜底；**最终**改为标准工具链后由 vcpkg 正常解析 |
| 6 | `opencv2/core.hpp: No such file or directory`（`sicnu_processing`） | 绕过工具链时 OpenCV 缺失；而 `src/processing/algorithms/ir_mad_kernels.cpp` 被**无条件**加入 `sicnu_processing` 源列表，opencv 的 include/link 却 gate 在 `if(OpenCV_FOUND)`（`src/processing/CMakeLists.txt:331`） | 先试 `-DOpenCV_DIR` → vcpkg 的 `OpenCVConfig.cmake` 需要工具链变量（`Can not find target to add properties to: opencv_core`）→ **最终改用标准 vcpkg 工具链** |
| 7 | `resources/icons/Fill_NoD_t_.svg missing`，`qrc_icons.cpp` 无法生成 | `resources/icons` 是 git 符号链接（blob 内容 `../docs/design/ui/svg-icons/icons`）；本机 `core.symlinks=false`，`git worktree add` 把它检成内容为该路径文本的**普通文件** | 在本 worktree 内 `ln -s` 还原为真实符号链接（目标串与 blob 完全一致，`git status` 保持干净，不产生提交差异） |
| 8 | Bash → `cmd //c` 桥接批处理时出现 `'m' is not recognized` 且环境变量丢失 | Git Bash 的参数/环境转换破坏批处理上下文 | 测试统一走 PowerShell runner（`run_r15e.ps1`），并把 DLL 搜索路径显式拼进 `PATH` |
| 9 | vcpkg 工具链需要可写的 `VCPKG_INSTALLED_DIR`，而主 worktree 的那份是**共享资源** | 并行轨道纪律要求共享文件的写操作串行 | 把主 worktree 已装好的 `vcpkg_installed`（3.0 GB，含 opencv4 / jsoncpp）复制到本轨 `build-dev/`，`VCPKG_INSTALLED_DIR` 指向本轨副本 → 完全隔离，不写别人的树 |

> ⚠️ 第 6 条同时是一个**独立的构建配置缺陷**（不在本轨所有权内）：当 OpenCV 不可用时，
> `sicnu_processing` 仍会编译 `ir_mad_kernels.cpp`，导致整个库不可构建。证据保留在此供对应轨道参考；
> 本轨未修改 `src/processing/**`。
