# fix(r22-05): 工作台易用性闭环 — async runner 生命周期、temporal 页路由、zoom-native CRS、组删除租约、log 标签过滤与 UI 一致性（9 项，双独立评审闭环）

## Base / Head

- base：`911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（origin/master @ pickup 与 push 前复核一致：`gh api commits/master` 确认未移动）
- head：`agent/r22-workbench-20261025T163805`（15 commits）
- worktree：`exp-rs-r22-workbench-20261025T163805`

## 改动清单（24 非 planning 文件：18 生产 + 5 新测试 + tests/CMakeLists.txt append）

| 包 | 缺陷（用户可见后果） | 文件 |
|---|---|---|
| W1 #1695-1 | async runner 任务中析构 → WaitCursor 滞留至退出；父对话框先亡 → 完成回调从悬垂裸指针构造 QPointer（RED 实证 SIGSEGV）；null-parent 构造 → completed 永不送达 | dialogs/async_runner_base.{h,cpp}、async_gdal_runner.cpp |
| W2 | Temporal 11.0 三算法（seasonal breaks/model select/phenology）参数页不可达——选择后仍显示汇总页，研究以硬编码默认值静默运行 | dialogs/temporal_analysis_dialog.cpp |
| W3 | 「原生分辨率 1:1」把层 CRS 单位直接写进画布 extent：OTF 下视图错位 5 个数量级 | active_view_host.cpp（setCanvasExtentTransformed，#1030 fail-closed 同款） |
| W4 | 图层树组「Remove」绕过 DisplayManager：Display Layer 记录成幽灵、Asset 租约滞留（"asset is leased / cannot unload" 直到清工程） | layer_tree_menu.cpp、active_view_host.{h,cpp}（新 removeLayers()） |
| W5 | 日志面板键入标签过滤被静默忽略（currentData 为空跳过整段判断 + 重复条件） | log_panel.cpp |
| W6 | 工作区重绑每次泄漏一个 ImportCenter（含绑定的 DataManager） | panels/workspace_browser_panel.cpp |
| W7/W8 | 英文路径全角冒号 `DPI：`；`n item(s)`/`n component(s)` 不可译复数；`1 layers` | cartography_dock.cpp、layout/qgslayoutdesignerdialog.cpp、active_view_host.cpp |
| W9 | 工具箱对话框 cancel 已武装仍被困（无法关闭）；jobCancelInFlight 三处复制 | shell/gui_job_adapter.{h,cpp}、sicnu_algorithm_dialog.{h,cpp}、raster_processing_dialog_base.cpp |
| extras | map tools 日志计数写死 19（实际 20）；TaskCenter 发起的外部取消使批处理静默跳文件、汇总少计、进度条 100% | map_tools/map_tool_manager.cpp、dialogs/batch_processing_dialog.cpp |

## 关联 Issues

- 关闭本轨范围：#1695 第 1 项（async runner；同 issue 其余项在 teaching/lab 域，见下）。
- 不重复修（动态验真）：#1557 master 已修（1b3d28f1b7）；#1632 六项全部已修；#1655 CONTESTED 转跨轨。
- 跨轨移交（CROSS_TRACK_FINDINGS.md）：#1678/#1688/#1689/#1694、#1695 第 2/3/5 项、#1655 CI 归属、zh_CN 六处孤儿条目清单。

## RED → GREEN 证据

- RED（未打补丁）：async 2 断言红 + 父亡 SIGSEGV；view_tree 2 红 + 组默认动作 SIGSEGV；temporal 6 红；log 1 红；ui_contract 10 红。R2 评审员独立 revert 复现（12 断言红）。
- GREEN×2（双跑一致）：24+70+30+24+34 断言 / 4+2+1+1+5 用例。
- 邻域回归 12 目标全绿（含 m3_stress 5241 断言、m2_batch_b_dialogs 193、ui_task_center_contract 169）。
- 逐字输出：`.planning/r22-workbench/RED_GREEN.md`、`TEST_RESULTS.md`。

## 双独立评审

- **R1**（fresh reviewer）：APPROVE-WITH-FIXES（P1：W9 逃逸在 WA_DeleteOnClose 下重开 #339 反馈 UAF；P2：模态后节点守卫；P3×4）→ 全部修复（d8d99ee28d）。
- **R2**（另一 fresh reviewer，独立构造反例 + 独立 RED 复现）：APPROVE-WITH-FIXES（P2：workerStarted 跨线程无同步→atomic；P2：Esc/reject 绕过 closeEvent→共享 canCloseSafely 门；P3×2/P4）→ 全部修复（a1f1fd8da6）。
- 记录：`.planning/r22-workbench/REVIEW_R1.md`、`REVIEW_R2.md`。

## 与 R21/前序差异核对

- #1677（R21-D）全程只读：其 29 文件与本轨 24 文件零交集；本轨未重做其 atlas/cancel/frame-CRS 修复；#1677 记录的双窗口测试与覆盖恢复缺口因依赖其未合并符号记 BLOCKED_ON_#1677。
- #1673-#1676 已关闭未合并；新 R22 PR（#1708-#1712）与本轨零文件交集（push 前 GitHub files API 逐文件核对，见 PR_COLLISION_MATRIX.md）。
- 历史技术债 #1632 六项经源码实证 master 已修——本轨**未重复实现**。

## 性能

无性能目标项（PERF_BASELINE.md = N/A）；全部为正确性/生命周期/交互语义修复。

## 遗留风险 / 未执行项（如实）

- 双窗口编辑租约集成测试、Atlas 覆盖恢复：BLOCKED_ON_#1677（合并后接手）。
- 对话框 close 生命周期行为级 E2E：NOT_EXECUTED（需真实 processing 算法夹具）；以源码契约测试（canCloseSafely/reject/workerStarted/executor 空守卫）+ 双评审推演替代。
- CP11 渲染用例：本机 legend paint 挂死（既往三轮记录一致），未设 SICNU_CP11_RENDER=1 强跑；该文件归 #1677，本轨未触碰。
- zh_CN .ts 六处源串变更产生孤儿条目（resources/translations 不在本轨白名单）→ 跨轨清单已附。
- python 编辑器输出无上限/无取消、temporal 同步 GDAL 重扫、魔棒每点击 open、CrossSectionWidget 死代码：发现并记录（CROSS_TRACK），未实施。
- 构建/运行环境 shim（vendor GDAL wrapper2 stub、Qt 符号链接）仅存在于宿主 /tmp，不进入本 PR。

## 不等待 CI

按轨道规约：线上 CI 不参与完成门（master 自 2026-10-11 起无完成 CI 结果，#1491），全部验证以上文本地构建/运行为准；PR 不自动合并。
