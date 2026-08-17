# Findings Log: UI / Rendering / Data / Interaction Audit

## Baseline
- BASE_SHA: 19843d1b6910c9207c7e5c97863a873db679368e
- 审计 worktree: .scratch/audit-worktree（detached, 只读）
- 状态图例：NEW=待复核 / DUP=与已有issue重复 / FP=误报 / LOW=低置信度 / CONF=已确认候选 / ISSUED=已提交

## 去重排除（子Agent报告后立即判定）
- B-F1 / E-F1 / E-F2（band combos 未填充→UB/永远失败）→ DUP #182（标题已明确覆盖 inputBands[-1] UB 或 always fails）
- E-F7（ribbon 运行全流程只 raise dock）→ DUP #215（标题明确覆盖 "运行全流程 only raises the dock"）
- F-F3（PythonScriptEditor QThread destroyed-while-running）→ DUP #237
- C-F4（cross_section/roi_statistics 死代码未编译）→ 不可达，不报
- D-F10（ExecutionResultCache 无 invalidation）→ 默认关闭且无消费者，不可达，不报（可并入相关 issue 附注）
- A-F2（工具切换错归属 job 结果）→ 保留（与 A-F1 不同 root cause，conf 0.6 待核）

## Agent A（窗体生命周期）
| ID | Sev | 状态 | 位置 | 摘要 |
|----|-----|------|------|------|
| A-F1 | P2 | NEW | shell/workflow_session_controller.cpp:288-378 | runFullWorkflow/runUpToNode 无 in-flight guard（runStep/onRunClicked 有），二次提交覆盖 m_activePipelineId，第一条 pipeline 终态事件被丢弃→无法取消/输出不提交/双 DAG 并发；pipeline canvas 右键 Run Up to 也不受 gate（与 F-F4 合并） |
| A-F2 | P2 | NEW | workflow_session_controller.cpp:406-534 + openTool:114-200 | openTool 不重置 m_pendingTaskId；A 工具 job 在飞行中切到 B 工具，A 完成把结果 applyJobResultToSession 到 B 的 session/step（conf 0.6） |
| A-F3 | P3 | NEW | main_window_project.cpp:67-69 | 每次 新建布局 泄漏 QgsLayoutDesignerDialog wrapper（WA_DeleteOnClose 只作用于内层 QMainWindow；wrapper parent 是主窗口活到退出）conf 0.9 |

## Agent B（渲染正确性）
| ID | Sev | 状态 | 位置 | 摘要 |
|----|-----|------|------|------|
| B-F2 | P2 | NEW | widgets/histogram_widget.cpp:60-65 + histogram_stretch_widget.cpp:193-216,439-441 | setRgbBands 从未被调用；通道/灰度 scope 始终用 band-1 统计应用到实际显示波段（组合4,3,2时红通道用band1范围→显示错误）conf 0.8 |
| B-F3 | P2 | NEW | display/qgis_display_manager.cpp:744-771 | relocateLayer 样式往返不校验波段数：源换更少波段文件后恢复越界 band index→空 block→图层消失/黑 conf 0.55 |
| B-F4 | P3 | NEW | widgets/histogram_widget.cpp:135-140,624-633 | 分段线性端点被强制覆盖为 data min/max，用户无法限制显示范围 conf 0.9 |
| B-F5 | P3 | NEW | core vendored qgscontrastenhancement writeXml 只存 min/max | piecewise/hist-eq 拉伸在任何 style 往返（relocate/项目保存）后丢失 conf 0.7（上游限制+app自定义函数无序列化钩子）|
| B-F6 | P3 | NEW | display/qgs_display_stretch.cpp:381-386 | min>=max 回退 0..255：常量 uint16 栅格显示全白（应为灰）；全 NaN float32 显示黑 conf 0.8 |
| B-F7 | P3 | NEW | qgs_display_stretch.cpp:290-292 + vendored CE clamp | PS色阶输入低于 dtype min 时 CE 内 clamp 可致 max<min→全黑 conf 0.7 |
| B-F8 | P3 | NEW | widgets/histogram_stretch_widget.cpp:127,143 | spinbox ±1e9 截断极端 float32/uint32 DN 值 conf 0.8 |

