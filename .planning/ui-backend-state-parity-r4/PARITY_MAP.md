# PARITY_MAP — UI/后端状态一致性映射全集（ui-backend-state-parity-r4）

> 每对五列：UI 控件（文件:符号）| 状态源（文件:符号）| 同步方向 | 触发时机 | oracle 引用（测试文件:用例）。
> 行号证据为 2026-09-27 @ `15e5c66b5` 实测。oracle 引用在测试落地后回填真实用例名（gate 会校验存在性）。
> 前序已收标注：#1312（full-shell/story-boundary/SaveAs/run-mirror/B12 restore）、test_scan_pool 既有 generation 契约、r3 debugger study（guided workflow 会话边界）。
> 方向语义：源→UI = 后端状态变化必须反映到 UI；UI→源 = UI 操作必须被权威源吸收；策略 = 异步落地时的晚到处置合同。

## A. GuidedWorkflowWidget（src/app/widgets/guided_workflow_widget.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| GW-1 | `m_workflowList` 行集（cpp:150-160 populateWorkflowList） | `lab::LabLoadResult`/`m_workflows`（lab_spec_loader.h loadLabSpecsFromDir，cpp:134） | 源→UI | `loadWorkflows()`（构造期一次，文件头自述"重开面板刷新"） | test_parity_state_mirror_r4.cpp [parity-gw1] | — |
| GW-2 | 错误页（cpp:162-186 onWorkflowSelected error 分支） | `m_loadResult.errors` | 源→UI | 选中第 0 行 | test_parity_state_mirror_r4.cpp [parity-gw2] | — |
| GW-3 | `m_startButton` enabled（cpp:212 walkable） | `Workflow.steps` 非空 | 源→UI | onWorkflowSelected | test_parity_state_mirror_r4.cpp [parity-gw3] | — |
| GW-4 | `m_runButton` enabled/文案（cpp:293-296,384-388） | `m_jobHandle->isRunning()` ∧ 当前步骤非 manual | 双向 | showStep / runOperatorStep / 晚到完成回调 | 无独立 oracle（需真实作业执行；r3 已加固 + test_guided_workflow_widget.cpp 既有面） | r3 已加固 |
| GW-5 | 步骤游标越界防护（cpp:291-293,322-324 fail-closed） | `Workflow.steps.size()` | 源→UI（自洽） | onNextStep / onRunStepAction | test_parity_state_mirror_r4.cpp [parity-gw5] | — |
| GW-6 | 晚到任务完成只还原归属会话按钮（cpp:391-424 workflowId+stepIndex 守卫） | GuiJobHandle 完成回调（gui_job_adapter.h:24） | 源→UI 策略 | 任务完成（任意时刻） | 无新 oracle（r3 已加固，见 DECISIONS D-5；test_guided_workflow_widget.cpp 既有） | r3 已加固，补 oracle |

## B. SpectralWorkbenchPanel（src/app/widgets/spectral_workbench_panel.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| SW-1 | 谱表行列表 + digest/provenance 状态行（cpp:100-108） | `SpectralTable::loadValidated`（processing/algorithms/spectral_table.h） | 源→UI | `setTablePath`（cpp:78） | test_parity_state_mirror_r4.cpp [parity-sw1] | — |
| SW-2 | 非法/损坏表 → 类型化错误态且 `spectrumCount()==0`（setTablePath 失败路径） | 同上失败分支 | 源→UI | setTablePath 失败 | test_parity_state_mirror_r4.cpp [parity-sw2] | — |
| SW-3 | 选中谱详情 ↔ `spectrumSelected(QString,int)`（h:48/cpp:174） | 面板选中投影 | UI→源投影 | selectSpectrum（cpp:115） | test_parity_state_mirror_r4.cpp [parity-sw3] | — |

## C. RsResultSummary（src/app/widgets/rs_result_summary.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| RS-1 | 状态行/指标/警告/产物区块（cpp:127 setResult） | `AlgorithmTaskInfo.resultPayload`（task_center.h；喂入 task_panel_host.cpp:176-182、rs_job_panel.cpp:690-716） | 源→UI | showResult / fillDetailsForTask | test_parity_state_mirror_r4.cpp [parity-rs1] | — |
| RS-2 | `clear()` 后 `hasResult()==false` 且区块全空（cpp:109） | 清空合同 | 源→UI | clear | test_parity_state_mirror_r4.cpp [parity-rs2] | — |
| RS-3 | `openPathRequested` 仅对产物路径发射（cpp:73） | 产物路径（Json artifacts） | UI→源 | 双击产物 | test_parity_state_mirror_r4.cpp [parity-rs3] | — |

