# REVIEW_ROUND3（第三轮复审）

> 方式：同一 agent 逐个复读本轮新增提交的 diff（c68d5e95、790d302c、adb6e525、d9ea2b01、bfe83d41、c4ed7d85、dcfa7d6a），
> 对照 oracle 与 RED 日志。**这是等价替代，不是真正独立的评审**（/teamwork-preview、/goal-loop：未安装/等价执行）。

## 发现

| ID | 级别 | 提交 | 发现 | 处理 |
|---|---|---|---|---|
| R3-01 | P3 | 790d302c | B06 重新解析成功时 capabilities 取"旧∪新"：若新内容失去某能力（如无金字塔），旧能力仍保留。取并集是为了不丢掉注册时由持久化/编辑层授予的能力。 | 接受，记录；需要区分"内容派生能力"与"授予能力"才能精确，超出本轨 |
| R3-02 | P3 | 790d302c | `notifyExternalContentChange` 限主线程，重新解析在主线程同步打开数据源；`/vsicurl/` 远端栅格会在 UI 线程发生网络 I/O。与 `registerSource` 现有行为一致（同样同步解析）。 | 接受，记录 |
| R3-03 | P3 | adb6e525 | `resourceIdentity` 对 `QUrl::StrictMode` 解析失败的 URL 原样返回（此时签名仍会落盘）；SAS 伴随字段只在已签名时剥离。 | 接受：非法 URL 本就无法被远端校验器使用 |
| R3-04 | P3 | c4ed7d85 | 开后复查只缩窄窗口：一次 publish 若在单次 GDALOpenEx 内开始并提交，仍可能读到混代。 | 已在提交与 FINAL_REPORT 标注；彻底关闭需要读写锁 |
| R3-05 | — | c68d5e95 | lifecycle 锁为递归读写锁：同线程在成员调用内部再调用 close()/open() 会死锁（读→写升级）。头文件已注明"不支持"。仓内无此调用路径（rg 检查 close( 调用点均在成员外）。 | 无需改动 |
| R3-06 | — | dcfa7d6a | PERF-2 的 keyset 切换经实测更慢，已撤回；只保留测量用例。确认工作树中三个遍历文件与 master 一致。 | 无需改动 |
| R3-07 | P2 | （未改） | `run_recorder::reconcileStaleRuns` / `ReplayReadiness::equivalentRuns` 在某页读失败时 `break`，返回部分结果且无错误信号（B07 同类）。返回类型无错误通道，修复需改 API。 | LIVE_DEFERRED，记录 |

## B03 / B08 / B09 / B10 审计记录（读码，范围有限）

- **B03 URI/路径一致性**：resource_uri 的 #1481/#1505 线程全部用 oracle 实跑核对（见 POST_1505_PATCHES.md），#1505 T9 已修；B15 的远端身份规范化也属于此类。未对 DataManager/DatasetStore 的路径持久化做系统审计。
- **B08 事务原子性**：统计 owned store 的 BEGIN/COMMIT/ROLLBACK 使用；各写路径失败分支均回滚（ExperimentStore 写入在 commit 失败后显式 rollback，#774 约定）。未发现新缺陷；`upsertRunsBatch` 首个失败即整体回滚有既有测试覆盖。
- **B09 线程安全**：#1449 生命周期锁（c68d5e95）+ race-close oracle；TSan 结果见 TEST_RESULTS.md。
- **B10 来源追溯**：LineageGraph 在 dataset 侧边读取失败或被截断时给出 `dataset_source_incomplete`（c68d5e95）；R3-07 是剩余缺口。

## 结论
本轮无新的 P0/P1。P2：R3-07（新）、PERF-1、PERF-2、DNS rebinding（#1505 T3，P1 级别但属于 offline_gate/transport 跨轨大改，见下）。
注意：#1505 T3（DNS 钉住）与 #1505 T6（重定向拒绝后回落）原评审标为 **P1**，本轨未落地 → 仍为未关闭的 P1。
