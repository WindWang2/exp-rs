# R9 Goal-Loop Ledger

格式：`第N轮 | 改动 | 验证结果 | 通过/未通过 | 下一步`

- 第0轮 | 基线审计（2 subagent：PR threads + Issue 映射）；建 worktree/分支/跟踪文件 | 远端=快照（master 8a0864276，0 open PR）；6 个 WP 候选确认 REMAINING | 通过 | 配置构建环境 → WP-A
- 第1轮 | WP-A ipc_channel setEventSink 锁内回放死锁：RED 测试先行（重入 case 在旧代码 5s watchdog 失败 exit 3；顺序 case 旧代码通过=契约保护）；修复=EventDeliveryPhase{NotInstalled,Draining,Live}+mEventOrderCv，回放移出锁外，reader Draining 等待，closeLocked 唤醒，异常路径 scope 恢复，null-sink 语义保持 | test_exprs_ipc 全套 26 case/148 断言 GREEN（exit 0） | 通过 | 二次 gate + 相邻套件（test_plugin_host_process 构建中）
- 第2轮 | 构建环境：VS cmake（Qt CMake_64 已不存在）+ vcvars64 + Ninja + CMAKE_PREFIX_PATH(Qt6.8/qca/kc/vcpkg_installed) + GDAL 显式变量 + winflexbison；辅助脚本放 build-dev/（被 ignore） | CONFIGURE_OK；test_exprs_ipc BUILD_OK | 通过 | —
- 第3轮 | WP-B/D/E/F/C 代码+测试全部落盘（WP-B 统一解析器+PATHEXT+typed cmd 拒绝；WP-D in-flight coalescing；WP-E QPointer+destroyed 钩子；WP-F 节点互斥+超时参数+adapter 去嵌套；WP-C red 测试+访问器，fix 待 red 后上） | 首编全量进行中（qgis_core 巨型目标） | 未通过（未验证） | 大构建完成后：stash WP-B/D 源修复→跑 RED→pop→GREEN

### RED 证据记录
- WP-A：已捕获（旧代码 exit 3 REQUIRE(installed) 失败）
- WP-B：已捕获（3 用例 red，exit 42；相对路径保护例在旧代码即绿）
- WP-C：已捕获（`REQUIRE( retainedRecordSnapshots() == 0 )` 扩展为 `64 == 0`）
- WP-D：已捕获（3 用例 red，exit 42）
- WP-E：UAF 为 UB，无法确定性 red；green 契约测试（seam 惰性化+重绑）
- WP-F：数据竞争，概率性 red；green 不变量测试（双占==0）

### 第4轮 RED→GREEN 结果（全部先 RED 后 GREEN）
| WP | RED 证据 | GREEN 结果 |
|----|----------|-----------|
| A | 旧代码 exit 3（重入死锁，watchdog 触发） | test_exprs_ipc 26 case/148 断言 exit 0 |
| B | 3 用例 exit 42（rooted 探针/spawn 分歧、PATHEXT .COM 不可发现、.cmd 误报 not found） | test_exprs_external_process_win 12 case/60 断言 exit 0 |
| C | `64 == 0`（64 次调用滞留 64 快照） | test_exprs_plugin_loader 37 case/552 断言 exit 0 |
| D | 3 用例 exit 42（假拒绝/双重加载/失败不广播） | test_model_runtime_stress 6 case/65 断言 exit 0 |
| E | UAF 不可确定性复现（UB） | test_execution_plane_9 [catalog] 2 case/8 断言 exit 0 |
| F | 数据竞争概率性 | 待跑 |

### 发现：预先存在的 flake（非本分支回归）
`test_execution_plane_9 [ep9][observability]` 断言 `runLocks held: 0` 偶发失败。
根因（代码审读）：`WorkflowRunCoordinator::onTaskUpdated` 在 JobEngine worker 线程先
`persistRun(finalizePersist)` 排队，再 `releaseRunOwnership(runId)`；主线程
`drainPersists()` 等的是 persist 线程的 in-flight 归零，两个线程之间无 happens-before，
故 persist 完成时 release 可能尚未执行 → explainDump 读到 1。
证据：master 纯净构建隔离跑 10 次 = 9 过 1 败；本分支 10 次 = 9 过 1 败（同频率，未加剧）。
处理：记录为已知预先存在 flake + follow-up，不在本 PR 修复（#1448/#1450 持久化时序为
敏感路径，且非本波 WP 范围）；PR 描述如实披露。

### 第5轮 WP-F + 预先存在失败基线
| 套件 | master | 本分支 | 说明 |
|---|---|---|---|
| test_runtime_python_channel_r4 | 9 用例 2 过 7 败（8 失败断言） | 11 用例 4 过 7 败（8 失败断言） | 7 败为预先存在：该套件 fixture 用 `/bin/sh`，本 Windows 主机无 sh；我的 2 个新用例（cmd.exe+.bat 跨平台）全过，**净增 Windows 覆盖** |
| test_plugin_host_process | 30 用例 4 过 26 败 | 同 | 预先存在：fixture DLL 的 LOAD_WITH_ALTERED_SEARCH_PATH 依赖解析（C 方向测试基建） |

WP-F GREEN：`*1462*` 2 用例 / 48 断言 exit 0（含 40 轮×4 线程竞态检测 + 有界等待 150ms→<900ms）。

