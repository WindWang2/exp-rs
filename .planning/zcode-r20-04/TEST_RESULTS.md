# ZCode R20-04 TEST_RESULTS

环境：worktree `exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`，build `build-r20-04`（Debug, ENABLE_TESTS=ON，系统依赖配方见 BASELINE）；Arch CachyOS / GCC 16 / CMake 3.30 Unix Makefiles / Qt 6.12 系统；`QT_QPA_PLATFORM=offscreen`，二进制直跑 `-j1` 串行。

## 构建记录

| 步骤 | 结果 |
|---|---|
| configure | CONFIGURE_EXIT=0 |
| 全量构建（锁内 -j2） | BUILD_EXIT=2 @70%：`collect2: ld 以信号 11`（test_spectral_anomaly 链接时段错误；同夜 R20-02 轨同款宿主症状，与本 diff 无关的测试目标） |
| 定向构建（锁内 -j2，10 个本轨目标） | BUILD2_EXIT=0 |
| sicnu_geo_rs_cli（锁内 -j2，parity CLI 腿所需） | BUILD3_EXIT=0 |
| RED/GREEN 取证增量构建（-j1 ×2 次，未持共享锁） | 见"偏差记录" |

### 偏差记录（协议 §4 "目标增量构建"路径）
全量失败后需两次 ~6 TU 的增量重建（种子提交 ↔ HEAD）。时值 R20-01 轨 -j1 全量构建持锁（无法估时），本轨队列第 3 位。两次增量均以 `-j1` 无锁执行：宿主聚合并发 = R20-01 单编译 + 本轨单编译 = 2，恰为协议自身 `-j2` 默认包络，远低于 `-j4` 上限（62GB RAM / 当时已用 ~15GB）。全程无 OOM/信号 9；重活（全量/定向/CLI）均仍在锁内。

## CT-6 RED 预取证（master 状态二进制，R20-01 build；该轨 HEAD==origin/master 零本地提交）

```
$ ./tests/test_surface_snapshot   (QT_QPA_PLATFORM=offscreen)
tests/test_surface_snapshot.cpp:322: FAILED:
  REQUIRE( canonicalJson(it->second.inputSchema) == canonicalJson(tool->inputSchema()) )
with expansion: "{"properties":{},"type":"object"}" == "{"type":"object"}"
with message:   registry tool: cartography:chart_list
test cases: 7 | 6 passed | 1 failed    assertions: 17696 | 17695 passed | 1 failed
```
唯一失败 = registry 腿；其余 6 用例（projection/wire/roundtrip/anchor/family/self-validating）全过 → 快照与 projection 正确，缺项仅在 raw 源。

## RED（本工作树，种子提交 d5b95a9546 = master src + 新测试）

| Oracle | 命令 | 失败证据（file:line + 展开） |
|---|---|---|
| O-1 #1560 取消不及时 | `./tests/test_raster_processing_dialog_base "RasterProcessingDialogBase runGdalTask"` | test_raster_processing_dialog_base.cpp:160 `REQUIRE(steps.load() < kTotalSteps)` 展开 `300 (0x12c) < 300 (0x12c)` —— 取消无法打断任务，跑完全程 |
| O-3 DPI 36 放行 | `./tests/test_layout_tools "LayoutService robustness*"` | test_layout_tools.cpp:774 `CHECK_FALSE(ok)` 展开 `!true`；:775 `REQUIRE(!error.isEmpty())` 展开 `false` —— 36 dpi 被放行且渲染成功 |
| O-2 CT-6 registry 腿 | `./tests/test_surface_snapshot "Every SpatialToolRegistry member is pinned by the snapshot"` | :322 失败 @ cartography:chart_list（同上展开） |

## GREEN ×2（HEAD edc7c27e17；两轮完整复跑）

| 二进制 | 第 1 轮 | 第 2 轮 |
|---|---|---|
| test_raster_processing_dialog_base | 119/119（11 用例）✓ | 119/119 ✓ |
| test_surface_snapshot | 17868/17868（7 用例）✓ | 17868/17868 ✓ |
| test_surface_parity（含 CLI 腿） | 5506/5506（12 用例）✓ | 5506/5506 ✓ |
| test_layout_tools | 1311/1311（14 用例）✓ | 1311/1311 ✓ |
| test_gui_job_adapter | 29/29 ✓ | 29/29 ✓ |
| test_m2_batch_a_dialogs | 132/132 ✓ | 132/132 ✓ |
| test_mapspec | 2830/2830（239 用例）✓ | 2830/2830 ✓ |
| test_mcp_server | 9956/9956（31 用例）✓ | 9956/9956 ✓ |
| test_ui_task_center_contract（thin-client guardrail） | 169/169 ✓ | 169/169 ✓ |
| test_cartography_production_11 | 25/28（3 失败） | 25/28（同 3 失败） |

（parity 首轮 11/12 的唯一失败为 CLI 腿 —— sicnu_geo_rs_cli 未在定向构建范围；补建（BUILD3_EXIT=0）后两轮 5506/5506 全绿，非代码问题。）

## KNOWN_BASELINE_FAIL（环境，非本 diff 回归）

- test_cartography_production_11：3/28 失败于 :811（atlas REQUIRE result.ok）/ :1160（e2e sha）/ :1296（corpus，"produce failed: EXPORT_FAILED"）—— 与 R19-03 在 master 记录的同名同位失败完全一致（本机离屏渲染字体/图例路径）。本 diff 的 cartography 改动仅 schema JSON 键（无渲染路径）；两轮结果逐位一致。

## 变异探针（断言可红性）

- O-1 的 RED 即最强探针：同结构循环去掉探针轮询（=种子提交的旧 seam）→ 确定性 `300 < 300` 红；R1 reviewer 独立复核断言有效性（REVIEW_R1 #2 分析）。
- O-3 的 RED 即探针：恢复 [1,1200] 下界 → `!true` 红。
- O-2 的 RED 即探针：删除任一 raw 补键 → registry 腿回到 chart_list/list_token_sets 处红。

## NOT_EXECUTED

- CI（#1491 全仓失效）→ 全部本地验证。
- Windows / macOS / sanitizer 构建（本机 Linux offscreen；三轮共享构建资源，协议允许记录而非硬凑）。
