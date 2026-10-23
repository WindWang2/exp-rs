# ZCode R20-04 BASELINE（LIVE RECON，2026-10-23 执行）

## 仓库与 HEAD

- 主仓：`/home/kevin/projects/rs-studio/main`（origin = https://github.com/WindWang2/exp-rs.git）
- LIVE `origin/master` = `28e1891390bb812dc7ef2fe045ac90492c9525f1`（fetch --prune 后复核，与审计快照 2026-10-22 一致，2026-10-21 08:23 +0800 后无新提交）
- 最近合并：PR #1663–#1667（r18-a/r18-e/r19-03/r19-02/r19-01），全部 MERGED；无 open PR（`gh pr list --state all` 前 100 条复核）。
- master 工作树 clean。

## R20 兄弟轨（worktree 复核，避免冲突）

| 轨 | 分支 | 领先 | 已改文件域 |
|---|---|---|---|
| R20-01 | agent/r20-01-scientific-algorithms-20261022T160501Z | 0 | （未开始，仅 build-r20-01 已配置） |
| R20-02 | agent/r20-02-data-model-runtime-20261022T160551Z | 1 | tests/test_{fault_injection,model_catalog_v2,model_runtime,workspace_services}.cpp + .planning/zcode-r20-02/** |
| R20-03 | agent/r20-03-pi-mcp-workflow-20261022T160809Z | 6 | src/agent/harness/**, src/agent_ops/operations_coordinator.h, src/sdk/exprs/ipc_*.cpp + tests/test_{capability_knowledge,exprs_ipc,harness_evals}.cpp |

→ 本轨写区（src/app/** 排除 teaching*、src/ui/**、resources/**、src/agent/{cartography,mapspec,layout_tools,symbology}/**、src/agent/agent_copilot_dock_widget.*, view_control_service.*, raster_display_service.*, src/help/** 排除 help_content_store.cpp、对应 tests、.planning/zcode-r20-04/**）与三兄弟轨零交集。

## Open Issues（126 条，多页遍历摘要）

- 全部 open issue 为 P2/P3 技术债汇总 + 少量专项。与本轨直接相关：
  - **#1560 [P2] runGdalTask 取消 hook 空 lambda**（src/app/dialogs/raster_processing_dialog_base.cpp:383）— LIVE 复核仍 OPEN，master 现码确认未修（cancel hook 仍为注释性空实现；executor 仅在 task() 前后检查取消）。R19-03 CROSS_TRACK_FINDINGS #1 明确"归属本轨后继轮次"。**本轨 D03 主任务**。
  - **#1632 [P3] src/app 技术债 6 项**（terrain_dialog 死 QFutureWatcher、路径派生重复且 completeBaseName/baseName 分歧、spectral_profile_widget 双同名类、gui_job_adapter 三重复、va_selection_hub clampText 第三份拷贝…）— 本轨候选。
  - **#1638 [P3] src/gui 弱桩三题** — src/gui 为本轨只读（vendored QGIS 基线），仅诊断记录，不改 ABI。
  - **#1623 [P2] sicnu_zh_CN.qm 无 install 规则** — i18n 管线，候选（改 CMakeLists 属共享区，append-only）。
  - **#1621/#1622 [P2] 测试恒真/墙钟门槛**（test_classification_studio_widget / test_spectral_profile_widget）— 本轨测试域候选。
  - **#1655 [P3] test_cartography_production_11 渲染门** — R19-03 已证本机离屏 3/28 失败（SICNU_CP11_RENDER 默认运行），环境相关，谨慎区分。
- 其余（src/core、src/runtime、src/cli、src/verification 等）属其他轨，不处理。

## R19 遗留跨轨线索（.planning/r19-*/CROSS_TRACK_FINDINGS.md 复核）

1. **R19-03 #1 = #1560**：seam 注释自述设计方向 — 把 task lambda 从 `std::function<QString()>` 扩为可拿到取消探针的形式，各对话框在 GDAL 进度回调里轮询；改动面 = 基类签名 + 全部对话框调用点。归属本轨。
2. **R19-02 CT-6**：`src/agent/cartography/cartography_tools.cpp` 7 个工具 raw schema 缺项（4 缺 `properties:{}`：catalog_index/chart_list/list_rules/list_token_sets；3 缺 `type:"object"`：diff_templates/export/explain）。`cartography:lint_catalog` 已 canonical。registry 腿 test_surface_snapshot 确定性失败（KNOWN_BASELINE_FAIL，首个失败 list_token_sets）。**本轨 D10 主任务**（cartography_tools.cpp 在本轨允许写清单内）。
3. **R19-03 W1b**：QgisApp facade toggleEditing/saveEdits 绕过 Edit Lease — 需 facade→shell 回调 + data 侧联合，本轨只做 UI 侧对照验证 + 跨轨移交，不改数据域。
4. **DPI 下界不一致**：layout:export(1) vs cartography:export(72) — r18-D 遗留观察。layout_tools 在本轨写区，先核查两处代码再定。
5. **#1638 弱桩**：只读诊断。

## 构建环境（复刻 R20-02/R20-03 已证配方）

- Arch CachyOS、GCC 16（/usr/sbin/c++）、CMake 3.30 Unix Makefiles、Qt 6.12 系统、GDAL/PROJ/GEOS/GSL/NETCDF/QCA/SQLite3 全部系统 /usr 解析（R19-02 的 vendor 前缀 `paleo-singlefactor-closure` 已消失，勿再用）。
- configure：`cmake -S . -B build-r20-04 -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON`
- 全局锁：`/home/kevin/projects/rs-studio/main/.git/exp-rs-zcode-global-build.lock`（-j2，与 R20-01/02/03 串行）；测试 `-j1` + `QT_QPA_PLATFORM=offscreen`。
- ccache 已全局配置（base_dir=/home/kevin/projects/rs-studio，跨 worktree 共享）。
- CI 全仓失效（#1491）→ 全部本地验证。

## 本轨工作树

- 分支：`agent/r20-04-qt-qgis-cartography-20261022T190613Z`（基于 origin/master 28e1891390）
- 路径：`/home/kevin/projects/rs-studio/exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`
- 构建目录：`build-r20-04`（log: /tmp/r2004-configure.log, /tmp/r2004-build.log）
