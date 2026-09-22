# Implementation Ledger — hardening/workflow-checkpoint-resume-integrity

Baseline: origin/master `a9dc33fa73`（2026-09-22，含 #1225 workflow durability batch / #1200 deep-review / #1224 atomic_fs）。
历史去重结论（02-history-dedup.md）：`agent/flash-workflow-integrity` 全部 6 个线索 commit 已被 master 吸收或取代，**无代码移植**。

## Slice A — executor 参数转换 fail-closed（P0，crash + fail-open 双面）

**缺陷（源码实证）**：`ir2_registry_node_executor.cpp` 的 `qJsonObjectToJsonCpp`
- 转换失败时返回**空 Json 对象**并继续执行 → operator 以空参数运行（fail-open 默认值执行，发布用户从未请求的产物）；
- jsoncpp 超过 stackLimit 时 `CharReader::parse` **抛 `Json::Exception`**（实测：Ubuntu jsoncpp 默认 stackLimit=256；深度 998 即 THREW）而该调用无 try/catch → 异常穿出 executor lambda → D17 QThreadPool worker → `std::terminate`（进程崩溃）。
- 可达窗口实测：Qt6 `QJsonDocument::fromJson` 接受嵌套深度 ≤1023，jsoncpp 默认拒绝 >256（上游默认 1000）——**256..1023 层的 workflow 参数**即可触发崩溃面（workflow JSON 是外部输入，Engine-2 侧有 `startTrackedPipelineJson` 已用 stackLimit=64 显式防同类问题，executor 是漏网调用点）。

**修复**：`qJsonObjectToJsonCpp` 返回 `std::optional<Json::Value>`、捕获 `Json::Exception`；executor 转换失败 → 类型化拒绝（`ir2.operator_failed: parameters ... cannot be converted`），operator 永不执行。附带 `isSafeNodeId` 增加 `:` 拒绝（NTFS ADS：默认产物名 `host:stream.out.tif` 在 NTFS 上分叉到备用数据流，字节永远不落在声明输出上）。

**Oracle**（tests/test_ir2_port_param_mapping.cpp）：
- `deep-params`：1020 层参数（Qt 必收 / 任何 jsoncpp 默认必拒的可移植窗口）→ 期望类型化拒绝 + 磁盘无产物。旧实现 RED：executor 直接抛异常（Catch2 捕获报错）或 abort。
- `ads-node-id`：nodeId `host:stream` → 期望 unsafe 拒绝；旧实现在 Linux 上成功写出文件（RED）。

## Slice B — Engine-2 checkpoint 写侧 cap（P1）

**缺陷**：`workflow_checkpoint.cpp::saveCheckpoint` 无大小检查；`loadCheckpoint`（读侧）拒收 >16MiB。大定义写出**自身 loader 必拒**的 checkpoint → 每次恢复 warn+skip → run 静默不可恢复。D17 写侧（`atomicWriteJson`）与 provenance 写侧已有同等 writer cap，Engine-2 是漏网点。

**修复**：序列化后、写 tmp 前检查 `jsonStr.size() > kMaxCheckpointDocumentBytes` → qWarning + 返回空（fail-closed，明确的保存失败优于静默不可恢复）。

**Oracle**（tests/test_workflow_recovery.cpp `write-cap`）：step 参数注入 16MiB+1024 字节 blob → saveCheckpoint 返回空、目标文件与 tmp 残留均不存在。旧实现 RED：文件被写出。

## Slice C — 空 stepId 定义 fail-closed（P1，毒 checkpoint）

**缺陷**：`workflowDefinitionFromJson` 容忍缺/空 `id` 的 step（仅非空才查重）；且**静默跳过非对象 step 条目**（silent truncation → 实际执行的 workflow 比编排的小）。`WorkflowRun::createFromDefinition` 原样复制空 id → 两个空 id step 产生两条 `stepId:""` 的 plan → saveCheckpoint 成功 → loadCheckpoint 以 "duplicate stepPlans id ''" 拒收 → run 永不可恢复。IR2 侧对应闸（"node: missing 'nodeId'"）早已存在，Engine-2 定义侧是漏网点。

**修复**：解析期空 id → `"step: missing 'id'"`；非对象条目 → `"steps: non-object step entry"`；`createFromDefinition` 对空 id 返回 nullptr（绕过 JSON 门的编程式定义二道防线）。

**Oracle**（tests/test_workflow_recovery.cpp `step-id`）：双空 id 定义 JSON 被拒 + 非对象 step 被拒 + 编程式定义 nullptr + 合法定义仍完整 save/load round-trip（正向对照）。

## Slice D — Engine-2 tmp sweep 尊重 live-owner lock（P2）

**缺陷**：`recoverInterruptedRuns` 的 tmp 清扫无条件删除全部 `checkpoint_*.json.tmp.*`，且发生在任何 per-run lock 检查之前、不看 tmp 名内嵌 pid。另一进程的 in-flight save 的 tmp 被删 → 其最终 rename 失败 → 该 checkpoint 永不出现。违反 #727 “liveness 由锁原语判定”纪律。

