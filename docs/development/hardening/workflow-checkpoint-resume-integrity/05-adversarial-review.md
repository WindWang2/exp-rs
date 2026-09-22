# 05 — Adversarial Review: hardening/workflow-checkpoint-resume-integrity

- 审查人：独立 adversarial reviewer（未参与实现）
- 工作区：`/home/kevin/projects/rs-studio/exp-rs-worktrees/hardening-workflow-cri`
- 基线：`a9dc33fa73`，审查对象 `a9dc33fa73..HEAD`（4 commit：`b478787a1a` / `b672ab8df1` / `8dfdb17d63` / `1664b6f441`）
- 方法：不止读 diff —— 通读 `pipeline_run_coordinator.cpp/.h` 全文、`workflow_checkpoint.cpp` 全文、`workflow_run_lock.cpp/.h` 全文、`ir2_registry_node_executor.cpp` 全文、`workflow_definition.cpp`、`workflow_run.cpp`、Engine-2（`workflow_run_coordinator.cpp`）锁/持久化段、生产调用方（`ir2_pipeline_designer_dock.cpp`、`main_window_view.cpp`），并扫描 `data/labs`、`data/agent/recipes`、`tests/fixtures` 的全部 id 字面量与全部 `StepDef`/`NodeFact` 构造点。

## 结论先行

六个修复（A–F）本身**未发现 P0**：锁的获取/释放路径穷举未找到泄漏或自我死锁，busy-depth 的 memory_order 正确，写侧 cap 与读侧一致，空 id / ':' 拒绝在树内零兼容性破坏。**发现 1 个 P1**：F 的回归测试存在结构性"侥幸通过路径"——按现在的写法它大概率**测不到**自己声称要杀的重入窗口，revert 修复后测试仍然绿。另有 3 个 P2（affinity 线程内联重入析构的 UAF 残留、tmp sweep 的 runId 提取缺陷、空/相对 runDirectory 下锁路径失效）。

---

## P1（必须修）

### P1-1 F 的 destroy 测试几乎总是走"幸运路径"，revert 修复不会变红

**证据链**：
- 测试在 `startRun` **之后**才用 `Qt::QueuedConnection` 排入 killer-setup lambda，再由 0ms timer 派生 `std::thread` 执行 `delete coordinator`（`tests/test_d17_workflow_pipeline_e2e.cpp:648-676`）。
- worker 完成事件是在 executor **返回之后**才投递的（`pipeline_run_coordinator.cpp:874-879`，`if (self) invokeMethod(..., Queued)` 在 lambda 内部）。
- 析构 drain 里 `pool.waitForDone()` 会等 worker lambda 完整返回，因此 worker 在 lambda 内投递的 completion 一定在 `removePostedEvents`（`pipeline_run_coordinator.cpp:526`）之前已入队并被清除（`pipeline_run_coordinator.cpp:522-526`）。

**反例构造（时序）**：主线程从 `startRun`（T0，worker 已开始写 32 MiB）到进入 `loopGuard->exec()` 只隔微秒级；killer-setup 在 T0+µs 入队，worker 完成事件在 T0+数毫秒～数十毫秒才入队（32 MiB 即使 tmpfs 也要 ms 级）。于是事件队列顺序几乎总是 `[killerSetup, (killerTimer)]`，completion 根本还没投递：
1. 0ms timer 触发 → `std::thread` 发起 delete；
2. 析构 drain 作为普通事件在 exec() 里执行，`waitForDone` 等写盘结束，`removePostedEvents` 清掉 completion；
3. `affinityBusy == 0`（**从未有 frame 入栈**），busy-wait 立即通过；
4. `m_state` 释放，`destroyed=true`，全部 REQUIRE 通过。

**杀伤力判定**：pre-fix 析构（`removePostedEvents` 后直接释放 `m_state`）在此时序下行为**逐字节相同**（没有 frame 可被打断），测试照样绿。测试注释声称的"timer first fires while the event loop is pumping INSIDE onNodeFinished's whole-file hash"（`test_d17_workflow_pipeline_e2e.cpp:649-653`）没有被代码强制——它要求 completion 先于 killerSetup 入队，而这在真实调度下是小概率事件。只有落在重入窗口时 revert 才会崩（freed `m_state` 上的 `statuses`/`def` 访问 + 对已析构 QObject 的 emit），但窗口几乎不可达。
（CI 若开 ASAN 也不救：根本没进 UAF 代码路径。）

