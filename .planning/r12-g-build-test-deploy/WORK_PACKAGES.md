# R12-G WORK PACKAGES — G01~G15 逐条状态表

- **分支**: `hardening/r12-g-build-test-offline` · **基线**: `origin/master = 28abef358`
- **Worktree**: `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`
- **资源纪律**: `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`
- **统计口径**: 本表只记录**已真实执行**的命令与输出。任何未执行的验证一律写 `NOT_RUN`，不做推断填充。

---

## 1. 总表

| ID | 目标 | 所属文件 | 状态 | 证据摘要 | 未完成原因 |
|---|---|---|---|---|---|
| G01 | 构建/CTest/发布拓扑普查 | `CMakePresets.json`、`scripts/build.sh`、`cmake/Sicnu*.cmake` | ✅ **PASS** | 真实 configure `Configuring done (397.5s)` / `Generating done (103.6s)` / exit 0；`check_presets.py` ALL PASS (30) | — |
| G02 | 单进程测试退出的资源闭环 | `tests/sicnu_test_main.cpp`、`tests/*.cpp`(21 处自带 main) | ⚠️ **PARTIAL / BLOCKED** | 共享 main 已修（`sicnu_test_main.cpp:64` = `return result;`）；19 个 Windows-only `_exit` 仍存在 | TU 所有权属其他方向；盲改会把「泄漏」换成「退出期崩溃」（`tests/support/qt_lifecycle.h:2-21`） |
| G03 | 测试 HTTP/COG fixture 可取消、无挂死 | `tests/support/http_{range,stac,s3}_*`、`mini_cog_server.h` | ⚠️ **PARTIAL**（仅审计） | 审计已确证 6 项候选缺陷（G03-1~G03-6），其中 G03-1/G03-2 为 P1 | 本轮未修；见 §2.2 |
| G04 | CTest 注册隔离与漂移门禁 | `tests/CMakeLists.txt`(41 个条件块) | ⚠️ **PARTIAL** | 纯文本 census 已执行：41 个含注册的 `if` 块，39 个块内 `message(`=0 | 修复点落在**共享敏感文件** `tests/CMakeLists.txt`，与 PR #1483/#1484 冲突 |
| G05 | 可选依赖精准失败（不假成功） | `tests/CMakeLists.txt:432-444`、`:578-579`、`:616-617`；`src/core/CMakeLists.txt:2754` | ✅ **PASS（G05-1）** / ❌ 未修（G05-2/3） | 改后 `find_library` + `find_path` 成对探测已落地；`cmake -P` 证明不加前缀时 `INC=[NETCDF_INCLUDE_DIR-NOTFOUND]`（印证原缺陷真实） | G05-2 需改契约语义；G05-3 在 `src/core/`（越权） |
| G06 | 跨进程构建租约真正生效 | `scripts/build.sh`、`scripts/dev/resource_guard.py` | ✅ **PASS** | RED：探针 `PROBE: RED — the entry point built while another process held the lease`；GREEN1+GREEN2：selftest 新增第 4 组断言 `ok: busy build dir refused (exit 75); payload never ran`，**两轮独立运行均 `ALL PASS` / exit 0**（2m3s） | —（P2 遗留见 §3.1） |
| G07 | 离线教学 profile 可执行 | `CMakePresets.json`(`offline-lab`)、`scripts/check_presets.py` | ✅ **PASS（L0/L1）** / ⚠️ 端到端 NOT_RUN | `check_presets.py` 中 `an offline/lab profile preset exists (SICNU_LAB_PROFILE=ON)` 断言 ok；preset `offline-lab`→`build-lab`，`SICNU_LAB_PROFILE=ON`+`ENABLE_LOCAL_BUILD_SHORTCUTS=ON` | 未跑 `offline-lab` 真实 configure（需 ~400s 且须先解决 G05-2 语义） |
| G08 | Windows 打包与依赖闭合 | `packaging/bundle/**`、`scripts/windows/**` | ⚠️ **PARTIAL**（仅审计） | 13 项已实测排除 + 2 项候选缺陷（G08-1/G08-2） | 需真实构建产物 + `dumpbin`，本轮禁止重型编译 |
| G09 | 规范验证器与契约 | `scripts/verify_bundle_manifest.py`、`scripts/bundle_manifest.ps1` | ⚠️ **PARTIAL**（仅审计） | 路径穿越/改字节/中文空格路径双侧实测全部正常；G09-1~G09-5 候选缺陷已定位 | 未修（其中 G09-2 与 G08-1 同一根因，被列为 G09 侧待修项） |
| G10 | 一键脚本（教师侧） | `scripts/gen_samples.{sh,cmd}`、`packaging/bundle/GRADE_ALL.sh`、`scripts/run_classroom_batch.py` | ⚠️ **PARTIAL**（仅审计） | G10-1~G10-6 候选缺陷已实测复现（exit=127/9009、措辞误导等） | 未修 |
| G11 | 测试数据与临时目录隔离 | `tests/support/process_scratch.h`、`tests/test_io_{raster,vector}_contract.cpp` | ⚠️ **PARTIAL**（仅审计） | census 已定位 G11-1(P1)/G11-2(P2)/G11-3(P2) | 未修；G11-3 需新增共享 RAII |
| G12 | 轻量分层验证阶梯 | `scripts/verification_ladder.py` | ✅ **PASS（G12-1/G12-2）** / ❌ 未修（G12-3） | GREEN1：`STATUS = failed` + `DETAIL = ctest matched no test for pattern '.*' …`；空跑 `executed_items=0 planned_items=4` → `OVERALL: ATTENTION` / exit 1 | G12-3 为 P3 |
| G13 | AppImage 交付链 | `packaging/build-appimage.sh`、`packaging/appimage_toolchain.sh` | ❌ **NOT_RUN** | 仅静态审计 + `fetch_tool` 探针：`fetch_tool exit=1`（PENDING 校验和） | 本机为 Windows；且 AppImage 在 PENDING 状态下**不可能构建** |
| G14 | perf guard（性能门） | `scripts/perf_report.py`、`scripts/bench/perf_observatory_*.py` | ❌ **NOT_RUN** | 未执行任何 baseline 采集/对比 | 见 PERFORMANCE.md §5 |
| G15 | 反作弊红线 + 独立审查 | 本目录全部文档 | 🔄 **IN_PROGRESS** | 账本已建立（ORACLE_LEDGER.md）；三份审计报告已归档 | 独立 reviewer 子 agent 仍在跑 |