## D. RsScanPool（src/app/widgets/rs_scan_pool.*，本身为状态源）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| SP-1 | generation 单调 + `isStale` supersede 语义（h:38-59） | 池内部 activeGeneration | 源语义 | nextGeneration | 既有 test_scan_pool.cpp + test_parity_state_mirror_r4.cpp [parity-sp1] | 已收（test_scan_pool），补 owner 维度 |
| SP-2 | `cancel(g)` 后慢 worker 仍判 stale（cpp:33-42）⚠ 1024 条批发 `m_canceled.clear()` 打破该保证 | 池 canceled 集 | 源语义 | cancel / 集合溢出 | test_parity_async_late_arrival_r4.cpp [parity-sp2] | **本轨道修复（漂移 F-07）** |
| SP-3 | histogram_widget 异步直方图绑定提交时 generation（histogram_widget.cpp:72,286-289,321） | RsScanPool | UI→池→UI | setRasterLayer/setBand | 无独立 oracle（histogram 生产消费者为 preview/va 面非直连；间接覆盖 SP-1/SP-2 + S1） | — |

## E. RsToolbarFlowHost（src/app/widgets/rs_toolbar_flow_host.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| TB-1 | chip 可见性 ↔ toggleViewAction checked（main_window_docks.cpp:896-946 wantByBar） | QAction/toggleViewAction | 源→UI | layoutToolbarsUnderRibbon / applyVisibility（cpp:82） | test_parity_restore_failsafe_r4.cpp [parity-tb1] | — |
| TB-2 | applyVisibility 期间不回写 QAction（h:39-41 契约 + docks:903-922 QSignalBlocker/guard） | 结构单向契约 | 源→UI 单向 | applyVisibility | test_parity_restore_failsafe_r4.cpp [parity-tb1] | — |
| TB-3 | order/width ↔ QSettings `mainwindow/toolbarFlow/*`（cpp:153-172） | QSettings | 双向 | saveSettings/loadSettings | test_parity_restore_failsafe_r4.cpp [parity-re4] | — |

## F. TimelineScrubberWidget（src/app/widgets/timeline_scrubber_widget.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| TS-1 | 索引游标越界防护（cpp:26-36 setCurrentIndex qBound/拒绝） | `setTimelineDates` 列表长度 | 源→UI | setCurrentIndex | test_parity_state_mirror_r4.cpp [parity-ts1] | — |
| TS-2 | play/pause ↔ mTimer 活性 ↔ playbackFinished（cpp:49-81） | UI 内部播放态 | UI 内部契约 | play/pause/自然结束 | test_parity_state_mirror_r4.cpp [parity-ts2] | — |

## G. ComparisonWidget（src/app/widgets/comparison_widget.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| CW-1 | `modeChanged`/`flickerIntervalChanged` 发射合同（cpp:88-115） | setMode/setFlickerInterval | UI 内部契约 | setter | test_parity_state_mirror_r4.cpp [parity-cw1] | — |
| CW-2 | 模式 ↔ 渲染分支可见性（cpp:74-107） | 当前 mode | 源→UI | setMode | test_parity_state_mirror_r4.cpp [parity-cw2] | — |

## H. ProgressDialog（src/app/widgets/progress_dialog.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| PD-1 | 取消态：按钮禁用+文案+`cancelled()` 恰一次（cpp:90-98） | m_cancelled | UI 内部契约 | cancel | 既有 test_progress_dialog.cpp | — |
| PD-2 | 取消后迟到 max 进度**不得**以成功态 auto-accept（cpp:50-58 现无 cancelled 防护） | setValue 落地策略 | 源→UI 策略 | 晚到 setValue(≥max) | test_parity_async_late_arrival_r4.cpp [parity-pd2] | **本轨道修复（竞态类 5）** |

## I. RsEmptyStateWidget（真相在宿主翻页逻辑）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| ES-1 | 画布欢迎页页号（main_window.cpp:558-567） | `WorkbenchStateModel::phase()`（workbench_state.h:63）→ WorkbenchRules::canvasStackPage | 源→UI | phaseChanged（main_window_workbench.cpp:211-213）/工程开合/图层增删 | 无独立 oracle（宿主翻页三点静态接线，行号锚+独立评审；full-shell 逐页属 #1312 suite 职责） | — |
| ES-2 | 图层面板空态与画布页反相（main_window.cpp:569-576） | QgsProject 图层计数 | 源→UI | updateLayersEmptyState | 无独立 oracle（同 ES-1 理由） | — |
| ES-3 | RsJobPanel 空态（rs_job_panel.cpp:248-255,535,580,625）⚠ 真源是 UI 树行数 `topLevelItemCount()` 而非 TaskCenter | `TaskCenter` 任务集合（task_center.h） | 源→UI | taskAdded/Updated/移除路径 | 待核（ES-3 漂移存在性核验中，见 REVIEW_LOG） | **本轨道修复候选（漂移 F-10）** |
| ES-4 | 日志面板空态（log_panel.cpp:97-104,160,178） | mMessageCount | 源→UI | clear/logMessage | 无独立 oracle（同 ES-1 理由） | — |
| ES-0 | RsEmptyStateWidget CTA 门控（rs_empty_state_widget.cpp:101,184） | setActionVisible 状态 | 源→UI | actionClicked | test_parity_state_mirror_r4.cpp [parity-es0] | — |