## Agent C（渲染/交互性能）
| ID | Sev | 状态 | 位置 | 摘要 |
|----|-----|------|------|------|
| C-F1 | P1/P2 | NEW | classification/qgsclassificationmainwindow.cpp:2991,3079-3087,269 | recomputeSpectralCurves：每个 ROI 像素每波段一次 1×1 RasterIO（GDALOpen 每次重开）；RsRoiCollection::changed 直连无防抖（JM 已有防抖未跟进）；10万像素×6波段≈60万次调用，近全幅 ROI 数千万次→GUI 冻结数秒-分钟。姊妹函数 recomputeJmMatrix 已迁移 row-grouped，此函数漏掉 conf 0.9 |
| C-F2 | P2 | NEW? | agent/workspace_snapshot.cpp:87-99 + agent/raster_display_service.cpp:32-57 | copilot Send / agent 波段合成工具在 GUI 线程全图 Min/Max 精确扫描（无 hasStatistics 快路径）conf 0.6 — 需对照 #218 正文判断是否已覆盖（#218 提到 band-composition rail）|
| C-F3 | P3 | NEW | panels/data_manager_panel.cpp:511-519,667-719 | 每条 asset 事件全树 clear+rebuild，批量导入 O(N²)，无 setUpdatesEnabled/合并 conf 0.75 |

## Agent D（数据管理）
| ID | Sev | 状态 | 位置 | 摘要 |
|----|-----|------|------|------|
| D-F1 | P1 | NEW | display/qgis_display_manager.cpp:287-312 + main_window.cpp:124 + active_view_host.cpp:271-285 | auto-display 策略无条件开启，显式 addLayer 路径再 add → 每个 手动添加/工程打开 重复两个图层/两份 View lease？conf 0.75 —— 必须复核（若有 layer_id_conflict 去重则不成立）|
| D-F2 | P1 | NEW | data/providers/virtual_raster_source_provider.cpp:271-280 + data_manager.cpp:964-967 + qgis_display_manager.cpp:645-654 | VRT 写入会话 QTemporaryDir；重启后 restore 用新 temp 路径，QGIS 持久层指向旧 /tmp 路径失效→adoptLayer 拒绝 Ready 资产→整个 SICNU read 报 failed，无效图层残留树中 conf 0.75 |
| D-F3 | P1 | NEW | processing/framework/output_committer.cpp:113-130 + data_manager.cpp:260-264 | 输出路径已存在时静默 QFile::remove 覆盖；registerSource dedup 返回旧 AssetId（reusedExisting）→revision 不进、structure 不更新（陈旧 dims/CRS/nodata）、旧 provenance 被覆盖 conf 0.8 |
| D-F4 | P2 | NEW | layer_tree_menu.cpp:44-45,75-81 | QGIS 默认 移除/重命名/移动到顶底 动作绕过 DisplayManager（无 RegistryBridge）→移除不释放 lease（panel 引用=1，unload 被拒）、改名不同步 asset displayName、拖拽重排后 viewRecord->layerIds 脱节、保存重载复活图层 conf 0.7 |
| D-F5 | P2 | NEW | project_context.cpp:289-328 | clearProject 级联卸载无在飞任务 guard：长任务运行中开新工程→lease 被强制回收，任务完成后 commit 失败→publish 回滚删除输出文件（用户输出静默消失）conf 0.6 |
| D-F6 | P2 | NEW | data_manager.cpp:1325-1366 + qgis_display_manager.cpp:700-823 | 矢量图层编辑中 promote/relocate → assetChanged→relocateLayer 移除旧图层（销毁未提交 edit buffer）无提示 conf 0.7 |
| D-F7 | P2 | NEW | data_project_serializer.cpp:141-163,303-343,484-527 | 相对路径工程移动后：QGIS 层从新位置渲染，asset 仍绑死旧绝对路径（Missing），DataManager 消费者全错位，再保存永久固化 conf 0.6 |
| D-F8 | P2 | NEW | data/data_manager.cpp:260-264 全链 | 资产 structure 注册时一次快照；同路径文件替换永不检测（无 mtime/size），陈旧 dims/CRS/nodata 流入 VRT preflight/panel/agent；histogram_widget.cpp:175-181 GDAL 句柄只按 source 字符串缓存 conf 0.85 |
| D-F9 | P3 | NEW | data_manager.cpp:437-476 | relocate 依赖 VRT 再生成失败静默 continue；再生成文件不重新 GDAL 校验；依赖资产 CRS 不刷新（structuresCompatible 忽略 CRS）conf 0.7 |
| D-F11 | P3 | NEW | data/providers/ogr_vector_source_provider.cpp:42-73,179-180 | 每次矢量注册用 GDAL_OF_UPDATE 探测可编辑性：只读介质上能力丢失；可能在用户输入旁产生锁文件 conf 0.5 |
| D-F12 | P3 | NEW | data_project_serializer.cpp:295-301,344-345,529-531 | 单个资产解析失败置 failed=true 但此前已恢复一半（assets+layers+leases），UI 仅警告，无回滚 conf 0.9 |