**修法**：让删除线程由 **executor 自己**在返回结果前一刻派生（或挂到 `nodeFinished` 信号上再 0ms 延迟），保证 completion 必然先入队、delete 必然落在该 frame 的 hash pump 内；或直接断言 `affinityBusy` 曾 >0（用测试内计数器验证窗口确实被命中），把"窗口未命中"变成显式失败。

---

## P2（应当修）

### P2-1 affinity 线程上的"内联重入析构"仍是 UAF，且新注释的前提是错的

- 析构只在外线程分支等 `affinityBusy`：`if (QThread::currentThread() != thread())`（`pipeline_run_coordinator.cpp:537-543`）；注释断言"On the affinity thread itself the stack unwinds before the member destruction below"（`:534-536`）。
- 该断言只在"析构从 frame 之外发起"时成立。若删除发生在 hash pump 所服务的事件里（pump 在 `computeArtifactFingerprintFull` 每 chunk `processEvents`，`:164-171`），则析构**内联运行在 onNodeFinished frame 的栈上**：drain 返回 → 跳过 busy-wait → `m_state` 释放 → 控制权回到 pump → hash 的 `aborted()` 读已释放的 `cancelRequested`、`BusyGuard::~BusyGuard` 对已释放 atomic 做 `fetch_sub`、后续 `m_state->shuttingDown` 读取全部 UAF。
- 生产相关性：生产 D17 协调器是 dock 的 QObject 子对象（`src/app/pipeline/ir2_pipeline_designer_dock.cpp:23`），affinity 线程 = GUI 线程；GUI 事件正是被 pump 服务的对象。当前树内没有"pump 内同步 delete dock"的调用点（dock 随主窗口在事件循环退出后析构），所以是**潜伏契约洞**而非现行崩溃路径。
- 修法：affinity 分支检测 `affinityBusy > 0` 时不得继续内联释放（警告 + 转为延迟释放/断言），或至少修正注释并写明"affinity 线程上由 pump 所服务事件发起的删除不受保护"。

### P2-2 tmp sweep 的 runId 提取用首次出现的 `indexOf(".json.tmp.")`，合法 runId 可让活锁 tmp 被误删

- `workflow_checkpoint.cpp:440-446`：`markerPos = orphan.indexOf(marker)`，`runId = orphan.mid(11, markerPos - 11)`。
- `isValidRunId`（`workflow_run.cpp:93-107`）允许 `.` —— **`"a.json.tmp.9"` 是合法 runId**。它的 checkpoint 是 `checkpoint_a.json.tmp.9.json`，保存中的 tmp 是 `checkpoint_a.json.tmp.9.json.tmp.<pid>.<n>`；提取得到 `runId="a"`，probe 的是 `checkpoint_a.lock`（别人的/不存在的锁）→ NoHolder → **删除活 writer 的在途 tmp**，最终 rename 失败、checkpoint 永不出现——这正是 D 声称关闭的事故类别，经由一个合法 id 复活。
- 修复是精确的：tmp 名恒为 `checkpoint_<runId>.json.tmp.<pid>.<counter>`，pid/counter 纯数字，故 **`lastIndexOf(".tmp.")` 恒等于真后缀分隔符**（对 `runId="x.tmp.1"` 也正确）。D17 侧 `atomicWriteJson` 同名约定（`pipeline_run_coordinator.cpp:356-358`），但 D17 runId 是 UUID（无点）不受影响——问题面在 Engine-2 用户命名 run。
- 其余恶意名已核安全：runId 取自文件名（不可能含 `/`），锁路径 `checkpoint_<runId>.lock` 恒在目录内；`runId=""` → `checkpoint_.lock` probe → NoHolder → 仅删 tmp 自身，无遍历风险。

### P2-3 空/相对 runDirectory 使跨进程锁互斥静默失效

