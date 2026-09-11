# CAPABILITY MATRIX — 9.0 目标能力 vs master 现状

逐项证明"计划新增"的能力是否已存在（implementation / production caller / surface / tests /
docs / bounds）。**禁止重复实现已存在能力。**

| 能力 | master 现状（基点 8f6293bceb） | 9.0 增量 |
|---|---|---|
| 增量准入（active counters、ready heap、scan budget） | ✅ 8.0（task_center） | 无需新增；transient 绕过需接入门 |
| worker 子任务 transient allowance（JobEngine 层） | ✅ #798（m_transientAllowance ≤8） | 已存在，不重复实现 |
| worker 子任务 transient allowance（**TaskCenter 准入层**） | ❌ #862 仍复现 | **M0 新增**（kMaxTransientChildren=8，镜像 #798 语义） |
| worker 线程同步等待防护：JobEngine::waitForJob | ✅ #798 立即拒绝 | 已存在 |
| worker 线程同步等待防护：TaskCenter::waitForTask/Pipeline | ❌ 可阻塞 30min | **M0 新增** typed 拒绝 |
| 显式 owner 层级（structured hierarchy） | ❌（仅 DAG parentTaskIds） | **M1 新增** ownerTaskId + m_ownedChildren |
| join 规则（owner terminal ⇒ child 终态） | ❌ | **M1 新增**（setTaskStatusLocked terminal 钩子 + flushOwnedCancels） |
| 取消传播含 owned 子树 | ❌（仅 DAG 后代） | **M1 新增**（collectTransitiveDescendants 走双边） |
| 锁外通知（TaskCenter signals） | ✅ flushPendingSignals | 已存在（模式来源） |
| 锁外通知（coordinator runStateChanged） | ❌ #860 持锁 emit | **M0 新增** 通知队列 + drain |
| ghost run 终态闭合 | ❌ #876 | **M0 新增**（swap 前 Canceled 广播 + 持久化） |
| DataManager 跨线程快照 | ✅ #852 修复（CatalogSnapshot+generation） | M2 契约收尾：移除过时 reader 警告 + generation 测试 |
| DataManager generation 公共访问器 | ✅ catalogGeneration() | 已存在，不重复实现 |
| 类型化取消原因 | ⚠ 仅文本 | **M3 新增** TaskCancelReason 枚举贯通 |
| terminal 单调守卫 | ⚠ 上游散查 | **M3 新增** seam 级守卫 + trace |
| pause/resume typed 行为 | ✅ #702 拒绝式 | 补测试钉住，不改语义 |
| checkpoint 原子写 + fault point | ✅ temp+rename + SICNU_FAULT_POINT | 已存在 |
| checkpoint 损坏 typed 拒绝 | ✅ loadCheckpoint typed error + quarantine | 已存在，补测试 |
| resume 身份门（impl stamp/digest/moved-output） | ✅ 8.0 WP-E | 已存在，不重复实现 |
| admission 快照（拒绝原因） | ✅ admissionSnapshot | **M5 扩展** transient 字段 |
| 实际用量观测 | ❌ 仅估计 | **M5 新增** terminal RSS trace（有界） |
| 执行诊断 dump | ❌ | **M7 新增** explainDump/explainRun |
| 规模矩阵（10k/deep/wide/cancel storm） | ⚠ ep7/ep8 部分 | **M8 扩展**（300 链/cancel storm/100k 门控） |

结论：9.0 的真实增量 = TaskCenter 准入层 transient 接缝 + 显式 owner 层级/join + coordinator
通知队列/ghost 闭合 + 类型化取消 + 诊断/观测 + 规模矩阵。其余为 8.0 已交付能力的复验、钉测与文档化。
