# R21-D BASELINE（LIVE 盘点，2026-10-24 CST）

## 基线事实

- 执行时刻 `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（本地 fetch 与 GitHub API 双确认；API: `repos/WindWang2/exp-rs/branches/master`）。
- Open PR = 0（`gh pr list --state open` 与 API 双确认）。最近合并：R20 五轨 #1668/#1669/#1670/#1671/#1672（2026-10-23）。
- Open Issues = 126（多页枚举入 `/tmp/r21d-issues.txt`，全部 needs-triage）。
- 本地 `master` 工作树停在 28e1891390（落后 59），未触碰、未 stash/reset。
- 兄弟轨 worktree 在场：R21-A（19d31917d0）、R21-B（cd4a10eeaf）、R21-C（142f11cb9e）；旧 r20-* 分支相对 master 仅 behind（不作为基座）。
- 本轨 worktree：`/home/kevin/project/exp-rs-r21-D-20261023T214750Z`，分支 `agent/r21-d-qt-qgis-cartography-20261023T214750Z`，基座 911e0b17a3。
- 网络：`git fetch` 间歇 SSL/连接失败，`gh` API 稳定；基线以 API 复核为准。

## 机器/构建

- 16 核 / 62GB（共享五轨）。本轨 `build-r21-D`（Unix Makefiles, Debug, ENABLE_TESTS=ON）。
- 依赖路径沿用兄弟轨 vendored 前缀 `/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr`（GDAL/PROJ/GEOS/GSL/netcdf）+ 系统 Qt6/opencv5。
- 编译纪律：`flock -x /tmp/exp-rs-r21-build.lock` + `-j2`；`QT_QPA_PLATFORM=offscreen`；`CTEST_PARALLEL_LEVEL=1`。

## R20-04（前同域轨）交付与遗留（读 PR #1672 + .planning/zcode-r20-04）

已交付（master 在位，需回归）：
1. #1560 GDAL 取消探针：`gdal_cancel_probe.*` seam + `raster_processing_dialog_base` 的 `gdalCancelProgress`/`jobCancelInFlight`；取消中允许关窗。
2. 7 个 cartography 工具 raw schema 规范化（registry 腿断言）。
3. 导出 DPI 下界统一 [72,1200]（layout seam + GUI 对话框 min）。

遗留（R20-04 FINAL_REPORT 明示）：
- **W1b**：属性表编辑路径绕过 DataManager Edit Lease（`src/app/qgis_app_facade.cpp:181-287` 直连 commitChanges/startEditing/rollBack；`qgsattributetabledialog.cpp:299/:706/:711/:930` 经 facade）。本轮 live 复核：**仍在**（见 ISSUE_TRIAGE）。
- #1655：`test_cartography_production_11` 本机离屏 3/28 红（:811/:1160/:1296），待本轮以同门复跑分类。
- #1623：`sicnu_zh_CN.qm` 无 install 规则（共享根 CMakeLists 灰区）。
- #1621/#1622：widget/dialog 测试恒真断言与墙钟门槛。
- #1632 条目4：gui_job_adapter 三重 submit 样板（P3）。

## R21-D 盘点结论（126 open issues 快速分诊）

D 轨白名单内候选经源码实证（基线 911e0b17a3）：

| Issue | 判定 | 证据 |
|---|---|---|
| #1535 P1 runner feedback UAF | **FIXED-OPEN** | `async_algorithm_runner.cpp:58` feedback->setParent(m_task) 所有权移交 |
| #1536 P1 guided workflow 行号 | **FIXED-OPEN** | `guided_workflow_widget.cpp:178-181` kLoadErrorEntryRows 固定 1 行（注释引 #1536） |
| #1537 P1 STAC searchNext 代数 | **FIXED-OPEN** | `stac_client.cpp:225-230` lastPageGeneration 守卫（F-02 注释） |
| #1539 P1 审批门空转（agent_ops） | OUT（src/agent/agent_ops 非 D 白名单） | — |
| #1559 P2 workspaceService 悬空 | **FIXED-OPEN** | `main_window_docks.cpp:758-769` destroyed 清理 seam（注释引 #1559） |
| #1560 P2 GDAL 取消 | **FIXED**（R20-04 #1672） | raster_processing_dialog_base.cpp:381+ |
| #1561 P2 光谱剖面 GUI 线程同步 | **FIXED-OPEN** | spectral_profile_widget.cpp:207-233 网络源走 QtConcurrent+代数守卫 |
| #1562 P2 Level 过滤空 lambda | **FIXED-OPEN** | log_panel.cpp:192 refilter + :181 可见计数 |
| #1563 P2 CSV 转义 | **FIXED-OPEN** | rs_accuracy_panel.cpp:30-43 RFC4180 |
| #1564 P2 dwell 节流绕过 | **FIXED-OPEN** | va_cursor_probe.cpp:74-86 startPending 内节流+重臂 |
| #1565 P2 ladderLabelsZh 上界 | OUT（src/app/teaching/** 教学排除区） | — |
| #1557 P1 macOS 通知 UAF | OUT（src/native） | — |
| #1558 P1 恒真断言（e2e recipe/task） | OUT（处理域测试）→ CROSS_TRACK | — |
| #1619/#1620 P2 src/gui 弱桩/registrygui | 候选（src/gui 在 D 白名单，vendored 基线风险高） | 待评估 |
| #1621/#1622 P2 测试恒真/墙钟 | **候选本轨**（widget/dialog 测试） | 待修 |
| #1623 P2 .qm 无 install | **候选本轨**（append-only 根 CMake） | 待修 |
| #1632 条目4 gui_job_adapter 样板 | P3 候选 | 待评估 |
| #1638 P3 src/gui 弱桩三题 | 记录（vendored） | 待评估 |
| #1655 P3 CP11 渲染门 | 待复跑分类 | — |

## 本轨主线（依据 §6 工作包 + 实证）

1. **D0/W1b**：Edit Lease 统一（facade 协调器 seam + 属性表路径闭环）——本轨最高价值修复。
2. **D0/回归**：R20-04 三 seam（cancel/raw-schema/DPI）+ CP11 分类。
3. **D1**：CommandRegistry/SelectionContext 状态真值审计与状态转移测试补强。
4. **D2**：MapSpec/编图模板/导出质量纵深（#1655 分类后按需）。
5. **D3**：测试卫生（#1621/#1622）与共享区 append-only（#1623）。