### 第6轮 修复我自己引入的测试缺陷（重要教训）
WP-E 用例单独跑 4/4 SIGSEGV。根因：**不是源码缺陷，是测试缺少该文件既有前置**——
本文件每个 TaskCenter 用例都先 `engine.shutdownForTests(); engine.setMaxWorkers(2);
engine.clearExecutors();` 再首次使用 `TaskCenter::instance()`（TaskCenter 构造锚定
JobEngine，#1357 exit-phase 家族）。我的用例漏了这段，`center.shutdownForTests()`
作用在刚构造、worker 池未重置的单例上即崩。
修复：补齐同模式前置。修复后单独跑 5/5 通过；`*catalog*` 3/3 通过；全套 18 用例
（17 过 + 1 跳过，0 失败）连续两次通过。
教训：新用例必须能独立运行，不能依赖同文件前置用例的副作用。

### 第7轮 最终 gate（每个关键 gate 两遍）
| 套件 | 第1遍 | 第2遍 |
|---|---|---|
| test_exprs_ipc | 26 case / 148 断言 PASS | 同 |
| test_exprs_external_process_win | 12 case / 60 断言 PASS | 同 |
| test_exprs_plugin_loader | 37 case / 552 断言 PASS | 同 |
| test_model_runtime_stress | 6 case / 65 断言 PASS | 同 |
| test_runtime_python_channel_r4 *1462* | 2 case / 48 断言 PASS | 同 |
| test_execution_plane_9 *catalog* | 2 case / 8 断言 PASS | 同（3/3 稳定） |
| test_execution_plane_9 全套 | 17 过 + 1 跳过，0 失败 | 同 |


## 双轮独立 review 记录（reviewer 均为未参与补丁实现的 subagent，总派生名额 3：审计①、审计②、review）

### 第一轮（general-purpose reviewer）
- 结论：**0 P0 / 2 P1 / 16 P2**
- P1-1（WP-D `model_runtime.cpp:1008/1019`）：`cv.wait`（持 m_mutex）后仍用等待前取得的 `cached` 迭代器；corpse + 并发同 key + LRU 淘汰三条件下构成 shared_ptr use-after-free。**已修**：等待循环后重新 `m_cache.find(key)`（`corpseEntry`），erase 改按 key。
- P1-2（WP-E `task_center.cpp:449`）：`shutdown()` 直接 `m_catalog = nullptr` 而不断开 `destroyed()` 守卫，`setCatalog` 又以"m_catalog 非空"为断开条件 → shutdown()+setCatalog(B) 后陈旧 catalog 析构清掉 B 的 seam（指纹静默退化为 "no catalog wired"）。**已修**：守卫连接句柄成员化 `QMetaObject::Connection m_catalogGuard` + `disconnectCatalogGuardLocked()`，shutdown/setCatalog 均按句柄断开；**并补回归测试**覆盖该场景。
- 采纳并修复的 P2：WP-D ride 路径补 `permanentlyUnavailable()` 门；删除 ride 未使用的 `errorMessage` 形参；`InFlightLoad::published` + `FlightGuard` scope guard 防 flight 泄漏（幂等）；WP-F 去掉 `initialize()` 末尾多余锁；`acquireWorker` 补 `QCoreApplication::instance()` 判空；WP-F 头注释改为如实描述 `isRestarting`/`crashBudgetLeft` 残余竞争并标注 follow-up；WP-B 删 Windows-only 测试里的 POSIX 死分支；WP-A 注释改为如实说明 sink 快照竞态为既有行为、重入注册静默拒绝语义；WP-C 死成员改名 `mRetainedRecordSnapshots` 并写清"契约 gauge"定位；WP-D 测试 3 处无超时自旋改 `waitForLoadStart()` 有界。
- 论证后接受不改的 P2（ reviewer 独立复核成立）：WP-B `hasExtension`（`resolveWindowsProgram` 只在无分隔符裸名分支走后缀逻辑，故 `wide.find('.')` 已是纯文件名判断）；WP-B TOCTOU 双重解析（既有模式）；WP-E lambda 在 `~QObject` 内取非递归 m_mutex（当前不可达，已写入头文件契约注释）。

### 第二轮（delta review，同一 reviewer，未参与实现）
- 结论：**0 P0 / 0 P1 / 5 P2**
- 新发现并已修：`tests/test_plugin_lifecycle_halffail_r4.cpp:465` —— WP-C 改 `record()` 返回 `shared_ptr` 后，range-for 直接绑定 `record(...)->diagnostics.items()` 临时对象的成员引用，临时 shared_ptr 在 range-init 完整表达式末尾析构 → 循环遍历已释放内存（UB；reviewer 用 MSVC 探针实证该模式下迭代 0 次，即断言被静默禁用）。**已修**为命名持有；修复后该套件 9 case / 69 断言全过。
- 其余 P2：TOCTOU 注释/复用（后续 PR）；PATHEXT `.com` 优先需 release note（**已写入 CHANGELOG**）；`rideInFlightOnRefusal` 与 `FlightGuard` 缺定向用例（锁语义已逐行核，列为后续覆盖缺口）；`validateArgv`（宿主视角）与 `run()`（子环境视角）锚定前提不同（注释已如实记载）。
- 附带核实：reviewer 实编译过 5 个 WP-C 受影响套件；我进一步**实跑**全部：channel_parity 3/3、pollution_gate 3/3、unload_order 4/4、halffail 9/9 通过；`test_plugin_host_boundary_r4`（8 例 5 过 3 败）与 `test_plugins_runtime_host`（master 6 例 1 过 5 败 → 本分支 7 例 2 过 5 败，同 5 个预先存在失败 + 1 个新增通过）经 master 同环境对比确认为**预先存在**（fixture DLL 的 LOAD_WITH_ALTERED_SEARCH_PATH 依赖解析，C 方向测试基建），非本轮回归。