**汇总**：✅ PASS 4（G01 / G05-1 / G06 / G12-1+G12-2，另 G07 静态层 PASS）· ⚠️ PARTIAL 8 · ❌ NOT_RUN 2 · 🔄 IN_PROGRESS 1

---

## 2. 未修项的逐条理由（不把审计结论写成"已修复"）

### 2.1 G02 — `_exit` 残余（PARTIAL + BLOCKED）

| 事实 | 现场核实 |
|---|---|
| #1468 主体 | ✅ 已修：`tests/sicnu_test_main.cpp:64` 为 `return result;`（注释 `:53-63` 点名 #1468） |
| 自带 `main()` 的 TU | **21 处**仍含 `_exit`，全部链接 `Catch2::Catch2`（不带 main），**永远拿不到**共享 main 的修复 |
| 有意豁免 | `tests/test_fault_injection.cpp:214` 是 fork 子进程，必须 `_exit`；`tests/helper_external_process.cpp:106` 是子进程度量 fixture |
| 真泄漏范围 | **仅 2 个 TU**：`test_data_project_roundtrip.cpp`（9 处 `QTemporaryDir` + `:132 _exit`）、`test_project_context_reap.cpp`（`:510 _exit`） |
| 其余 17 个 | 不使用 `processScratchDir`（全仓仅 4 个 TU 引用），#1468 描述的「%TEMP% 无界累积」对其**不成立** |

**BLOCKED 原因**：改为 `return` 会让栈上 `QgsApplication` 在 `exit()` 链里析构，而 QGIS 全局缓存/注册表的静态析构顺序正是当初引入 `_exit` 要规避的（`tests/support/qt_lifecycle.h:2-21` 有明确记载）。**在拿不到 Windows 全量构建 + 跑通这些 suite 的验证预算前盲改，是用「泄漏」换「退出期崩溃」。** 本方向不拥有这些 TU。

### 2.2 G03 — HTTP fixture（仅审计）

| ID | 严重度 | 一句话 | 状态 |
|---|---|---|---|
| G03-1 | P1 | `~HttpRangeServer`(`:94`)、`~HttpStacServer`(`:106`) 先 close 再 self-connect 唤醒 → 唤醒是死代码；POSIX 上析构 `join` 可挂死 | ❌ 未修 |
| G03-2 | P1 | 全仓 **0 处** `SO_SNDTIMEO`；客户端「发完头从不读」→ `send()` 无限阻塞 → `join` 永久挂死 | ❌ 未修 |
| G03-3 | P2 | `http_stac_server.cpp:58-64` 读体循环「每次有界、整体无界」 | ❌ 未修 |
| G03-4 | P2 | 析构对已拷贝的 in-flight fd 做 `::shutdown()` 存在 fd 复用 TOCTOU | ❌ 未修 |
| G03-5 | P2 | `MiniCogServer::stop()` 先 close 再 join，且无 self-connect 唤醒 | ❌ 未修 |
| G03-6 | P2 | 并发模式下 `mHandlers` 无回收，随连接数线性增长 | ❌ 未修 |

