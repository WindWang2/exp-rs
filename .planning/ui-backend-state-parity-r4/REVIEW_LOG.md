# REVIEW_LOG — ui-backend-state-parity-r4

独立对抗性评审与本轨道自查的记录。格式：轮次 | 审查面 | 发现（P0/P1/P2）| 处置。

## 自查记录（执行中持续追加）

### S-1 ES-3 漂移存在性核验（2026-09-27）

- 假设（来自普查）：RsJobPanel 空态以 UI 树行数为真源、仅 3 点翻页，存在"有任务却显示空态"漏通知。
- 核验：rs_job_panel.cpp:162-164 taskAdded/taskUpdated Queued 连接 → onTaskAdded 插行路径与移除路径都汇入共享更新尾（:535/:580/:625 的 `m_treeStack->setCurrentIndex(topLevelItemCount>0?0:1)`）。任何插行/删行必经其一。
- **判定：无真实漂移。** F-10 从修复清单撤销；PARITY_MAP ES-3 行标注"已核无漂移"。教训：普查嫌疑点必须逐个核验，不凑修复数。

### S-2 SC-5 收敛方向核验（2026-09-27）

- 假设：active_view_host.cpp:105-112 与 selection_context.cpp:450-452 对同一 canvas 的 layerWillBeRemoved 清理是双写。
- 核验：两处均写 `setCurrentLayer(nullptr)`（同值、幂等、同为 #778 UAF 防护）；ActiveViewHost 生产构造仅 main_window.cpp:147（SelectionContext 恒在场）；tests 中 ActiveViewHost 无一处钉住该守卫行为（grep currentLayer 于三个 ActiveViewHost 测试 = 0 命中）。
- **判定：可安全收敛**——删 ActiveViewHost 重复写者，权威成为 canvas 现势层清理的单一写者（F-09）。幂等同值写本无漂移，收敛的收益是职责单一化（canvas 归属清理归权威），评审质询点已记录。

（后续独立 subagent 评审记录见下）

### S-3 独立对抗性评审（Phase 5，只读 subagent，2026-09-27）

评审输入：`git diff origin/master..HEAD` 全量 + 白名单/下限核查。结论：**needs-fixes**（P0×2、P1×4、P2×6）。

证实干净面：领地零越界（main_window.cpp/src/agent 0 行）；40 个 oracle tag 真实可数、gate 双向校验真实；
抽查 10 个 oracle 无同义反复（期望值均来自 stub/canvas/QgsProject/result 等后端真源）；F-07 水位线数学正确；
F-11/12 双重防护正确。

| 级 | 发现 | 处置 |
|---|---|---|
| P0-1 | 提交 54be9577c（错标 docs）夹带 stac_client 代际自增与 combo QueuedConnection 两个行为 hunk | **已修**：变基拆分（5134bd0af 行为修复 / ba531a742 行为修复 / 8443bc738 纯文档），分支未推送故历史重写安全 |
| P0-2 | 7d9c460a1 引用不存在的 qOverload 独立不可编译，对抗"每提交可编译"声称；修复计数 13 vs 撤销后 12 失真 | **已修**：变基折叠 1fd138d01 入 7d9c460a1；计数改为"登记 13、撤销 F-10 → 落地 12 项修复"（EVIDENCE §2 与账本同批改） |
| P1-1 | 会话纪元 FIFO 弹出在失败/取消任务留孤儿条目时误丢后续合法落地 | **已修**：信号加 taskId（默认参数保兼容），落地按 id 精确配对（commit "auto-load requests pair by task id"）；AS-4/AS-5 改配对调用 |
| P1-2 | 提交数 16 < 下限 18 | **已修**：拆分+折叠净变化 +2，再叠加本表记录与后续修复提交（当前 ≥20） |
| P1-3 | F-04 只覆盖 closeEvent；Esc/reject/done 不经过 closeEvent | **已修**：改 hideEvent（所有离屏路径含 close），注释说明；生产调用点为模态 exec 的局限已在 EVIDENCE §1 披露 |
| P1-4 | 评审工件缺失（本文件止于占位行） | **已修**：即本记录 |
| P2-1 | m_searchGeneration 有符号 | **已修**：quint64 |
| P2-2 | cancel 不再擦除 owner 记录的滞留 | **裁决不改码**：擦除会重新打开 SP-2 的窗口（记录是 canceled-but-not-superseded 代的唯一锚）；滞留有界（每活跃 owner 一条）+ 地址复用需同代未取消才误判，窗口极窄。记入 DECISIONS D-9 |
| P2-3 | combo 刷新的 -1 瞬态 | **已修**：QSignalBlocker 包裹重建 + 单次重选 |
| P2-4 | PARITY_MAP 登记尾漏 F-12/F-13、F-10 未撤、ES-3 矛盾、计数枚举不一致 | **已修**（见 PARITY_MAP 漂移登记节修订） |
| P2-5 | 256 上限依赖 taskId 单调 | **已注**：代码注释声明不变量（单调 ids，唯一重置点是测试专用 shutdown） |
| P2-6 | AS-2 真实 10s 等待是负载抖动源 | **裁决不改**：D-3 已声明（诚实优先于速度），留待后续超时注入缝 |

P2-2/D-9：scan-pool owner 记录滞留的取舍——cancel 时擦除 owner 记录曾让"canceled 且未被取代"的代失去唯一 staleness 锚（SP-2 红）。保留记录的代价是每活跃 owner 一条微小滞留；地址复用误读需要【同地址复用 + 该代未取消 + 1024 次 churn 存活】三条件同时成立，风险可接受，登记为已知取舍。

最终：P0 全消、P1 全消、P2=1 改码 3 修文档 2 裁决不改（有据）。