## J. RasterLayerCombo（src/app/widgets/raster_layer_combo.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| RL-1 | 下拉项 ↔ QgsProject 栅格图层集（cpp:13-23 populate） | `QgsProject::instance()->mapLayers()` | 源→UI | populate()（打开对话框时） | test_parity_state_mirror_r4.cpp [parity-rl1] | — |
| RL-2 | 对话框存活期图层增删 ↔ 下拉刷新（现无 layersAdded/Removed 挂钩=漂移） | 同上 | 源→UI | layersAdded/layersRemoved | test_parity_state_mirror_r4.cpp [parity-rl2] | **本轨道修复（漂移 F-01）** |

## K. HistogramStretchWidget（src/app/widgets/histogram_stretch_widget.*）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| HS-1 | applyStretch 直写 `QgsRasterLayer` 渲染器（cpp:441,476）⚠ 无工程脏标记 | 渲染器 + 工程脏真相（QgsProject dirty） | UI→源 | applyToLayer | test_parity_restore_failsafe_r4.cpp [parity-hs1] | **本轨道修复（漂移 F-08）** |
| HS-2 | `stretchApplied` → canvas refresh（main_window_docks.cpp:266-275） | 拉伸应用事件 | UI→源 | applyStretch | 既有（stretchApplied→canvas refresh 接线，main_window_docks.cpp:266-275 静态锚） | — |

## L. SelectionContext 选择权威（src/app/workbench/selection_context.*，WP-B）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| SC-1 | mapCanvas currentLayer ↔ snapshot.activeLayer（selection_context.cpp:464-465） | SelectionContext（main_window_workbench.cpp:183 唯一实例） | 源→UI | currentLayerChanged→scheduleRefresh | test_parity_selection_authority_r4.cpp [parity-sc1] | — |
| SC-2 | layerTree 选择 ↔ snapshot.selectedLayers（selection_context.cpp:477-478） | 同上 | 双向 | QItemSelectionModel::selectionChanged | test_parity_selection_authority_r4.cpp [parity-sc2] | — |
| SC-3 | 8 个 notify* 提交链吸收（selection_context.cpp:491-566；调用点 main_window_workbench.cpp:376-686 等 14 处） | SelectionContext snapshot | UI→源 | 各面板 selectionChanged 信号 | test_parity_selection_authority_r4.cpp [parity-sc3] | — |
| SC-4 | `changed` 出口三消费：CommandRegistry refreshAll（mww:393）、InspectorHost（inspector_host.cpp:57）、WorkbenchStateModel（workbench_state.cpp:87） | SelectionContext::changed | 源→UI | refresh 合并窗口后 | test_parity_selection_authority_r4.cpp [parity-sc4] | — |
| SC-5 | 层移除清洗：权威单写 canvas（selection_context.cpp:450-452）⚠ active_view_host.cpp:105-112 第二写者待收敛 | 层移除事件 | 源→UI | handleLayerWillBeRemoved | test_parity_selection_authority_r4.cpp [parity-sc5] | **本轨道收敛（漂移 F-09）** |
| SC-6 | 150ms 去抖窗口内命令可用性最终一致（selection_context.cpp:625） | refresh 合并 | 源→UI | scheduleRefresh | test_parity_selection_authority_r4.cpp [parity-sc6] | — |