## Agent E（UI 状态与工作流）
| ID | Sev | 状态 | 位置 | 摘要 |
|----|-----|------|------|------|
| E-F3 | P2 | NEW | main_window_vector.cpp:287-304 | saveEdits：commitChanges() 失败弹警告后**无条件** statusBar "Edits saved"——矛盾反馈；与 E-F11 同族（toggleEditing 停止编辑 Save 失败无任何提示且状态条说 Started editing）conf 0.95 |
| E-F4 | P2 | NEW | main_window_connections.cpp:328-340 | onProjectRead 用工程里第一个矢量层驱动 updateEditingUI 而非 currentLayer → 编辑按钮 checked/enable 与实际操作对象脱节（光栅当前层时数字化工具新可用）conf 0.7 |
| E-F5 | P3 | NEW | main_window_connections.cpp:414-418 | 点击图层树 group/空白清空 canvas currentLayer → 编辑会话仍在但工具栏全灰、Ctrl+E 报 No vector layer conf 0.8 |
| E-F6 | P2 | NEW | main_window_project.cpp:120-140 | saveProject/saveProjectAs 忽略 QgsProject::write() 返回值无条件 "Project saved"（SICNU 目录写失败反而有报——不一致）；只读路径/满盘→假成功→数据丢失 conf 0.95 |
| E-F8 | P2 | NEW | workflow_session_controller.cpp:288-324 + task_center.cpp:1145+ | 运行全流程/Run Up to 用 definition 默认参数执行，任务面板 form 值（m_runtime.setParams）从不拷入 targetDef conf 0.6 |
| E-F9 | P3 | NEW | map_tools/measure_tool.cpp:32-33,153 | 测量结果模态 QMessageBox 抢焦点；CRS/椭球构造时快照，改工程 CRS 后测量仍用旧 CRS conf 0.85 |
| E-F10 | P3 | NEW | main_window_vector.cpp:110-126 | undo/redo 无 enabled 绑定；光栅当前层 Ctrl+Z 完全无反馈（else 分支只在 layer 为 null 时提示）conf 0.9 |
| E-F12 | P3 | NEW | active_view_host.cpp:435-451 | 移除图层对外部矢量层无 isEditable/isModified 检查→未保存编辑静默丢弃 conf 0.6 |
| E-F13 | P3 | NEW | main_window_menus.cpp:407-415 + extract_band_dialog.cpp:60-72 | 提取波段对话框忽略菜单传入的当前层预选（combo 构造时填充一次）conf 0.8 |
| E-F14 | P3 | NEW | map_tools/rs_roi_spectrum_tool.cpp:29-59 | ROI 均值谱工具无 Escape 取消进行中多边形；切工具静默丢弃草图 conf 0.8 |

