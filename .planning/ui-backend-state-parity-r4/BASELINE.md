# BASELINE — ui-backend-state-parity-r4（轨道 3 R4 Deep Edition）

记录时间：2026-09-27。全部数字为实测（`git fetch origin` 后），未沿用提示词快照值。

## 1. 实测基线

| 项 | 值 |
|---|---|
| `origin/master` 实测 SHA | `15e5c66b543ef3874cb929f17529ef456bd6c059`（PR #1333 合并点；与提示词写作值一致，未前进） |
| 本分支领先/落后 | 新建分支 `hardening/r4-ui-state-parity` 指向 `15e5c66b5`，+0/-0 |
| 本轨道 worktree | `/home/kevin/project/exp-rs-ui-state-parity-r4`（master 仓库严格只读） |
| 本机构建工具链 | cmake/ctest: `/home/kevin/toolchain/cmake-dist/bin/`；ninja: `/home/kevin/toolchain/ninja`（shell 存在 `ninja -j40` alias，为红线陷阱，一律用显式路径）；Qt 6.11.2（/usr/lib/cmake/Qt6）；GDAL/GEOS/PROJ: `/home/kevin/pwb-sdks/root/usr`；QGIS 为仓库 vendored 子集 |
| 资源红线执行 | `CMAKE_BUILD_PARALLEL_LEVEL=2`、`ninja -j2`、`CTEST_PARALLEL_LEVEL=1`、offscreen 由 CTestCustom.cmake 注入 |

## 2. 在途 PR 盘点与 file-overlap map（实测 5 个，提示词只列 3 个）

| PR | 分支 | 文件数 | 与本轨道白名单的交集 | 处置 |
|---|---|---|---|---|
| #1334 fix/review-p1-security | 30 | `src/agent/**`、`src/app/main.cpp`、`src/app/main_window.cpp`、`tests/CMakeLists.txt`、`tests/test_mcp_server.cpp` 等 | **tests/CMakeLists.txt 冲突面**；main_window.cpp 是状态镜像关键文件 | tests/CMakeLists.txt：独立 hunk 追加注册；PR 说明 rebase 责任。main_window.cpp 若需触碰须最小化并声明 |
| #1335 fix/review-p0-build-restore | 19 | 顶层 `CMakeLists.txt`、`cmake/SicnuCatchAddTests.cmake`、`cmake/SicnuSharedLinkGuard.cmake`、`src/agent/CMakeLists.txt`、`src/app/CMakeLists.txt`、`tests/CMakeLists.txt` | **tests/CMakeLists.txt 冲突面**；**新测试链接 sicnu_geo_rs_shell 需 Sicnu::agent_ops（该 PR 的发现）** | 注册新测试用独立 hunk；链接 shell 时直接带上 `Sicnu::agent_ops`（吸收其教训，避免重复踩坑） |
| #1336 hardening/closure-ui-runtime-r4 | 13 | `tests/test_m2_dialog_stress.cpp`、`tests/test_rs_result_summary.cpp`、`tests/test_data_manager_panel.cpp`、`data/help/**`、`src/lab_pack/lab_pack.cpp` 等 | test_m2_dialog_stress.cpp（WP-F 压测扩展目标！）与 test_rs_result_summary.cpp | WP-F 扩展压测**不改动既有断言**，只追加场景与文件级 append；若仍重叠，PR 说明"基于 #1336 合并后 master rebase"。#1336 修 i18n 漂移（英文 tr 源钉住），WP-F 压测断言一律只用英文 tr() 源文本 |
| #1337 hardening/closure-workflow-contracts-r4 | 25 | `src/agent/harness/**`、`src/contracts/**`、`mission-runtime-gate/**` | 极小（本轨道不碰 harness/contracts） | 无冲突预期 |
| #1338 hardening/closure-io-processing-r4 | 29 | `src/geospatial/**`、`src/data/**`、`src/processing/gdal/**` | 极小（本轨道不碰 io/processing 内核） | 无冲突预期 |

**合并顺序风险声明**：本 PR 基于当前 `15e5c66b5`。上述任一 PR 先合并后，本分支在 `tests/CMakeLists.txt`（及可能的 test_m2_dialog_stress.cpp）上需要 rebase；这些文件的本轨道改动均为**纯追加 hunk**（新测试注册、新压测场景），rebase 冲突概率低。

## 3. 评审材料通读结论

- **#1335「剩余 142 个既有失败」8 组分类**（本轨道相关的）：
  - 组 2 退出段错误（14 个，断言全过后 atexit 段错误）：layer_sync_contract、qgis_display_manager、workspace_services 等——其中 layer_sync_contract 类名字面与本轨道 gate 正则 `state|mirror` 不相交但属簇 B 邻接；不修（属簇 A 拆机生命周期，#1335 已给出 `_Exit` 处置并声明正确修法在簇 A）。
  - 组 3 i18n 漂移（约 14 个）：data_manager_panel(5)、schema_form_4(2)、**m2_dialog_stress(2)**、raster_processing_dialog_base、temporal_scene_model、rs_result_summary——**与本轨道 gate 正则 `dialog` 相交**。#1336 正在修（钉英文 tr 源）。本轨道 Phase 0.5 实测确认基线红绿后决定：若基线仍红且 #1336 未合并，本轨道对该两文件**只接受 #1336 同方向修复**（钉英文源文本）以让 gate 可达，PR 如实声明与 #1336 的重叠。
  - 组 8 workflow_checkpoint_cache（2 个，QEventLoop without QApplication）——异步落地面的既有教训：**测试里 QEventLoop 必须有 QApplication**，本轨道全部 oracle 测试遵守。
