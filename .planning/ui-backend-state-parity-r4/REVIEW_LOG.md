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