## Agent F（异步/线程）
| ID | Sev | 状态 | 位置 | 摘要 |
|----|-----|------|------|------|
| F-F1 | P1 | NEW | dialogs/sicnu_algorithm_dialog.cpp:739-777 | worker 捕获 dialog 成员裸指针 &mContext / feedbackPtr（dialog 无 reject/close guard，WA_DeleteOnClose）→运行中关对话框=worker 线程 UAF。Agent A 认为与 #220 同序列，需读 #220 正文裁定是否独立 root cause conf 0.8 |
| F-F2 | P2 | NEW | obia/rs_obia_main_window.cpp:418,845-883,890+ | OBIA 平面分类无模态运行中 loadRaster 无 isBusy guard→A 图分类结果应用到 B 图会话（applySegmentationResult 还把 A 的 segMap 配 B 的 geotransform）conf 0.75 |
| F-F4 | P2 | 合并A-F1 | 同 A-F1 | 同一问题（pipeline canvas 右键路径不受 gate）|
| F-F5 | P3 | NEW | selecttools/qgsmaptoolselectutils.cpp:539-560,648-660 | setFuture 阻塞 GUI 等上一个搜索；旧结果丢失菜单卡 "Searching…"；无 generation check conf 0.6 |
| F-F6 | P3 | NEW | obia/rs_obia_main_window.cpp:708-722,1252 + georeferencer/rs_georeferencing_session.cpp:335 | 工具栏取消平面分类任务：m_pendingFlatTask=nullptr 无 deleteLater→QgsTask 泄漏（georef warp-task 已在 #238 提及 rejected submission 泄漏——OBIA cancel 路径独立）conf 0.8 |