**修复**：tmp 清扫逐文件提取 runId → `WorkflowRunLock::probeOwner`；LiveOwner → 跳过。`.orphaned` 清扫保持无条件（选举败者本就是退役文件）。

**Oracle**（tests/test_workflow_recovery.cpp `tmp-lock`）：构造 checkpoint + tmp + 持锁（模拟活进程）→ 恢复 pass 不动 tmp、不恢复 run；释放锁后二次 pass 清扫 tmp 并 reconcile。旧实现 RED：持锁时 tmp 已被删。

## Slice E — D17 跨进程 resume 所有权锁（P1）

**缺陷**：`PipelineRunCoordinator` 全文无 WorkflowRunLock。两个进程（或同进程两个 coordinator）同时 resume 同一 checkpoint 均成功 → 双执行剩余节点 + checkpoint 互相覆盖；甚至 fresh startRun 中途被另一进程 resume 也成功。Engine-2 对 start（#727）与 resume 均持 flock，D17 是整个 workflow 模块唯一无跨进程所有权的执行面。

**修复**（复用既有 authority `WorkflowRunLock`，无新机制）：
- `resumeOnAffinity`：runId/runDirectory 验证后、昂贵的 artifact 验证循环前 `tryAcquire`；HeldByLiveOwner → 类型化拒绝（带 holder pid）；任何后续 fail 由局部 unique_ptr 自动释放；commit 时移交 `m_state->runLock`。
- `startRunOnAffinity`：fresh UUID 持锁（与 Engine-2 `startTrackedPipeline` 的 fresh-run 持锁 parity），在任何 m_state 变更前获取，拒绝路径保持 coordinator 可复用。
- `finalizeIfDone`：最终 checkpoint persist 之后再释放（peer 永不见半发布终态）；终态 checkpoint 之后可被任意 peer 全 CacheHit 复验。
- `~RunState` 析构兜底释放。

**Oracle**（tests/test_d17_workflow_pipeline_e2e.cpp）：
- `ownership`：owner 中途（node_2 被 gate 阻塞）→ peer resume 同一 checkpoint → 期望 false + "live process"，且 owner checkpoint 字节不变；释放后 owner 完成 → 第三者 resume 全 CacheHit 成功。旧实现 RED：peer resume 成功（双执行）。
- `double-resume`：resume A 持锁执行中 → resume B 同一 checkpoint 被拒。
- 同进程两 coordinator 的 flock 冲突即跨进程语义（flock 按打开文件描述符冲突），测试无需真实双进程。

## Slice F — D17 外线程析构 UAF（P2，header 契约修复）

**缺陷（源码推演）**：`~PipelineRunCoordinator` 的 drain functor 经 BlockingQueuedConnection 投递；affinity 线程在 `onNodeFinished` 的 whole-file hash 内 processEvents 泵（GUI 线程场景必开）时，drain 被重入执行并返回 → 外线程继续析构 `m_state` → affinity 线程泵返回后继续访问已释放状态（snapshot 引用 / persistCheckpoint / finalizeIfDone）。header 明确承诺 "a foreign-thread destruction is safe"——契约被自身实现破坏。

**修复**：`RunState::affinityBusy` 深度计数（onNodeFinished RAII guard）；析构函数在外线程路径 drain 后自旋等待 busy==0（cancel flag 已保证 hash 一个 chunk 内中止，等待有界）；hash 后补 `shuttingDown` 再检查，销毁中放弃 completion（不再从将死对象 persist/emit）。affinity 线程自身析构保持原内联快速路径（文档化的 fast path，不自等死锁）。

**Oracle**（tests/test_d17_workflow_pipeline_e2e.cpp `uaf`）：32MiB 产物（Auto → full hash，30+ 个泵窗口），0ms timer 在泵内触发外线程 delete → 断言存活且析构完成。杀伤力证据：以 revert-fix 循环运行旧实现观察崩溃（见验证记录）。

## 记档不实现（本 slice 关闭的候选）

