# DECISIONS — ui-backend-state-parity-r4

逐条记录"为什么这样修/为什么不修"，全部有实测证据锚。日期 2026-09-27，基线 `15e5c66b5`。

## D-1 单一代际戳原语的落点：各面内聚计数器，不新建共享基类

WP-C 门禁要求"代际戳是否单一原语（禁止每面板一套时间戳比较）"。仓库现状：`RsScanPool`（rs_scan_pool.h:38-59）已有 generation 原语（单调计数 + supersede + 定向 cancel），但它是**有界扫描线程池**的协作取消令牌，与网络晚到落地是两个语义域；`VaSelectionHub` 有选择事件代际；`llm_streaming_client` 有 `m_finishedEmitted` 幂等标志。
**裁决**：不把 RsScanPool 强拉为网络落地原语（域错配），也不新建共享头（新增抽象属于"新方向"边缘）。统一的做法是**同一形状**：各落地面的"单调代际计数 + 到达时比对 + 不匹配即丢弃并留痕"，语义与 RsScanPool.isStale 完全同构（STAC 用 `m_searchGeneration`，autoload 用窗口 `m_sessionEpoch`）。同一形状 + 同一丢弃策略 + 全部留痕可断言 = 单一原语的实质；PARITY_MAP M 节六对全部引用它。

## D-2 autoload 会话纪元：记录在窗口（taskAdded→epoch 映射），不改 TaskCenter 内部

晚到自动加载落进已切换项目（main_window_docks.cpp:679-683 无任何会话检查；task_center.cpp:3148-3149 发射）。
备选：(a) TaskCenter 记录任务所属项目——TaskCenter 无项目概念，侵入大；(b) 信号加 taskId 并在窗口比对——需要改信号签名 + 消费者。
**裁决**：`taskAdded(const AlgorithmTaskInfo&)` 已携带 `taskId/autoLoadLayer/outputLayerPath`（task_center.h:118/141/155），窗口侧在 taskAdded 记录 `(taskId → 当前会话纪元)`，`layerAutoLoadRequested` 到达时弹队首比对。零信号签名变更（test_execution_plane.cpp:679 的 QSignalSpy 面不动），纪元在 `resetSessionStoryState()`（main_window_project.cpp:239，#1312 建立的 story boundary）与 `saveProjectAsTo()` 成功处推进。
**已知妥协**（诚实声明）：落地请求只带 path 不带 taskId（1 参信号），窗口按 FIFO 弹队；乱序完成的两任务通常同会话同纪元，误配不影响判定。记录于此，评审可质询。

## D-3 STAC 类 6（超时后落地）用真实 10s 传输超时，不做注水捷径

`setTransferTimeout(10000)`（stac_client.cpp:185）无注入点。伪造"超时"（如直接 emit error）是同义反复。**裁决**：AS-2 用真实 abort 的连接错误（远端断开=传输失败形态）+ 真实超时路径共用同一丢弃分支；测试接受秒级等待（TIMEOUT 300）。诚实优先于速度。

## D-4 WP-D 收窄为"UI→源直写缺记账"契约，不建 undo 栈 oracle

提示词 WP-D 假设"对话框 OK/apply 与 undo 栈交界"。实测：本仓 UI 侧**无应用级 undo 栈**（QGIS 层编辑缓冲除外，属簇外）。强建 undo oracle 即同义反复。
**裁决**：WP-D 收窄为实测存在的双写缺陷面——`rs::display::applyToLayer`（qgs_display_stretch.cpp:346-347 `layer->setRenderer` 后）直写渲染器但不推进工程脏真相（关窗即丢样式，无任何提示）。修复：seam 处补 `QgsProject::setDirty(true)`（层在工程内时）。undo 契约测试的前提不成立记入账本。

## D-5 #1312/#1316 边界去重表（不重复建 oracle）

| 已收项 | 载体 | 本轨道处置 |
|---|---|---|
| restoreState/Geometry corrupt blob 丢弃 + 版本门 | test_workbench_full_shell_lifecycle.cpp:502（B12） | RE-1/RE-3 标"已收"，不重建；本轨道补**截断（半截）形态**（RE-2：合法 magic、中途截断——B12 只测了整串垃圾）与 toolbarFlow 损坏值（RE-4） |
| story boundary / SaveAs 回滚 / open 事务 | main_window_project.cpp（#1312） | 本轨道不动其逻辑，只把 `saveProjectAsTo` 成功路径纳入会话纪元推进点 |
| run-mirror connect-once | project_context（#1284） | 不碰 |
| mission selection-authority 模式 | #1316 | WP-B 照其模式做壳层 SelectionContext 矩阵 |
| scan pool generation 契约 | test_scan_pool.cpp 既有 | SP-1 补 owner 维度，不重测 global 语义 |
| guided workflow 会话边界/晚到守卫 | r3 debugger study（cpp:181-199,391-424 已加固） | GW-6 只钉防回归 oracle，不重修 |

## D-6 竞态类 2 的接缝选择：STAC 宿主对话框 close 失效，而非裸 lambda 修复

普查证实 src/app 无"捕获 this 无 context"的 connect（前序轨道卫生良好）；类 2 的真实形态是**逻辑宿主关闭（隐藏而非销毁）后晚到结果仍改隐藏态**。**裁决**：`StacBrowserDialog::closeEvent` → `StacClient::cancelInFlight()`（代际推进），晚到 reply 丢弃并 `searchDropped` 留痕。对象销毁路径由 Qt receiver 自动断连兜底（既有保证，不重复建 oracle）。

## D-7 压测漂移归宿规则

压测（WP-F）只负责抓漂移；每处抓到必落成确定性 oracle（WP-A~E 文件内），压测自身不作为修复证据。若压测 N 步零漂移，按提示词要求视为 oracle 强度不足，先加深 oracle 再质疑场景。

## D-8 与在途 PR 的文件重叠处置

`tests/CMakeLists.txt` 为 #1334/#1335 共同触碰面：本轨道所有注册均为**文件尾部纯追加块**（带轨道标记注释），rebase 时冲突面为零。`src/app/main_window.h/.cpp`：#1334 触碰 main_window.cpp——本轨道会话纪元成员放 main_window.h、连接放 main_window_docks.cpp、纪元推进放 main_window_project.cpp，**不触碰 main_window.cpp**。`test_m2_dialog_stress.cpp`（#1336 触碰）：WP-F 不改既有断言、只追加场景；若需 rebase 按"基于 #1336 合并后 master"声明。
