# TASKS — r23-C 工作包

状态机：PENDING → RED → FIX → GREEN1 → GREEN2 → REVIEWED

## WP-1 (C05)：TaskCenter 累积/增量日志索引空间统一（R22 REVIEW_R2 R2-F3 + R2-F6 收口）
- 状态：PENDING
- 生产路径：JobEngine 进度回调/引擎内部 append（cumulative 记录，logLinesOffset==0、droppedLogLines>0 after #1705 eviction）→ TaskCenter::onJobRecord 累积分支（task_center.cpp ~:2039-2044）。
- 缺陷：累积分支以 retained 窗口 `logLines.size()` 对比/写回绝对索引 `m_forwardedLogCounts`：
  1. eviction 后 `size() < forwarded` 时守卫失真 → 引擎内部 append 的终态行（"Cancelled due to shutdown"类）永不转发（R2-F6）；
  2. 乱序（多线程 operator notify 重排）下错切片+丢行+重行（R2-F3）。
- 修复方案（消费端绝对空间翻译，R2-F3 建议）：比较/存储 `droppedLogLines + logLines.size()`，切片自 `max(forwarded, droppedLogLines) - droppedLogLines`。
- 验收（Oracle）：新测试 `tests/test_r23_c_task_center_log_spaces.cpp`
  - Case A（R2-F6 形状）：forwarded==absTotal 时引擎内部追加 1 行后的 cumulative 记录必须恰好转发该行一次（旧代码 0 次 → RED）。
  - Case B（R2-F3 形状）：mid-forwarded 累积记录必须转发绝对区间 [forwarded, absTotal) 的精确行集（旧代码转发错误切片 → RED）；随后迟到的重复 delta 必须 0 转发（旧代码重行 → RED）。
  - 既有套件回归：test_task_center 全量、test_r22_agent_job_logs 全量两轮绿。
- 测试 seam：TaskCenter 新增 `deliverJobRecordForTests(record)`（Oneline 传给 onJobRecord；理由：engine 单 listener 槽的同步投递无法从测试侧排序，契约测试需受控乱序注入；仓库已有 *ForTests 惯例）。

## WP-2 (C10, #1695-17e)：批量评估输出 JSON/CSV 发布原子性
- 状态：PENDING
- 生产路径：src/teaching_admin/batch_assessment.cpp publishBatchOutputsAtomic（:868-955）：JSON `writeBytesAtomically` 成功后构建 CSV 再写 —— CSV 失败时新 JSON + 旧 CSV 并存（部分发布）。
- 修复方案：全部载荷先构建并各自 staged 写成功后再发布（保持 writeBytesAtomically 语义），或两文件同目录 staging + 顺序发布前校验二者皆可写。
- 验收：RED = 模拟第二文件写失败（只读目录/坏路径）断言第一文件未被发布；GREEN 两轮。

## WP-3 (C09/C10, #1695-17b)：teaching_admin_dock "entr%2" 复数 hack
- 状态：PENDING
- 修复：`tr("...%1 entr%2", nullptr, n)` 复数形式（.ts 源串变更；zh 无复数形态，en 收敛）。
- 验收：源码 grep 无 `entr%2`；既有 i18n/一致性测试绿。

## WP-4（候选，视预算）：#1646-2 local_worker_pool m_running atomic 化
- 状态：PENDING（评审后决定）
- 注：形式 UB（无锁 bool 读 vs 持锁写）；注释已承认。修复=std::atomic<bool> + 注释更新。

## 已评估不做（证据见 ISSUE_TRIAGE/RISK_REGISTER）
- #1628-3 temporal 接线（声明过的后续集成，非缺陷）、#1655 渲染门启用（需健康主机验证 legend 挂死，风险>收益）、#1695-17c（M 级重构，转 RISK）、#1695-17d（非白名单）、#1632-3 exp_gui 类删除（清理决策非缺陷）。