- 锁路径从**文档内记录的原始 runDirectory** 推导：resume `WorkflowRunLock::lockPathForRun(QDir(runDirectory).absolutePath(), runId)`（`pipeline_run_coordinator.cpp:1247-1249`），checkpoint 也原样持久化 `runDirectory`（`:1100`）；`startRun` 同构（`:656-659`）。
- 反例：`startRun(def, "")`（或 `"runs/x"`）→ 锁落在**当前工作目录**。两个不同 cwd 启动的进程 resume 同一 checkpoint → 各自推导出不同锁路径 → 双双获得"独占" → 双执行剩余节点、互踩 checkpoint——E 要防的事故原样发生，且报的是成功。
- 严重度收敛为 P2 的理由：runDirectory 为空/相对时 D17 的 artifact containment（`canonicalRunDir`）本来就已 cwd 化，跨 cwd resume 的行为在基线就不健全；但锁是**唯一**的跨进程防线，其静默失效值得显式拒绝——建议 `resumeOnAffinity`/`startRunOnAffinity` 对空/相对 runDirectory fail-closed，或改用 `canonicalPath` 记录。

---

## P3（可记档）

1. **probeOwner 把 open 失败当 NoHolder**（`workflow_run_lock.cpp:174-179`）：多用户共享 checkpoint 目录下，他人 umask 出的 0644 锁文件使 probe `O_RDWR` 打不开 → 判 NoHolder → 误扫活 writer 的 tmp。建议区分 `EACCES` 为 `Unknown` 并跳过删除。开销本身没问题：每 tmp 一次 open+flock，仅启动恢复期执行，万级文件也只是毫秒级。
2. **深度 1020 的可移植性论证部分成立**：Ubuntu `libjsoncpp-dev` 与 vcpkg jsoncpp 1.9.x 默认 `stackLimit=1000`，1020 必被拒；但 executor 的 `CharReaderBuilder` 未显式设 stackLimit（`ir2_registry_node_executor.cpp:40`），与仓库纪律（`docs/recipes-integration.md:58` "JSON parses cap stackLimit at 256"、`startTrackedPipelineJson` 显式 64）不一致——换一个默认 ≥1021 的 jsoncpp 构建时测试会假红（噪声）。另：测试注释里"Qt's own parse ceiling (1024) still accepts the document"（`tests/test_ir2_port_param_mapping.cpp:544-546`）是无关论证——路径中 Qt 只做序列化不做解析（参数以 `QJsonObject` 到达）。
3. **startRun 把 `TryResult::Error` 混报成 "already owned by a live process (pid ?)"**（`pipeline_run_coordinator.cpp:662-666`；对比 resume 路径 `:1252-1259` 正确区分）——run 目录不可建/不可开时诊断误导。
4. **析构 busy-wait 无超时**（`pipeline_run_coordinator.cpp:539-540`）：被卡死的 persistCheckpoint 磁盘 IO 会让外线程析构永久自旋。cancel 标志把常规上限压到一个 hash chunk，属病态 IO 场景。
5. **Windows probe 副作用**：`probeOwner` 对不存在的锁文件走 `QLockFile::tryLock(0)` 会创建又删除一个锁文件（`workflow_run_lock.cpp:191-199`），瞬态 churn；`getLockInfo` 失败时 `Unknown` 落入删除分支（D 的 tmp sweep 中 `Unknown` ≠ `LiveOwner` → 删）——建议与 1 一并按"非 LiveOwner 但存疑则跳过"收紧。
6. **锁文件永不 unlink**（设计如此，`workflow_run_lock.h:21-22`）+ D17 每次 startRun 新 UUID 新锁文件 → run 目录内锁文件随运行数线性累积，约 40B/个，可接受但值得在文档标注。
7. 测试卫生：deep-params 测试若中途 REQUIRE 失败会跳过 `registry.unregisterOperator`（`tests/test_ir2_port_param_mapping.cpp:563`），算子泄漏进同二进制后续测试；当前同类型覆盖注册，良性。

---

## 逐修复面判定（A–F）