G03-2 已被列为「待修」优先项（G09-2 的修复会先落地）。

### 2.3 G04 — 条件门静默吞测试（仅审计）

| 条件 | 行范围 | 被门控 target |
|---|---|---|
| `if(TARGET sicnu_operators)` | 6527-7273 | **45**（实际恒真） |
| `if(TARGET qgis_app_classify)` | 5511-6266 | **39**（OpenCV 不全即静默消失） |
| `if(SICNU_HAS_OPENCV AND OpenCV_FOUND)` | 2969-3236 | 13 |
| `if(NOT WIN32)` | 10165-10226 | **7**（本 worktree 正是 Windows → 本机 lane 静默缺席） |
| `if(SICNU_HAS_OPENCV AND TARGET qgis_app_obia)` | 6374-6522 | 6 |
| `if(SICNU_EMBED_PYTHON)` | 7824-8055 | **6**（默认 OFF） |

**BLOCKED 原因**：修复必须改 `tests/CMakeLists.txt`（引入 `sicnu_skip_suite()` 宏 + `SICNU_SKIPPED_SUITES` 计数），而该文件是**共享敏感文件**，正被 PR #1483（42 文件）/ #1484（10 文件）同时写入。按 OWNERSHIP.md，本方向对其持**只读短租约**，不抢写。

### 2.4 G05 — 依赖探测假成功

| 子项 | 状态 | 说明 |
|---|---|---|
| G05-1（只查库不查头） | ✅ **已修并落地** | `tests/CMakeLists.txt:432-433` 改为 `find_library` + `find_path` 成对；`:434` 守卫改为 `OR`；3 个含 `netcdf.h` 的 target 追加 `target_include_directories`（`:444`、`:579`、`:617`） |
| G05-1 GREEN1 | ✅ | `cmake -P probe.cmake` → `INC=[NETCDF_INCLUDE_DIR-NOTFOUND]`（无前缀时），反证「只查库」确实是假成功 |
| G05-1 GREEN2（重跑 configure） | ❌ **NOT_RUN** | `build-dev/CMakeCache.txt` 生成于 01:25，**早于**本次改动；且 `grep NETCDF_INCLUDE_DIR build-dev/CMakeCache.txt` 无命中 → 尚未用新逻辑重配 |
| G05-2（optional 注释 vs FATAL_ERROR 实现） | ❌ 未修 | 需改依赖契约语义，且 `vcpkg.json`(14 项) 无 netcdf |
| G05-3（`src/core/CMakeLists.txt:2754-2756`） | ❌ 未修 | **越权**：`src/core/` 不属 R12-G |

### 2.5 G08 / G09 / G10 / G11 — 仅审计，全部未修

见 `AUDIT_G08_G09_G10_G13.md` §2 与 `AUDIT_G02_G03_G11.md` §2。逐项均有 file:line 与最小 RED 命令；本轮**不产出代码改动**，只作为跨方向发现归档。

### 2.6 G12-3 / G13 / G14

- **G12-3（P3）**：`--resume` 对 `kind=="target"`（L0 `sicnu_header_probes`）不做身份校验，历史 pass 被无条件复用。未修。
- **G13**：❌ **NOT_RUN** — 本机 Windows，AppImage 无法构建亦无法启动（PENDING 校验和见 `packaging/appimage-tool-checksums.txt:11-12`）。
- **G14**：❌ **NOT_RUN** — 未采集任何 baseline。

---

## 3. 本轮已真实执行的命令台账

