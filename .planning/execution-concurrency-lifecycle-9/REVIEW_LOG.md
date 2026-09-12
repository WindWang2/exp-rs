# REVIEW LOG — execution-concurrency-lifecycle-9

## 自审记录（主 Agent，开发中持续）

### 自审 R1（M0/M1 实现后，编译前）

| 发现 | 严重度 | 处置 |
|---|---|---|
| ghost 闭合处误加嵌套 lock_guard（swap 块已持 m_mutex）→ 自死锁 | P0 | 已修：去掉嵌套锁，注释说明锁作用域 |
| globalMax/RSS gate 的 `break` 会遮蔽堆中更深处的 transient child（优先级遮挡恢复死锁） | P0 | 已修：mark-and-continue，被 hold 的 head 重排且不推进 epoch；评审 B-P3-1 后补 O(1) 快速路径 |
| join 规则下 engine 通用 Cancelled 记录会把级联已盖的 StructuredJoin/Upstream 类型戳覆盖为 User | P1 | 已修：markTaskCanceled 仅在 None 时落参 reason |
| cascadeCancelTargetsLocked 根任务 cancelReason 硬编码 User → shutdown teardown 根任务被误标 | P1 | 已修：全 targets 取调用方传入 reason |
| 孤儿子任务 executor 不观察 isCancelled 时滞留 Cancelling（合作式取消契约） | P2 | 测试改为观察 ctx.isCancelled()（生产行为正确，属操作符契约） |
| 移动 build 需要 -fpermissive（canonical_metadata GDAL 类型不匹配，geospatial 非 ownership） | 记录 | 构建配置对齐主构建，不改代码 |
| explainRun 初版 resuming 判断逻辑错误 | P3 | 已修 |
| ep9 测试 explain 断言使用了不存在的 runId 格式 | P3 | 已修 |
| 测试内 disconnect-all 会剪断 coordinator↔TaskCenter 粘性连接（跨用例污染） | P2 | 已修：context-owned connection |
| #860 修复第一版把 drain 写在 fold 锁作用域内 → 全 coordinator 套件挂死 | **P0（被回归测试当场抓住）** | 已修：fold 显式作用域 + 锁外 drain；gdb 栈实证（worker 在 drainRunNotifications 内自等 pthread_mutex_lock） |

## 对抗评审（实现完成后，2 个只读 subagent 并行）

### Reviewer A — architecture / correctness / concurrency（结论：not-mergeable → 修复后解除）

| # | 严重度 | 发现 | 处置 |
|---|---|---|---|
| A-F1 | **P1** | explainDump 在锁外调用 `limitForProfileLocked`（...Locked 契约 + QMap 裸读竞态 setResourceProfileLimit） | **已修**：limits 快照进 Snapshot，锁外用副本解析 |
| A-F2 | P2 | DataManager 注释过度声明快照覆盖：leaseCount/leases/hasActiveEditLease/planUnload 直读 live 容器；catalogGeneration 回退裸读 | **已修**：四 reader 加 `checkLeaseReaderAffinity` 高声标记；注释如实区分两类 reader；generation 只走快照 |
| A-F3 | P2 | transientChildren 计数混淆 bypass 与普通准入的 worker 任务 → 8 上界失真、I4 减弱 | **已修**：`transientBypass` 在 dispatch 时盖戳（同 isolatedRoute 纪律），计数只算 bypass 准入 |
| A-F4 | P3 | ghost 闭包在 coordinator 锁内做 checkpoint I/O（与全文件模式一致；persist-before-broadcast 是顺序保证） | 接受并记录（REVIEW_LOG/FINAL_REPORT 已声明窗口缩小但未闭合的 crash window） |
| A-F5 | P3 | owned-cancel 级联按 ownership 链深递归（已验证可终止、无锁环） | 接受并记录（深度受 transient 有界性自然约束；M8 嵌套用例覆盖两层） |
| A-F6 | P3 | engine 侧无请求戳的 Cancelled 记录被标 User | **已修**：新增 `TaskCancelReason::Engine` |
| A-F7 | P3 | resumeRunImpl public——绕过 wrapper 会滞留通知 | **已修**：转 private |
| A-F8 | P3 | coordinator m_mutex → TaskCenter m_mutex 嵌套未文档化 | **已修**：锁序注释落在成员声明处 |
| A-F9 | P3 | explainDump 临界区 O(全任务) | 接受：注释声明为诊断专用有意停顿；文档已修正措辞 |
| A-F10 | P3 | 文档"禁止 sleep"与有界轮询实现之间的表述差 | 接受：TEST_MATRIX/ARCHITECTURE 措辞已注明"有界轮询后断言" |

评审 A 确认存活的关键面：全部 drain 点锁外；无残留持锁 emit；ghost 闭合 crash 窗口收窄；
flushOwnedCancels ↔ dispatch ↔ cancel → listener 递归链无死锁可终止；ownership 边不可成环；
flock 契约全路径先 flock 后 m_mutex；terminal 单调守卫不与 auto-retry 冲突。

### Reviewer B — tests / performance / portability / docs-vs-code（结论：mergeable，1 P1）

| # | 严重度 | 发现 | 处置 |
|---|---|---|---|
| B-P1-1 | **P1** | #862 reproducer 旧代码失败非确定（父任务超时路径仍完成，子任务迟后完成可错过检出的竞态窗口） | **已修**：成功路径独占 `parentSawChildTerminal` 戳，REQUIRE 该戳——旧代码必失败成为确定性事实；文档三处声明同步成立 |
| B-P2-1 | P2 | terminal RSS 采样在 tracing 关闭时仍每完成解析 /proc | **已修**：Trace::enabled() 门控 |
| B-P2-2 | P2 | FINAL_REPORT/VERIFICATION/MILESTONES/REVIEW_LOG 未提交 | **已修**：随本 commit 入库 |
| B-P3-1 | P3 | 饱和池每候选 RSS 读；mark-and-continue 丢 O(1) 快路径 | **已修**：探测提升每 pass 一次 + 无 bypass 容量时恢复 break |
| B-P3-2 | P3 | explainDump O(全保留任务)（非 O(live)） | 文档修正（代码维持诊断专用取舍） |
| B-P3-3 | P3 | 100k 门控用例静默 return 被 ctest 记为 pass；teardown 尾巴 | **已修**：SKIP() 如实上报；实测 teardown 6.1s 有界 |
| B-P3-4 | P3 | TEST_MATRIX 缺两个 M6 用例行 | **已修**：补行 |
| B-P3-5 | P3 | CMake RUN_SERIAL 理由错误 + SICNU_WORKER_EXE 死定义 | **已修** |
| B-P3-6 | P3 | 两个弱断言（负向词缺席 / 恒真头字面量） | **已修**：正向标记 + finalize 后的 runLocks held: 0 |
| B-P3-7 | P3 | FINAL_REPORT I8"一切有界"过度声明 | **已修**：措辞限定为调度资源；任务 map 保留策略沿既有 |

评审 B 确认：#860 重入测试对旧代码是真实死锁；#876 测试确定性；join/取消传播的终态有保证非竞态；
静态原子量全部复位；扫描预算数学正确；无可移植性风险（MSVC）。

## 合并判定

两位评审的全部 P0（0）/P1（2）/P2（4）修复完毕；P3 中可行动项全部修复，
其余（A-F4/A-F5/A-F9/A-F10）逐条记录接受理由。修复后重跑受影响套件：
test_execution_plane_9 865/865、test_workflow_run_coordinator 172/172、
test_task_center 382/382、test_workflow_resume_provenance 67/67 —— 全绿。