| 面 | 判定 | 依据 / 打击记录 |
|---|---|---|
| A 参数转换 fail-closed + ':' 拒绝 | **通过** | 抛出与返回 false 两条路径都被 `catch(Json::Exception)`+`nullopt` 收口（`ir2_registry_node_executor.cpp:44-55`），空对象 `{}` 仍转换为空 objectValue 放行（既有测试以默认空 `QJsonObject` 走 executor 验证）。':' 拒绝零兼容破坏：`data/labs` 11 个 lab、`tests/fixtures` 36 个唯一 nodeId、labspec lift 生成的 `lab_step_%N`（`labspec_workflow_lift.cpp:76`）全部无 `:`；带 `:` 的都是 operatorId（`rs:test`/`test:ir2_*`），不受影响。`WorkflowIR::validateSemantics` 只查空/重名（`workflow_ir_v2.cpp:341-345`），':' nodeId 生产可达，gate 非死代码。 |
| B 写侧 16MiB cap | **通过** | 检查点在**完整序列化之后**（覆盖 definition+stepPlans+artifacts 一切字段）、写 tmp 之前（`workflow_checkpoint.cpp:84-91`），拒绝发生在任何文件触碰前；与 `loadCheckpoint` 的 file.size() cap 和 `atomicWriteJson` 的 writer cap 一致。调用方语义安全：Engine-2 `persistRun` 本就 best-effort（`workflow_run_coordinator.cpp:437-444`），recovery 重存失败则不报告、下轮重试（`workflow_checkpoint.cpp:509-517`）。 |
| C 空 stepId fail-closed | **通过** | 三道闸（非对象项 `workflow_definition.cpp:117-123`、空 id `:129-138`、程序化 `workflow_run.cpp:310-315`）。兼容扫描：`builtin_definitions` / `preset_catalog_widget` / `workflow_session_controller.cpp:317`（"step1"）/ `rs_pipeline_runner.cpp:266-271`（回退 `"step_N"`）全部赋非空 id；`data/agent/recipes/*.json` 零缺 id；测试全部 `StepDef` 带 id。唯一行为变化是"旧的可加载但本就不可健全运行的 id-less checkpoint 现在拒载"——有意 fail-closed。 |
| D tmp sweep 尊重锁 | **带 P2-2/P3-1 通过** | 活锁保护本身正确且被测试钉死；缺陷在 runId 提取与 probe 语义，见上。 |
| E D17 跨进程所有权锁 | **带 P2-3 通过** | 路径穷举：resume 提交前每条 `fail()` 都在局部 `unique_ptr` 生命周期内（提交点 `:1384` 之后无 fail 路径）；startRun 锁先于全部 m_state 变更（`:656-675`）；requestCancel→finalize（`:1043`）与析构（RunState 成员销毁）都释放；finalize 前无锁调用（null reset 无害）；同 coordinator 连续两次 startRun / resume→完成→再 resume 均正确（finalize 已释放，旧锁文件按设计不 unlink）；同进程双 coordinator 靠 per-open-file-description 的 flock 冲突拒绝（`workflow_run_recovery` 既有测试 `:310-348` 已证），`LOCK_NB` 永不阻塞，无自我死锁。Engine-2（中央目录，`workflow_run_coordinator.cpp:284-288,508,890`）与 D17（run 目录）路径相撞需用户显式把 D17 runDirectory 指到中央目录；届时 D17 checkpoint 因 `version:"1.1"`（字符串 vs 要求 int，`workflow_run.cpp:649-654`）被 Engine-2 loadCheckpoint 拒载跳过，D17 tmp 由同名锁正确保护——无有害相撞。 |
| F busy-depth 防 UAF | **外线程路径通过；affinity 内联路径见 P2-1** | 计数覆盖嵌套投递（pump 内嵌套 onNodeFinished 计数叠加，逐层递减）；入计数前的入口读安全（单 affinity 线程上，drain 服务时未运行的 completion 已被 `removePostedEvents` 清除，worker 在 `waitForDone` 返回前投递的事件必被清除）；`fetch_add(acq_rel)/fetch_sub(release)/load(acquire)` 配对正确，acquire 读到 0 即与最后一次 release sub 同步，析构方看到全部 frame 写入。msleep 等待有界（frame 内 hash 受 cancel 一 chunk 上限约束），除病态 IO（P3-4）。 |

