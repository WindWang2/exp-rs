# R21-D: Qt/QGIS 工作台、地图交互、编图出图与 UI 一致性 — 质量收敛波次

## Base / Head

- base: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（origin/master at pickup; R20 PRs #1668–#1672 all merged, open PR = 0）
- head: `d3d356f9f2`
- worktree: `exp-rs-r21-D-20261023T214750Z`, branch `agent/r21-d-qt-qgis-cartography-20261023T214750Z`
- commits: 5（每个 RED→GREEN×2，见下）

## 读过的 PR / Issues

- PRs：#1672 (R20-04, 直接前驱), #1665 (R19-03), #1661 (R18-D), #1662 (sweep), #1667/#1666 (data/Pi 前驱)。
- Issues：126 open 全量枚举+分诊（`.planning/r21-D/ISSUE_TRIAGE.md`）。D 轨白名单内 P1/P2 经源码实证：**#1535/#1536/#1537/#1559/#1560/#1561/#1562/#1563/#1564 均已在 master 修复**（复测绿，未重复修）；#1565/#1557/#1558/#1539 等属其他轨区域。**W1b**（R19-03→R20-04 移交：属性表编辑绕过 DataManager Edit Lease）live 复核仍在 → 本轨修复。

## 已完成工作包

### D0/W1b — Edit Lease 统一（b83f31386c）

属性表（qgsattributetabledialog.cpp:299/:706/:711/:930）、QgisApp facade 与 QgsGuiVectorLayerTools 三条编辑入口以裸 provider 调用结束会话，使 shell 的 RAII lease 句柄永久持有：资产对所有其他视图保持锁定、revision 不前进、兄弟 Display Layer 服务陈旧数据。

- 新增 `SicnuEditLeaseCoordinator` seam：shell 在 setupUi 安装、析构 guarded 释放（#1454/#1559 模式）；无 coordinator 时 facade 保持原生 QGIS 行为。
- `QgisApp::saveEdits`（逐层+批量）、`toggleEditing`、`QgsGuiVectorLayerTools::{startEditing,saveEdits,stopEditing}` 与 shell 槽同一 bracket：startEditing 前 acquire、commit 后 publish+release、rollback release、保存保持编辑时 re-acquire；身份快照（layer id + sicnu/displayLayerId）围绕每个协调步骤重解析（commitEditLease 可同步 relocate/destroy Display Layer）。
- `QgsAttributeTableDialog::editingToggled()` 空指针守卫（协调换层会同步销毁 mLayer）。
- RED（stash 生产修复）：0/4 新用例、6 关键断言红。GREEN×2：38/38；全文件 408/408（既有 17 用例无回归）。

### D2 编图/导出纵深（b29c751ea8, 851151b449, 641dec9d4f, d3d356f9f2）

1. **P1 atlas filter 静默丢弃**：解析失败的 filter 被整体丢弃（error 丢弃、setFilterFeatures 不 arm）→ atlas 导出交付全部 coverage feature，spec 声明的子集被无声 reinterpret（最多 512 页）。现为 typed 编译拒绝（与 filename_expression 契约对齐），有效 filter 经验证 arm。
2. **P2 export 无取消 seam**：单/多页栅格导出不接取消探针，Stop 后任务记 Cancelled 而文件已落盘。exportMapLayout 新增可选 cancelProbe（默认空），页边界轮询+回滚本 run 已交付页；cartography:export operator 映射 ErrorCode::Cancelled，produceMap 返回 PRODUCE_CANCELLED。
3. **P2 frame-declared CRS 不编译**：map_frames[].crs 经 mapspec 校验并平息 preflight MAP_MISSING_CRS_NOTE，编译器却丢弃 → 按项目 CRS 解释 spec CRS 单位的 extent（3857 米按度解释），渲染错误区域。现编译进 QgsLayoutItemMap，无效 CRS 拒绝。
4. **P3 applyItemProperties 类型抛异常**：visible/locked/bold/italic 的 asBool() 对错类型抛 Json::LogicError → 批调用返回 ok=false 且此前键已应用（半应用+误导错误）。改为 ignored（符合 applied/ignored 契约）并补 undo 配对。
5. **P3 重复页索引**：pages:[0,0] 被接受 → 同确定名双写（后覆盖前）而 manifest 声称 2 页。现 typed refusal。
6. **P3 repair 进度非单调**：0.3+0.1*iterations 从第 4 轮越过 export 阶段 0.6（进度条 70%→100% 掉回 60%）。repair 阶段改为 [0.30,0.55]。
7. **P3 layout:export 事后目录重扫**：pages[] 由导出后目录探测拼出 → 陈旧 sibling（早前更大交付/被 excludeFromExports 跳过的页）被报为本 run 交付页。exportLayout 新增 deliveredFiles 出参（导出前有界清理 + 报告本 run 实际写入）。

