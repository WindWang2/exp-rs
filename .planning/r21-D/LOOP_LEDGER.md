# R21-D LOOP_LEDGER（goal-loop 每轮记账）

## Round 4（2026-10-24 16:0x–19:xx CST）— 审计驱动编图/制图/布局修复波次

- 子代理（fresh context）深度审计 src/agent/cartography + layout_tools + mapspec + app/cartography：交付 P1×1 / P2×2 / P3×5 + 非缺陷核验清单。
- 修复并提交（每个 RED→GREEN×2）：
  1. b29c751ea8 P1-1 atlas filter 解析失败静默丢弃（→编译拒绝 + 验证 filter armed）；P2-2 export 无取消 seam（page-boundary probe + 回滚 + operator/produce 映射 Cancelled/PRODUCE_CANCELLED）。
  2. 851151b449 P2-1 frame-declared CRS 声明不编译（→编译进 QgsLayoutItemMap，无效 CRS 拒绝）。
  3. 641dec9d4f P3-2 applyItemProperties 布尔键类型抛异常半应用（→ignored + undo 配对）；P3-3 重复页索引双清单双写（→typed refusal）；P3-4 repair 进度非单调（→[0.30,0.55]）。
  4. d3d356f9f2 P3-1 layout:export 事后目录重扫陈旧 sibling（→exportLayout deliveredFiles 出参 + 导出前有界清理）。
- 构建环境教训：本机 make 的 depend 检查在时钟偏移下漏编（对象时间旧于源但不重编）——强制删 .o 重编后才暴露真实编译错误（UndoInvisibility 不存在等）。所有 RED 证据均为重编后的真实运行结果。
- 测试：R21-D 新用例 7 个（CP11 4 + layout 3），31+27+12+7 断言全绿；族回归 mapspec 2830/239、layout_tools 1338/16、CP11 29+3 已知环境红（SICNU_CP11_RENDER=0 时 0 失败）。
- 下一轮：R1 独立评审（fresh reviewer 审 diff BASE...HEAD）。

## Round 2（2026-10-24 14:0x–15:xx CST）— W1b 验证闭环 + 回归族 + 第二波启动

- W1b RED 验证：stash 生产修复（保留测试）→ rebuild → 4/4 用例红、6 关键断言红（hasActiveEditLease=false、revision 未前进）→ pop → rebuild → GREEN×2（38/38）。
- 全文件回归：test_r14_D_vector_edit_honesty 408/408（21 用例，既有 17 无回归）×2。
- 邻域回归全绿：qgis_display_manager 6490/6490 · command_registry 57/57 · selection_context 72/72 · full_shell_lifecycle 126/126 · workbench_host 50/50 · shutdown_policy 54/54 · mapspec 2830/239 · layout_designer 48/48 · layout_designer_lifecycle 18/18 · editing_e2e 34/34 · active_view_host_{data_context 83, viewport 12} · theme_selector_parity 102/2 · i18n 401/6 · accuracy_panel_wiring 8/2 · guided_workflow_widget 124/11 · async_algorithm_runner 8/2 · stac_client 28/3 · spectral_profile_widget 154/16 · log_panel 7/4 · raster_processing_dialog_base 120/11（R20 cancel seam）· layout_tools 1311/14（R20 DPI seam）。
- 环境伪红归因（O-G5）：test_view_link 3 断言红根因 PROJ_DATA 未设（vendored proj.db 在 prefix/share/proj）→ 补 PROJ_DATA 后 1308/1308 全绿。runner 脚本已固化该变量。
- CP11（#1655）：默认 25/28 过，3 红 :811/:1160/:1296 与 R19-03/R20-04 记录逐点一致；SICNU_CP11_RENDER=0 时 23 passed+5 skipped、0 failed → 主机渲染路径差异（门注释记载的 legend paint 主机问题），非产品缺陷。
- 提交 b83f31386c（W1b 波1：facade/window/vectorLayerTools/attributeTable 协调器 + 4 测试 + .planning 全套 + .gitignore R21-D 例外）。
- 下一波：编图/导出纵深审计（子代理进行中）。

## Round 1（2026-10-24 07:0x–07:5x CST）— W1b 实现 + 构建修复

