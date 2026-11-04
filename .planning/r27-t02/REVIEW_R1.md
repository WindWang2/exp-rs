# REVIEW_R1 — R27-T2 第一轮独立评审（并发 / 资源生命周期 / 错误路径）

评审者：独立 general-purpose subagent（未参与实现；指示其以"merge 取决于你"的敌意标准审查）。
范围：`10652fd003..HEAD` 全部 commit（除 cc7aaf69e0 重建单独验证）+ 工作区。

## FINDINGS（11 项）与处置

| # | 严重级 | 摘要 | 处置 |
|---|---|---|---|
| 1 | P0 | HEAD 三处编译断（governance Result→bool、mirror 声明序、p3 namespace） | ✅ 已修（工具链复核） |
| 2 | P1 | resume 不对 catalog 重验 journaled-Registered → 已删资产静默"完成" | ✅ resume() 重验 findByPath，失配回 Pending |
| 3 | P2 | resume 对无 verdict 的 Pending 直接 registerSource，绕过 NeedsUserChoice 门 | ✅ admitPendingItems() 同 check() 门 |
| 4 | P2 | journal 发布失败被吞 | ✅ journal_write_error/journal_durable 上报（后 R2-F7 补愈合清除） |
| 5 | P2 | 被弃 worker 进程退出期触碰析构后的函数局部 static（Trace/Telemetry） | ✅ m_abandonedWorkers 让 trace end-guard 静默；ExecutionTelemetry 改 immortal 堆单例 |
| 6 | P3 | shutdownForTests 不升 generation，注释承诺为假 | ✅ 升 generation + 重置 abandonment 标志 |
| 7 | P3 | pool 关机后租约断言空转（重复断言旧快照） | ✅ pool.health().trackedLeases == 0 |
| 8 | P3 | acquire 对对象存储 VSI 拼写硬拒 | ✅ header 明示 http(s)-only 契约（对象存储归 fabric 层） |
| 9 | P3 | cancel() 跨线程写 m_state 与 atomic 暗示矛盾 | ✅ owner-thread 亲和文档化 |
| 10 | P3 | registerSource 抛异常 → 会话滞留 Importing | ✅ per-item try/catch → Error + journal |
| 11 | P3 | watchdog 用 system_clock | ✅ 保持（wall-clock 契约）并注释说明理由 |

## 评审确认干净（ hunted-clean 摘要）

addJoin 强异常保证与跨调用守卫；ExclusivePathLock 移动语义/双 inode/同进程再入；execute 全退出路径 RAII 持锁；JobEngine 全成员锁审计（无丢失唤醒、无重入死锁、m_workers 有界、m_jobDeadlines 全终态清理、无其它构造路径）；worker pool 四释放点齐；safe_remote_open 门序（离线先于 DNS 触碰、私网先于缓存）。

## 修复验证

修复提交 a5d60ee6c5；35/35 + 回归（job_engine 34、worker_host 13、批 23）全绿 ×2。