## 优先验证队列（Tier 1）
C-F1, D-F2, D-F3, D-F1(复核guard), E-F3(+E-F11), E-F6, B-F2, F-F1(对照#220正文), A-F1(+F-F4), F-F2, D-F4, D-F8

## Lead 复核结论（Phase 4，2026-08-16）
- C-F1 CONF 0.9：直连无防抖（:268）+逐像素 1×1 RasterIO（:3079-3087）+GDALOpen 每次重开（:3021）。JM 有 500ms 防抖（:259-263）为同文件先例
- D-F1 CONF 0.85：addLayer 无去重（qgis_display_manager.cpp:406-517 全读无检查）；auto-display 唯一开关=main_window.cpp:124 无条件 true；openSource=registerSource(emit assetAdded→排队自动add)+显式addLayer（active_view_host.cpp:271-285）；测试仅覆盖单独自动路径（test:830-857）断言恰好 1 层
- D-F2 CONF 0.85：restoreVirtualRaster 每会话新 QTemporaryDir（data_manager.cpp:964-967）注释声称确定性路径但跨重启不成立；adoptLayer :645-654 Ready+invalid→硬失败 display.layer_not_adoptable；设计注释（:641-644）只考虑 Missing→relocate 角落
- D-F3 CONF 0.85：output_committer.cpp:103-104 静默 QFile::remove 已存在输出；registerSource dedup :260-264 返回旧 id 无 revision/structure/provenance 更新。D-F8（外部替换同路径无 mtime 检测）并入同 issue 作第二触发路径
- D-F4 CONF 0.8：layer_tree_menu.cpp:45,75-81 用 QGIS 默认动作；全 src/app 无 QgsLayerTreeRegistryBridge 实例化；store 移除仅 qgis_display_manager.cpp:812/859 + active_view_host.cpp:450
- E-F3/E-F11 CONF 0.95：main_window_vector.cpp:292-303 commit 失败仍无条件 "Edits saved"；:257-259 停止编辑 Save 失败无提示且状态条说 Started
- E-F6 CONF 0.95：main_window_project.cpp:125-126/137-138 write() 返回值忽略无条件 "Project saved"
- B-F2 CONF 0.85：qgs_display_stretch.cpp:219-221 bandMin/Max 初始=参考波段值；仅 MasterRgb 归一化（:226-247）、PercentClip/StdDev/HistEq 按目标带重算（:262-284）；LinearMinMax/Levels/Piecewise 在 Red/Green/Blue/Gray scope 直接错位；histogram_stretch_widget.cpp:201-216 多波段时 bandCombo 禁用强制 band1；setRgbBands 无调用方
- A-F1/F-F4/A-F2 CONF 0.85 合并：onRunClicked 有 guard（:223-224）而 runFullWorkflow(:288)/runUpToNode(:326) 无；openTool(:114-200) 不清 m_pendingTaskId/m_runInFlight；showTool 重启用 Run；onTaskUpdated(:406+) 按 m_activeSession 归属错位。一个 root cause：运行状态机字段无协同守卫
- F-F1 CONF 0.85：sicnu_algorithm_dialog.cpp:739-740 裸捕获 &mContext/feedbackPtr 进 JobEngine executor；:758-759 worker 解引用；非模态 WA_DeleteOnClose 无 close guard。与 #220（GUI 侧回调重入）不同机制
- F-F2 CONF 0.8：loadRaster(:418) 无 isBusy（其余动作 :538/1064/1268/1392 均有）；startFlatClassifyTask 仅 WaitCursor 无模态框（:1395-1408）；onObiaTaskUpdated(:726) 匹配未重置的 m_pendingTaskId
- E-F8 CONF 0.85：WorkflowSession::setParams 存 m_paramsByStep（workflow_session.cpp:91-94）；task_center.cpp:1188 pipeline 执行读 step->params（definition）；runFullWorkflow 直接 submitPipeline(*def) 无合并
- C-F2 CONF 0.75：workspace_snapshot.cpp:87-99 注释引 ADR 0008 声称 Min|Max 不阻塞，但 sampleSize=0 默认仍精确全扫；raster_display_service.cpp:44-46 同型。与 #218 不同站点（agent/copilot 路径），互为补充

## 拟提交 Issue 列单（待对抗复核）
1. [Classification][P2][Perf] C-F1 光谱曲线逐像素 RasterIO 冻结
2. [Data][P1][Integrity] D-F1 auto-display 与显式添加双重加层
3. [Data][P1][Integrity] D-F2 VRT 工程重启后恢复失败
4. [Data][P1][Integrity] D-F3(+D-F8) OutputCommitter 覆盖+陈旧资产身份
5. [Lifecycle][P1][UAF] F-F1 SicnuAlgorithmDialog worker 裸指针
6. [UI][P2] E-F3+E-F11 提交失败假成功反馈
7. [UI][P2] E-F6 保存工程忽略 write() 结果
8. [Rendering][P2][Correctness] B-F2 通道拉伸波段错位
9. [Workflow][P2][State] A-F1+A-F2+F-F4 运行状态机三缺口
10. [Workflow][P2][Correctness] E-F8 全流程用默认参数
11. [OBIA][P2][State] F-F2 运行中换图陈旧结果
12. [Data][P2][Integrity] D-F4 右键默认动作绕过 DisplayManager
13. [Rendering][P3][Perf] C-F2 agent/copilot 精确统计扫描（补 #218）
14. [P3 批量A-UI] E-F4,E-F5,E-F9,E-F10,E-F13,A-F3,D-F12
15. [P3 批量B-渲染/面板] B-F4,B-F6,B-F8,C-F3,F-F6