1. **Engine-2 resume swap 后 racing fold persist 复活 ghost checkpoint**（`workflow_run_coordinator.cpp` swap 的 `QFile::remove(ghostRunId)` 只在 m_mutex 下，不带 IO 锁、不提升 `m_latestPersistSeq` watermark；swap 前捕获、swap 后落盘的 fold persist 会重写已删 ghost）。**后果上限**：persistRun 保存的是共享 run 对象的**当前**状态（swap 后已被 forceSet 为 Canceled）→ 复活的是惰性 terminal-Canceled 幽灵文件（无锁、非恢复候选、不触发双执行）——诊断噪声/`--list-runs` 幽灵条目，非数据损坏。**为何不实现**：公开面无法确定性驱动该窗口（onTaskUpdated 是 private slot，唯一一次性 IO delay 会被 resume 自身的首个 persist 消费），track 规则要求每个实现改动绑定回归 oracle。**修复草案**（留给有 seam 的后续）：swap 在 IO 锁内 `m_latestPersistSeq[ghostRunId] = m_nextPersistSeq` 后再 remove——锁序 io→m_mutex 与 persistRun 一致，避免死锁；任何 in-flight seq < watermark → superseded → 跳过。
2. **IR2 非声明结果的 run 内陈旧文件执行窗口校验**（`ir2_registry_node_executor.cpp:218-264` 残余）：operator 忽略 output 契约且 expectedOutput 恰为 input 引用路径时可复活陈旧文件——构造型场景，危害极低，#1032 的 authorship 已覆盖主路径。记档。
3. **路径 confinement 三处实现语义分叉**（executor `isInsideRunRoot` cleanPath 前缀 vs D17 `isContainedInDirectory` canonical+平台大小写策略 vs Engine-2 resume 无路径 containment 而以 digest 锚定）：合并是正确方向但涉及语义统一（大小写策略、symlink 时机），超出本次 correctness-first slice 的最小 delta 原则；已记录为后续收敛候选。
4. **大 workflow O(V²E) 热点**（plan_optimizer 全对边扫描、每节点全量 checkpoint 重写）：取消响应性与内存上界设计达标；无实测性能退化证据，按 track 规则不硬造 micro-optimization。记档。

## 验证记录

（每轮迭代后追加）

## 验证记录（最终）

- 全部修改 TU（3 个源文件 + 3 个测试文件）`g++ -fsyntax-only -std=c++20` 零错误（Qt6/jsoncpp/catch2 真实头文件路径）。
- `sicnu_workflow`（含全部修改的 SHARED 库）在 stop-build 指令前已完整编译通过（build log `Built target sicnu_workflow`，零 error）。
- 测试可执行文件未及链接/运行：应 owner 指令（"不要构建了，完成后review后就提交PR"）停止构建。7+2 个回归 oracle 已写入既有测试 TU，**本地未执行**；测试的正确性依据 = 逐条反例推演 + 独立 reviewer 对"revert 必红"的判定。online CI not awaited。
- 杀伤力判定改由独立 reviewer 复核：8 个新 TEST_CASE 中 7 个 revert 必红无抖动源；destroy 测试初版存在侥幸路径（P1-1），已按 reviewer 修法重写（executor 返回前派生删除线程 + `nodeFinished` 未发射作为窗口命中代理断言），重写后 revert 必红（窗口内 UAF 必现）。

## Review 修复记录（05-adversarial-review.md 全部 P0/P1/P2 关闭）

| 级别 | 发现 | 处置 |
|---|---|---|
| P1-1 | destroy 测试结构性侥幸路径（killer 先于 completion 入队，重入窗口不可达，revert 仍绿） | 重写：删除线程由 executor 返回前派生（completion 必先入队），150ms grace 落进 64MiB hash（300ms+）泵窗口；新增 `completionSeen` 断言（窗口命中 ⇒ 完成被放弃 ⇒ nodeFinished 不发射），窗口未命中变为响亮失败 |
| P2-1 | affinity 线程内联重入析构仍 UAF（潜伏契约洞，树内无调用点） | 析构 affinity 分支 busy>0 时 qWarning 显式标出不受保护的删除点；记入已知限制 |
| P2-2 | tmp sweep runId 提取用首次 indexOf，合法 runId 含 ".json.tmp." 时探错锁、活 tmp 被误删 | 改 `lastIndexOf`（tmp 后缀 pid/counter 纯数字，恒为最后分隔）+ tmp-lock 测试补嵌套 marker 用例 |
| P2-3 | 空/相对 runDirectory 使锁路径 cwd 派生，跨 cwd 互斥静默失效 | startRun 落盘绝对路径（文档携带绝对 runDirectory）；resume 对空/相对 runDirectory fail-closed |
| P3-2 | executor 未显式 stackLimit（可移植性依赖 jsoncpp 默认值） | 显式 `stackLimit=1024`（对齐 Qt 解析上限）：authored 文档在任意 jsoncpp 构建下转换行为一致；deep 测试改为 1500 层程序化构造 |
| P3-3 | startRun 把锁 Error 混报为 "owned by live process" | 区分 HeldByLiveOwner / Error 两条消息 |
| P3-4 | 析构 busy-wait 无超时、无诊断 | 5s 后 qWarning（不设硬超时：超时后释放即 UAF，宁可自旋） |
| P3-1/5 | probeOwner 把 open 失败当 NoHolder / Windows probe 副作用 | 记档不改（属 WorkflowRunLock 既有 authority 语义，改动波及 Engine-2 --list-runs 等面，超出本 slice 最小 delta） |
| P3-6/7 | 锁文件随 startRun 线性累积（~40B，设计如此）；测试 unregister 卫生 | 记档 |