### D1/D3 回归与复测（无代码改动）

- R20-04 三 seam 回归：GDAL 取消（120/120）、DPI [72,1200]（1311/14）、raw schema registry 腿（mapspec 2830/239）。
- FIXED-OPEN 复测：#1535（8/2+120/11）、#1536（124/11）、#1537（28/3）、#1561（154/16）、#1562（7/4）、#1563（8/2）、#1564（1308/16）全绿。
- W1b 邻域：qgis_display_manager 6490/25、command_registry 57/11、selection_context 72/15、full_shell_lifecycle 126/7、workbench_host 50/8、shutdown_policy 54/4。

## 回归/基准

- 本机环境：ctest ENVIRONMENT_MODIFICATION 等价 + vendored 前缀 + `/tmp/r21a-libs` + `PROJ_DATA`（runner: `.planning/r21-D/run-test.sh`）。QT_QPA_PLATFORM=offscreen。构建 flock -j2 串行，6388 对象。
- 全绿族：r14_D 408/21、raster_processing_dialog_base 120/11、layout_tools 1338/16、mapspec 2830/239、qgis_display_manager 6490/25、command_registry 57/11、selection_context 72/15、full_shell_lifecycle 126/7、view_link 1308/16（PROJ_DATA 补齐后）、stac_client 28/3、spectral_profile_widget 154/16、log_panel 7/4、accuracy_panel_wiring 8/2、guided_workflow_widget 124/11、async_algorithm_runner 8/2、layout_designer 48/9、editing_e2e 34/1、i18n 401/6、theme_selector_parity 102/2、task_center（flaky 竞态用例，3 次 1 绿——见下）。
- KNOWN_BASELINE_FAIL（如实分类，未改 golden）：`test_cartography_production_11` 默认模式 3/28 红（:811/:1160/:1296 atlas/e2e/corpus），与 R19-03/R20-04 记录逐点一致；`SICNU_CP11_RENDER=0` 时 25 passed+5 skipped、0 failed → 主机 legend-paint 渲染路径差异（门注释记载），非产品缺陷。
- 环境伪红：test_view_link 3 断言（PROJ_DATA 缺失，补齐后全绿）；test_task_center #1454 竞态用例 flaky（3 次 1 绿，src/processing 非本轨 diff 域）。

## Review 修复次数

- **R1**（fresh reviewer #1，独立 RED 重放）：**REJECT**（1×P1 + 3×P2）→ db93b2655e。P1 是 frame-CRS 修复新引入的静默错渲回归（项目 CRS 度数 extent 被按声明 CRS 单位解释），已修（extent 变换 + 4326 回落 + extent 断言）。记录：`.planning/r21-D/REVIEW_R1.md`。
- **R2**（fresh reviewer #2，不同上下文，自行重构 RED 探针）：**APPROVE-WITH-FIXES**（4×P2）→ 8c3ff74bd6。包含 R1 一项虚假闭环的纠正（produce 取消探针照旧每轮询推进度事件）、4 处遗漏类型守卫、grid interval 单位分裂。记录：`.planning/r21-D/REVIEW_R2.md`。

## 剩余风险与未执行项（已知限制，均附理由记录于 REVIEW_R2.md）

- #1655 CP11 门：本机无法执行渲染用例（legend paint 主机差异），已用 SICNU_CP11_RENDER=0 证明零失败；关闭建议留给维护者在带显示环境复核。
- 取消/清单失败的回滚只删"本次已发布页"，不恢复被本次覆盖的上一版同名文件（原子发布契约语义）。
- kStaleSiblingSlack=16 有界清理对 >16 页的旧交付留孤儿（有界、下次导出自愈）。
- page_count 跨表面语义：layout:export = 交付文件数；cartography:produce pdf = 布局页数（各自文档已说明）。
- W1b 双窗口集成测试未构造（NOT_EXECUTED：QgisApp 单例协调器由 guarded release 代码级覆盖）。
- O-D1/O-D4 的"新造"用户路径/视觉记录未展开：本会话以审计驱动缺陷修复与双评审闭环为优先，既有 offscreen 覆盖全绿。

## 同轨文件白名单

`src/app/**`（排除 teaching）、`src/gui/**`、`src/ui/**`、`src/agent/cartography/**`、`src/agent/layout_tools/**`、`src/agent/mapspec/**`、对应测试、`.planning/r21-D/**`、`.gitignore`（append-only R21-D 例外块）。未触碰共享构建登记表与他人 worktree。