- **#1336 未解决项**：明示簇 A（拆机崩溃）、**簇 B（UI/后端状态一致性）**、簇 E（插件生命周期）未收口 → 本轨道就是簇 B 的收口轨道。#1336 在 Windows 上未实跑任何 C++ 测试；本机 Linux 可实跑，本轨道所有 oracle 必须真实执行。
- **#1312 已收项 diff 清单**（merge `7d14e96b6`，本轨道不重复建 oracle）：
  1. full-shell lifecycle fixture：`tests/test_workbench_full_shell_lifecycle.cpp` + `sicnu_geo_rs_shell` 静态库收编。
  2. story-boundary：`resetSessionStoryState()`（main_window_project.cpp）+ failed-open lab 泄漏修复。
  3. SaveAs：`saveProjectAsTo()` + mission authority refs re-home + 失败回滚。
  4. run-mirror connect-once：`project_context.{h,cpp}` `installRunStateMirror()`（重复 recordRun 缺陷已收）。
  5. **B12 restore-state 契约**：`main_window_misc.cpp` restoreState/restoreGeometry 返回值即契约，corrupt blob 丢弃、版本门精确 `==`。
     → WP-E 不重复上述，只补：会话数据版本不符、半截布局字节串、几何损坏三类注入的 fail-safe 验证面与触发时机（若 B12 已覆盖其中某类，实测 diff 后标注"已收（#1312）"）。
- **#1316**（merge `8bb2de13d`）：mission 侧 selection-authority parity 模式先例 → WP-B 参照其模式。
- 本地 `PROJECT_REVIEW_DOSSIER_5.0.md` / `AUDIT_DOSSIER_ISSUES_747_760.md` / `PR_TRIAGE_REPORT_2026-09-16.md` / `docs/PARALLEL_TRACKS_10.md`：**实测均不存在**（已按提示词预案以 git 历史与 PR 描述为准）。仓库现存 `WHOLE_REPO_REVIEW.md`、`CONTEXT.md`、`PROJECT.md`。

## 4. 开放 issue

`gh issue list --state open` 实测：0 个（2026-09-27）。

## 5. 本轨道边界声明（白名单）

允许触碰：
- `src/app/widgets/**`（22 对控件）
- `src/gui/**`（状态镜像、选择权威、异步落地——仅当映射对证据指向时最小触碰）
- `src/app/**`（main_window 状态面、project_context 等后端状态源的最小修复）
- `tests/**`（parity oracle 测试 + tests/CMakeLists.txt 注册）
- `.planning/ui-backend-state-parity-r4/*.md`（5 份规划工件）
- `.gitignore`（追加本轨道 .planning 白名单条目）
- `.goal-loop-ledger.md`（账本，仓库惯例随 PR 携带）

凡白名单外改动一律拒绝并记账。**明确不做**：新功能方向/新工作台/新算子/新实验/新产品入口；src/agent、src/contracts、src/geospatial、src/data、src/processing 内核（在途 PR 领地）；等待线上 CI。

## 6. 锚定表复核（2026-09-27 实测，7/7 吻合）

| 锚点 | 实测值 |
|---|---|
| `src/app/widgets` 文件对 | 22 对（44 文件） |
| 九代表面板 | guided_workflow_widget / spectral_workbench_panel / rs_result_summary / rs_scan_pool / rs_toolbar_flow_host / timeline_scrubber_widget / comparison_widget / progress_dialog / rs_empty_state_widget 全部存在 |
| `src/app` .h/.cpp | 655 |
| `src/gui` .h/.cpp | 1604 |
| state/mirror/selection 既有测试 | 远超 25（test_selection_context / test_feature_selection / test_spectral_selection / test_temporal_selection / test_workbench_state_model / test_workspace_state / test_classify_session_state / test_project_context_run_mirror / test_io_mirror_maintenance …） |
| 压测模式源 | tests/test_m2_dialog_stress.cpp 存在 |
| 异步暴露面 | QNetworkReply 66 文件 / singleShot 29 处 / deleteLater(gui) 107 行 |
| 前序已收项 | #1312 merge `7d14e96b6`、#1316 merge `8bb2de13d`（diff 清单见上节） |
| 测试文件全集 | tests/ 共 918 个文件 |

## 7. PARITY_MAP 建对起点全集

- 九代表面板各 ≥1 对（硬性）。
- 其余 13 个控件：annotation_widget、band_composite_palette、band_role_combo、cross_section_widget、crs_selector、histogram_stretch_widget、histogram_widget、lab_spec_loader、python_script_editor、raster_layer_combo、resolution_widget、roi_statistics_widget、spectral_profile_widget。
- 状态源（同层）：src/app 的 main_window 状态面、project_context、workbench、mission、task center、workflow coordinator；src/gui 的 layer tree/selection/display。**widgets 下无 data_manager/layer/properties 命名文件，禁止编造。**