## M. 异步晚到落地策略（WP-C；竞态分类见 DECISIONS.md）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| AS-1 | STAC 搜索结果列表只反映最新查询代际（stac_client.cpp:165-229 现无代际） | StacClient 查询代际（本轨道新增单一原语） | 源→UI 策略 | finished 任意序 | test_parity_async_late_arrival_r4.cpp [parity-as1] | **类 4 修复（漂移 F-02）** |
| AS-2 | 超时错误晚到不得覆盖新成功结果（setTransferTimeout(10000) 的 error 到达） | 同上代际 | 源→UI 策略 | 超时后到达 | test_parity_async_late_arrival_r4.cpp [parity-as2] | **类 6 修复（漂移 F-03）** |
| AS-3 | 宿主对话框关闭后 in-flight 结果失效 + 丢弃留痕（stac_browser_dialog.cpp:37 现落地无检查） | 对话框生命周期 | 策略 | 关闭后到达 | test_parity_async_late_arrival_r4.cpp [parity-as3] | **类 2 修复（漂移 F-04）** |
| AS-4 | `layerAutoLoadRequested` 只落地发起会话（main_window_docks.cpp:679-683 现无会话检查；task_center.cpp:3148-3149 发射） | TaskCenter 任务 + 窗口会话代际 | 源→UI 策略 | markTaskCompleted | test_parity_async_late_arrival_r4.cpp [parity-as4] | **类 1 修复（漂移 F-05）** |
| AS-5 | SaveAs 后晚到自动加载不得进入新会话（saveProjectAsTo 会话代际推进） | 同上 | 源→UI 策略 | SaveAs 后到达 | test_parity_async_late_arrival_r4.cpp [parity-as5] | **类 3 修复（漂移 F-06）** |
| AS-6 | 取消后仍落地：TaskCenter 终态取消的完成回调消费面按取消态处置（rs_job_runner.cpp:44-66、cartography_dock.cpp:305-330 为正面样板；PD-2 为反例） | TaskStatus::Canceled | 源→UI 策略 | 取消后回调 | test_parity_async_late_arrival_r4.cpp [parity-pd2]+[parity-sp2] | **类 5（=PD-2+SP-2）** |

## N. restore-state 完备性（WP-E；#1312 B12 已收项去重）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| RE-1 | restoreState/restoreGeometry corrupt blob → 丢弃重写（main_window_misc.cpp B12） | 窗口状态字节串 | 源→UI | 启动恢复 | 既有 test_workbench_full_shell_lifecycle.cpp | **已收（#1312）**，不重复建 oracle |
| RE-2 | 半截/截断的 dock 布局字节串 → fail-safe 默认布局 + 诊断输出 | restoreState 输入 | 源→UI | 启动恢复 | test_parity_restore_failsafe_r4.cpp [parity-re2] | 本轨道补触发面 |
| RE-3 | 版本不符（新/旧版本号）会话数据 → 不解释不采用（B12 版本门 `==`） | 会话数据版本 | 源→UI | 启动恢复 | 既有 test_workbench_full_shell_lifecycle.cpp（newer-version 用例） | **已收（#1312）**，补独立用例证明 |
| RE-4 | TB-3 损坏的 toolbarFlow QSettings 值 → fail-safe 默认（rs_toolbar_flow_host.cpp:153-172） | QSettings | 源→UI | loadSettings | test_parity_restore_failsafe_r4.cpp [parity-re4] | — |

## O. 一致性压测与活文档 gate（WP-F/WP-G）

| ID | UI 控件 | 状态源 | 方向 | 触发时机 | oracle 引用 | 前序 |
|---|---|---|---|---|---|---|
| ST-1 | 选择风暴 300 步 × 每步投影不变量 | SelectionContext snapshot | 源→UI | 每步后 | test_parity_stress_r4.cpp [parity-s1] | — |
| ST-2 | 对话框开关风暴 200 步 × 每步镜像探针 | 结果/进度/空态/对比四件套 | 双向 | 每步后 | test_parity_stress_r4.cpp [parity-s2] | — |
| ST-3 | 图层切换风暴 120 步 × combo==工程真相 | QgsProject + RasterLayerCombo | 源→UI | 每步后 | test_parity_stress_r4.cpp [parity-s3] | — |
| GA-1 | PARITY_MAP 行数 ≥30 + 每行 oracle 引用可 grep | 本文件 | 活文档 gate | ctest | test_parity_map_gate_r4.cpp [parity-ga1] | — |

## 覆盖统计

- 映射对总数：**50**（GW6+SW3+RS3+SP3+TB3+TS2+CW2+PD2+ES5+RL2+HS2+SC6+AS6+RE4+ST4=52 行，其中 RE-1/RE-3 为已收引用行、ES-3 为待核行）
- oracle 用例（[parity-*] tag 可 grep）：**38** 个新增 + gate 1 = 39；另有 7 行以"既有 suite"或"无独立 oracle（理由）"诚实标注
- 九代表面板各 ≥1 对：GW✓ SW✓ RS✓ SP✓ TB✓ TS✓ CW✓ PD✓ ES✓
- 漂移修复登记（F-xx）：F-01 RL-2、F-02 AS-1、F-03 AS-2、F-04 AS-3、F-05 AS-4、F-06 AS-5、F-07 SP-2、F-08 HS-1、F-09 SC-5、F-10 ES-3、PD-2（=F-11，与 AS-6 同根）+ WP-E/压测新增待登记 → 目标 ≥12