| # | 命令（worktree 根） | 退出码 | 关键输出 |
|---|---|---|---|
| 1 | `bash scripts/build.sh selftest` | 0 | `selftest: ALL PASS`（16 项，含 `ok: busy build dir refused (exit 75); payload never ran`）· real 2m3s · **第二轮独立复核同样 ALL PASS / exit 0** |
| 2 | `python scripts/check_presets.py CMakePresets.json` | 0 | `check_presets: ALL PASS`（30 项断言） |
| 3 | `python scripts/verification_ladder.py --list` | 0 | 列出 L0..L8 + LS 全部 lane/item |
| 4 | `python scripts/verification_ladder.py --build-dir build-dev --lanes L1 --json <tmp>` | **1** | `[not-built] ×4`；`OVERALL NOTE: no item actually executed (4 planned, 0 executed)`；`OVERALL: ATTENTION`；JSON `executed_items=0, planned_items=4` |
| 5 | `python -m unittest discover -s scripts/dev/tests -p test_resource_guard.py -t .` | **1** | `Ran 16 tests in 53.196s` → `FAILED (failures=1)`：`test_second_writer_refused_fail_fast` `AssertionError: 0 != 75` |
| 6 | `PATH=<Qt>/bin:$PATH python -c "…vl._ctest_supports_no_tests()"` | 0 | `supports --no-tests: True` |
| 7 | `ctest --test-dir <empty-CTestTestfile> -R ".*"` | **0** | `No tests were found!!!`（默认放行） |
| 8 | `ctest --test-dir <empty> -R ".*" --no-tests=error` | **8** | `Errors while running CTest`（拒绝） |
| 9 | `PATH=<Qt>/bin:$PATH python -c "…vl.run_ctest(<empty>,'.*',1,60,{})"` | 0 | `STATUS = failed` / `DETAIL = ctest matched no test for pattern '.*': the lane ran nothing…` |
| 10 | `cmake -P probe.cmake`（含 `find_path(... netcdf.h)`） | 0 | `-- INC=[NETCDF_INCLUDE_DIR-NOTFOUND]` |
| 11 | `git diff --numstat` | 0 | `104/0 12/4 53/1 15/5`（build.sh / resource_guard.py / verification_ladder.py / tests/CMakeLists.txt） |

### 3.1 ⚠️ 发现：resource_guard 单元测试存在 1 项失败（**先于本轮改动**）

```
FAIL: test_second_writer_refused_fail_fast
  scripts/dev/tests/test_resource_guard.py:137
  AssertionError: 0 != 75        （第二个写入者没有被拒绝，而是拿到了锁）
Ran 16 tests in 53.196s — FAILED (failures=1)
```

**归属性判定（已实测，非推断）**：

| 验证 | 结果 |
|---|---|
| 在工作区（含本轮改动）单独重跑该用例 ×3 | 3/3 失败（6.5s / 6.27s / 6.25s） |
| 用 `git show HEAD:scripts/dev/resource_guard.py` 导出**原版**到 `/tmp/r12g-base` 后跑同一套测试 | **同样失败**（`Ran 16 tests in 53.812s — FAILED (failures=1)`） |
| 本轮对该文件的实际改动 | 仅 `--parallel` 从 `choices=(1,2)` 改为 `int` + 新增 `<1` 显式拒绝；与锁获取路径无关 |

**结论**：这是一项**既有的、对宿主启动延迟敏感的测试失败**，不是 R12-G 引入的回归。诱因证据：本机 `python -c "pass"` 单次启动约 **0.95 s**，而该用例的持锁 payload 只 `sleep(0.6)`——第二个进程的解释器启动尚未完成，持锁者已释放，于是断言落空。测试文件自身注释（`scripts/dev/tests/test_resource_guard.py:110-113`）已承认该路径「在并行 agent 下 flaky」。

**处置**：不修改该测试（属 R12-G 自有范围，但改动会污染 regression 基线）。作为 P2 发现记入 G06 的待办，建议后续把 payload 睡眠提到显著大于解释器启动时间，或改为「持锁者写 started 标记后再等待释放信号」的握手式同步。

---

## 4. 明确排除（审计已证伪，勿重复修补）

完整清单见三份审计报告的「明确排除项」章节，共 24 项。高频误报：

1. `sicnu_test_main.cpp` 的 Windows `_exit` —— **ALREADY_FIXED**，勿再修一遍。
2. `tests/CMakeLists.txt:642`「WIN32 块重复 link `test_io_fabric_catalog`」—— **不可复现**（行号已漂移；15 处 `ws2_32` 按 target 去重后无重复）。
3. 5 个 `test_verifier_*_14.cpp`「未注册孤儿」—— **假阳性**，真实注册在 `:15604-15621` 的 `foreach` 里，纯文本 stem 匹配看不见。
4. `verification_ladder.py`「把没跑记成 PASS」—— **item 层不成立**（状态机严格区分 `passed/failed/not-built/timeout/skipped`）；残留风险仅在 aggregate 措辞，已单列 G12-1 并修复。