## 并发正确性补充

- **锁序**：D17 的 flock 全部在 affinity 线程上获取/释放（start/resume/finalize 均为 marshal 后路径）；析构方释放时已确认无 frame 持有状态；与 Engine-2 的 "flock 先于 m_mutex" 契约分属两个引擎，无共享锁序。`invokeOnCoordinatorThread`（BlockingQueued）不会持锁阻塞：锁只在已上板（affinity）后触碰。
- **removePostedEvents 双保险**：drain 内一次 + busy-wait 后一次，后者的窗口内（busy 归零到释放之间）无新事件源——pool 已 clear+waitForDone，worker 投递必在 waitForDone 返回前。
- **外线程 destroy 与外线程 resume 并发**（另一线程正在 `resumeFromCheckpoint` 时第三线程 delete）属"对象使用中析构"的标准 Qt 违约，resume frame 未计入 busy-depth 但其所有 m_state 访问都在 drain 设立的 cancel/shuttingDown 检查之后立即返回；不建议为此加计数，建议在头文件 threading contract 里明写"析构不得与其他线程的公共调用并发"。

## 测试杀伤力表

| 新 TEST_CASE | revert 对应修复后必红？ | 侥幸路径 / 抖动 |
|---|---|---|
| `A checkpoint mid-resume is owned by one process at a time`（d17:398） | **是**（peer resume 返回 true → `REQUIRE_FALSE` 即红；checkpoint 字节不变断言亦红） | 无。checkpoint 前像在 node_2 park 后读取，期间无写者。同进程 flock 冲突有既有测试背书。 |
| `Two coordinators cannot resume the same checkpoint concurrently`（d17:471） | **是** | 无。resumeB 为全新 coordinator，refusal 只能来自锁。 |
| `Foreign-thread destruction during a whole-file hash never frees live state`（d17:524） | **否 —— P1-1**。主时序下 killer 先于 completion，重入窗口不可达，pre-fix 也绿。 | 幸运路径即主路径。修法见 P1-1。 |
| `Registry executor fails closed on parameters jsoncpp cannot convert`（ir2:531） | **是**（本构建 parse 抛 `Json::Exception` → 旧代码异常逃逸被 Catch2 记失败；若某构建返回 false → 空 object → operator 跑 defaults → `REQUIRE_FALSE(refused.success)` 红） | 可移植性条件：jsoncpp 默认 stackLimit < 1020（1.9.x=1000，两条 CI 工具链满足）；见 P3-2。正例对照杀死"全拒绝"回归。 |
| `Registry executor refuses a nodeId that forks an NTFS alternate data stream`（ir2:578） | **是**（pre-fix `host:stream` 在 POSIX 合法 → 执行成功 → 红） | 无。 |
| `saveCheckpoint refuses to write a checkpoint its own loader would reject`（rec:424） | **是**（pre-fix 提升成功 → `savedPath` 非空 → 红；tmp 残留断言双保险） | 无。16 MiB 字符串在内存构建，测试耗时/内存可接受。 |
| `recovery does not sweep the tmp file of a run owned by a live process`（rec:465） | **是**（旧 sweep 无条件删 → `QFile::exists(liveTmp)` 红） | 无。释放锁后第二遍恢复 1 个 run + tmp 被扫，正反两段都钉死。 |
| `an id-less definition step is refused instead of poisoning the checkpoint`（rec:514） | **是**（三个子断言各自独立杀 JSON 门、非对象项、`createFromDefinition` 程序化门；正例 round-trip 杀"全拒绝"回归） | 无。 |

CI 抖动总体评估：d17 三个新测试全部以 waitUntil/信号等待 + 充足超时驱动，无真实 flake 源；唯一可靠性问题是 P1-1 的"测不到"而非"偶发红"。

## 最终结论

**PROCEED-WITH-FIXES** —— 六项修复的产品代码全部成立、未发现 P0/P1 级产品缺陷，但必须先修 F 的 destroy 回归测试（当前写法大概率测不到自己声称的重入窗口，revert 不红），并建议顺手落实 P2-2 的 `lastIndexOf` 一行修复与 P2-3 的 runDirectory fail-closed。
