# REVIEW_LOG — R5 Track 07 独立评审记录（两轮对抗评审）

评审者：两个独立只读 subagent（与实现分离），输入 = `a726d17a6...HEAD` 全量 diff +
任务书 + 测试证据（并独立复跑关键套件）。

## 第一轮（fresh diff 全量评审）

结论：**SHIP**（0 Blocker / 0 High / 1 Medium / 2 Low / 3 Nit），逐域
POOL/FENCE/PROVIDER/TESTS/CMAKE 全 PASS。评审者独立复跑：recovery 10/10 ×2、
fence 5/5、新增 provider 用例、chunk pin 修正后用例；并核对了 CMake 注册链
（sicnu_add_test → discover → per-case TIMEOUT/RUN_SERIAL 传递）。

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| M1 | Medium | 池在重启 backoff 窗口被销毁 → 已恢复请求（所有权在 timer lambda 捕获）泄漏不答 | **已修**：所有权移入成员 `m_pendingRecovery`；shutdown() 快照→清空→逐条 typed 答复；timer/replay/watchdog 借用-消费-擦除 |
| L1 | Low | fixture answer_then_exit 150ms 排水窗口是负载敏感的场景整形 | **已修**：改为确定性握手（fixture 退出只发生在再答一条 test.hang 之后，受控流保序，无固定延时） |
| L2 | Low | fence 并发 oracle 理论上可双赢家（50ms 持锁窗 vs 去调度） | **已修**：持锁 250ms、8→4 轮 |
| N1 | Nit | 大小写探测对无字母尾分量 miss（保守方向） | 注释明确声明保守 miss 方向 |
| N2 | Nit | DECISIONS.md 尾部 heredoc 残留 | 已删 |
| N3 | Nit | createWorkerNode 忽略 startWorker 返回（master 预存） | 记为预存/越界，PR 中说明 |

## 第二轮（修复 delta 复审）

结论：**BLOCK**（1 High / 2 Medium / 3 Low / 3 Nit），修复 delta 大体确认但发现新问题，
全部处置如下。

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| H1 | High | retire/shrink 路径孤儿化 `m_pendingRecovery`：替换 worker 连接前即死（pre-connect death streak）时每个新周期 in-flight 为空，旧条目无人消费；watchdog 随各周期 server 死亡（backoff 1-8s < 5s）→ 原调用方永久挂起 | **已修**：retire 分支与 setPoolSize 收缩分支对 `m_pendingRecovery[node->id]` fail+erase；新增确定性 oracle（pre-connect 死亡串烧预算 → 原持有请求必须得到 typed 答复），12/12 绿 |
| M1 | Medium | 重入假设未成文：shutdown() 迭代 map 时回调可重入（QEventLoop 先例存在），timer 无 m_shuttingDown 守卫 → 迭代器失效 UB/双答 | **已修**：shutdown 快照→清空→答复；timer 顶部 `m_shuttingDown` 守卫；replay/watchdog 擦除改为"回调后按指针身份复核再删"；头文件明文 threading/re-entrancy contract |
| M2 | Medium | drainBufferedResponses/onSocketDisconnected 中 `waitForReadyRead` 同步派发 readyRead，回调可 close() 置空 m_socket → 空解引用 | **已修**：drain 后与 teardown 前 `if (!m_socket)` 复核 |
| L1 | Low | server 父化 watchdog 仍残留同一次 dispatcher pass 的过期窗口（deleteLater 与已到期 timer 同 pass 触发） | **已修**：watchdog 回调加 QPointer 服务器身份校验（`live->server == myServer` 才答） |
| L2 | Low | drain 使 inFlightCount 归零 → 存活 worker + 死 socket + 0 in-flight 被静默搁置 | **已修**：clientDisconnected 恢复不再以 inFlightCount 为门（worker 仍运行即恢复；已完成的最终应答已先被 drain 排出计数） |
| L3 | Low | clean-exit 用例对 drain 修复的钉死是统计性（数据先到则走正常路径） | **已修**：新增 `answer_then_abort` fixture 方法 + 独立 oracle（应答与其自身 abort 同批到达的契约；分支归属按构造不可钉，测试注释声明钉的是"答了而非丢"）；clean-exit 用例注释补 drain 契约 |
| N1 | Nit | m_stderrBuffer 不在 startWorker 重置（池总是新实例，无现实影响） | 记录，不修（YAGNI，池生命周期已保证） |
| N2 | Nit | shutdown 用 `delete server`（若从 server 槽内调用不重入安全） | 记录（shutdown 仅析构/显式停止调用） |
| N3 | Nit | workerCrashed 信号名对 clean-exit/EOF 损失也发（文本已澄清） | 记录，不修（改信号名是 API 变更，越界） |

## 评审后回归中发现的第三个独立问题（实现者自测，非评审员发现）

**既有 r4 orphan 套件的双线程 fence 用例间歇 SIGABRT**：两个线程都在对方之前抢注
fence 时，输家的 `AlreadyRunning` 从未捕获的 thread lambda 逃逸 → terminate。
master 上窗口极小（ctor µs 级）；本 Track 把 fence key 改为带 FS 探测的计算后窗口
拉宽，负载下 ~50% 复现。**产品语义正确（typed 拒绝），属测试时序假设缺陷**：
修法 = 第二线程 wait 第一线程的 `firstHeld` promise（guard 构造完成后才 set），
fence 持有先于第二次构造成为字面事实。16/16 隔离复跑稳定。

## 最终验证状态（评审两轮修复之后）

- test_runtime_pool_recovery_r5：12/12 ×3（含新增 retire-oracle 与 abort-race 用例）
- test_model_publish_fence_r5：5/5 稳定
- 全聚焦面（recovery/fence/channel/stress/provider/orphan/worker-boundaries/chunk/
  failure_matrix）：40/40 连续两轮
- test_model_tasks：12802/12802；test_model_ensemble/stress/scene/parallel 全绿
- 修复后 fence 双线程用例隔离 16/16