- 问题：属性表/facade/vectorLayerTools 三条编辑入口绕过 DataManager Edit Lease（W1b，R19-03→R20-04 移交，live 复核仍在）。
- 修改（单一归因：lease 记账统一）：
  - `src/app/qgis_app_facade.h/.cpp`：新增 `SicnuEditLeaseCoordinator` 接口 + facade 保存/协调器存取；saveEdits(per-layer/batch)/toggleEditing 三方法加 lease bracket（commit→commitEditLease→按 id/displayLayerId 重解析→leaveEditable 时重取 lease）；批量版改按 id 快照迭代防重定位 UAF。
  - `src/app/main_window.h/.cpp`：QgisDesktopWindow 实现 coordinator（acquire 单参 override 转发 2 参 shell 版；commit/rollback 现有签名即 override）；setupUi 注册、析构 guarded 释放（同 #1454/#1559 seam 模式）。
  - `src/app/qgsguivectorlayertools.cpp`：startEditing/saveEdits/stopEditing 三路径同 bracket + 重定位重解析。
  - `src/app/qgsattributetabledialog.cpp`：`editingToggled()` 空指针守卫（协调提交会同步换层销毁 mLayer，destroyed→close 不覆盖同步残余路径）。
- RED 测试（`tests/test_r14_D_vector_edit_honesty.cpp` 追加 4 用例 + W1bAssetLayer 夹具）：facade stop 释放 lease+revision 前进；facade saveEdits(leaveEditable=false) 终结会话；批量 save 重开会话持新 lease；vectorLayerTools start/stop bracket。判据：`hasActiveEditLease` 与 `revision().value()`（DataManager 公共 API）。
- 构建插曲：首轮全量编译在 libqgis_core.so 链接失败——vendored libsqlite3.a 无 -fPIC（非 OOM）。对账兄弟轨 cache：三轨均钉系统 sqlite（/usr/lib/libsqlite3.so）。补 `-DSQLite3_INCLUDE_DIR/-DSQLite3_LIBRARY` 重配置后续编（进行中）。
- 验证：待编译完成后执行 stash-fix→RED→unstash→GREEN×2 流程。
- 结论：继续。

## Round 0（2026-10-24 05:45–06:30 CST）— 基线与盘点

- LIVE 盘点完成：origin/master=911e0b17a3，open PR=0，126 open issues 全量导出+分诊（ISSUE_TRIAGE.md）。
- 关键发现：D 轨 P1/P2 候选中 #1535/#1536/#1537/#1559/#1560/#1561/#1562/#1563/#1564 全部 FIXED-OPEN（源码逐条实证，证据在 ISSUE_TRIAGE）；**W1b 属性表绕过 Edit Lease 仍在**（facade :181-287 无 lease）。
- worktree 建立：exp-rs-r21-D-20261023T214750Z @ 911e0b17a3。
- 构建配置成功（vendored 依赖路径沿用兄弟轨）；全量编译后台进行中（flock -j2）。
- 规划文件齐备：BASELINE/ISSUE_TRIAGE/OWNERSHIP/PR_COLLISION_MATRIX/RISK_REGISTER/TASKS/ORACLES。
- 下一轮：M1 seam 回归 + CP11 分类；W1b RED。

## Round 模板

```
## Round N（时间）— <主题>
- 问题：<单一清晰问题>
- 归因修改：<file:line + 意图>
- 验证：<RED 证据 / 定向编译 / GREEN 轮次与 exit code>
- 结论：继续/换假设/阻塞
```

## Round 5（2026-10-24 深夜）— R2 闭环 + PR 交付

- R2（第二个 fresh reviewer）APPROVE-WITH-FIXES：4×P2 + 6×P3 采纳 → 8c3ff74bd6。R2 以自行重构 RED 探针独立验证 R1 的 P1-1/P2-2/P2-3 真实修复，并纠正 R1 一项虚假闭环声明（produce 探针照旧每轮询推进度事件）。
- R2 返工中发现并修正本轨测试缺陷：W1b 分支用例最初以 `leaseCount()`（含 View lease）断言 Edit lease 唯一性 → 读数为 2 → 补 editLeaseCount 辅助（按 LeaseRef::kind 过滤）。
- 终态回归：r14_D 440/440（24 用例）、CP11 [r21d] 44/4、layout [r21d] 27/2、mapspec 2830/239、layout_tools 1338/16；CP11 默认 29+3 已知环境红（render-off 0 failed）。
- 提交前核对：`git diff --check` 干净、工作树干净、merge-base = origin/master = 911e0b17a3（master 未前进，无需 rebase）、open PR 盘点（兄弟轨 #1673-#1676 在场；B 轨 PR 未触碰 data_manager.h，与 W1b 共享 API 无签名冲突）。
- push：origin `agent/r21-d-qt-qgis-cartography-20261023T214750Z`；PR：https://github.com/WindWang2/exp-rs/pull/1677（未 auto-merge、未等 CI）。
- 总 commits：11（911e0b17a3 → 628f2590d8）。
