# REVIEW_LOG — 独立 review 与 disposition

## Round 1 — 对抗审查轴：WP-C resume 协议（read-only subagent，2026-09-15）

审查范围：resumable_tile_run.{h,cpp}、tile_run_contract.{h,cpp}、disk_tile_store 新 API、chunk_pipeline 改动、tile_checkpoint hash 重构、test_chunk_resume_11（含子进程崩溃钩子）。

| # | 级别 | 发现 | Disposition |
|---|---|---|---|
| 1 | **P0** | `tileRunIdentityKey` 用 `:` 分隔 → runKey 作为目录名在 Win32 非法（`:` 为 NTFS ADS 分隔符），`create_directories` 静默失败后首个 tile 写入即抛错——整个 resumable 功能在 Windows 上死路 | **已修**：分隔符改 `-`（tile_run_contract.cpp），round-trip 同步；runDir 创建失败改为显式抛错（此前 dirEc 被忽略） |
| 2 | **P1** | journal 头只在"文件不存在"时写：崩溃于文件创建与首 flush 之间（或 torn-header 恢复 resize 到 0）留下空文件 → 后续追加变成无头 journal → 永久 ChunkCorruptTile（≥2 行）或静默丢全部提交（1 行）且陷阱自续 | **已修**：appendCommit 在 missing OR size==0 时写头（崩溃窗口闭合） |
| 3 | P2 | torn-tail/torn-header 的 `resize_file` 失败被忽略 → 新行接在未终结片段后 → 下次加载必判损坏 | **已修**：truncEc 非零 → 抛 runtime_error（fail-closed） |
| 4 | P2 | rename 后 fsync 传的是文件路径+directory=true → POSIX 上 open(O_DIRECTORY) 失败，父目录条目从未 fsync | **已修**：fsync 父目录路径 |
| 5 | P2 | `cb.consume()` 位于 `catch(ChunkCorruptTile)` 的 try 内：用户 consume 抛该公开类型会被误诊为 tile 损坏→同 tile 被 consume 两次且真实错误被吞 | **已修**：验证读取在 try 内，consume 移到 catch 之后 |
| 6 | P3 | Windows 子进程崩溃断言 `rc != 0` 过弱（env 缺失=64、loader 失败都能混过） | **已修**：`rc == 70` 精确断言 |
| 7 | P3 | 子进程钩子 ANSI API 在非 ASCII 路径宿主上可能误触发 | 记录为 known limitation（本宿主路径为 ASCII；精确 exit code 使误触发可诊断）；PR_BODY 声明 |
| 8 | P3 | journal 重放先积攒 vector<uint64> 再折叠（1e6 提交瞬态 8MB）与 O(1) 声明不符 | **已修**：扫描中直接折叠进位图 |
| 9 | P3 | checkpoint 在本驱动中纯 advisory（journal 是唯一恢复真值），cadence 的 fsync 成本无内部收益 | **已修**（注释明确 advisory 定位 + 保留：对 workflow/工具可见的位置语义） |

审查者同时确认（未列问题）：崩溃窗口 A 全部 fail-safe（tile digest+几何校验、按身份分目录、index 顺序 consume）；journal 边界 B 除 #2 外全部 fail-closed；身份门 C 结构性阻断跨身份服务；rename 覆写 MSVC 语义 OK（MoveFileExW REPLACE）；ChunkConsumerAborted 修复无死锁且 fused_chain 唯一生产消费者不受影响；遥测采样界与文档一致。

**修复后状态：P0=0、P1=0**（修复 commit 见 EVIDENCE；重跑 gate 见 TEST_MATRIX）。

## Round 2 — 待 Phase 7 全量 diff review（含 fused_chain/governor/lease/adoption/测试可信度轴）

## Round 2 — 对抗审查轴：治理/lease/adoption/测试可信度（read-only subagent，2026-09-15）

| # | 级别 | 发现 | Disposition |
|---|---|---|---|
| 1 | **P1** | planner 上界仍低估：stage 线程在 StageFn 契约下瞬态持有输入+新输出 ≈2 tiles（fused_chain 实测 2-3），(S+2)·I 不是上界——正是 F-A-13 要关的失败模式；且旧模拟器把"push 原子清 in-hand" baking 进动作空间，检查在该维度重言式化 | **已修**：in-hand 项改 (2S+2)·I（producer I + stages 2I each + consumer I）；模拟器重写：stage 增加 build 状态（输入+输出共存，上限 2I），旧两种公式现在都会被模拟器击穿（有咬合力） |
| 2 | **P1** | test_chunk_adoption_11 无条件 `::_getpid()` → Linux/macOS 编译破坏（CI 主 lane ubuntu） | **已修**：selfPid() ifdef helper |
| 3 | P2 | adoption kit pipeline 模式只 wire flag，callback 型 context 取消不生效（与头文件承诺矛盾） | **已修**：producer/consumer body 增加 bridge.throwIfCancelled()（fused_chain 同型）+ 新增 pipeline 模式 callback 取消测试 |
| 4 | P2 | adoption resume 测试核心 oracle 重言式（tilesReused=0 也全过） | **已修**：增加 `tilesReused >= 9`（consume 在 journal append 之后，≥9 commits 有保证） |
| 5 | P3 | g_lastLeakReportJson 裸全局 string 数据竞争 | **已修**：静态 mutex 读写 |
| 6 | P3 | lease Expired 判定在生产接线中不可达（verdict 只查 idle，而 run 结束即清 holdingJob） | **已修**：runOnWorker soft-timeout 分支轮询 verdict，Expired → tree-kill + TimedOut（lease-expired 消息）——leaseTtl 成为与 hang window 独立的第二道活性判定 |
| 7 | P3 | health().quarantinedWorkers 是累计计数却被文档成当前状态 | **已修**：文档改 cumulative 并说明为何 gauge 无意义 |
| 8 | P3 | pipeline 进度回调 dead store `done` | **已修**：删除 |
| 9 | P3 | admission 经 governor 构造 ScratchRegistry（create_directories 副作用） | **已修**：直接 planTileMemory + Refuse→AdmissionRefused（纯规划） |
| 10 | P3 | write-gate 测试固定 sleep(50ms) 断言线程已启动（CI 负载下 flaky） | **已修**：deadline 循环等待 entered |
| 11 | P3 | 架构网 regex 漏 QThread::create/pthread_create/CreateThread | **已修**：regex 扩展；census 复核仍为同 36 文件（allowlist 不变）——现网络覆盖原生线程三系 |

审查者确认无误（未列）：tracker/pool 锁序无环（m_mutex→tracker 单向）；jsoncpp 对象键 std::map 排序 → FastWriter 输出确定；identity key `-` 分隔 path-safe；fused_chain 取消桥生命周期；遥测线程安全与采样界。

**Round 2 修复后状态：P0=0、P1=0。**
