# R9 FINAL REPORT — Agent Harness / 执行框架 / 运行时 / 插件 SDK 稳定性收敛（方向 B）

**PR**: https://github.com/WindWang2/exp-rs/pull/1477 (OPEN, base `master`, **未自动合并、未等待线上 CI**)
**分支**: `hardening/r9-agent-execution-reliability` @ `e830e8a0d`（7 commits, 33 files, +1823 / −303）
**Worktree**: `C:\Users\wangj.KEVIN\projects\exp-rs-r9-agent-runtime`
**BASE_SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`（= 创建时 origin/master；PR 前重新 fetch，master 无新提交，merge 报告 Already up to date）

## 1. 已闭环的 Work Packages（6 个，全部 RED → GREEN）

| WP | 缺陷 → 根因 → 修复 | 回归 |
|---|---|---|
| A | `IpcChannel::setEventSink` 在 mMutex 内回放队列事件，sink 再入 `close()/protocolFailure()/request()` 即自死锁（PR#1469 thread WRx）。回放移出锁外 + `EventDeliveryPhase` 顺序门，保住 #1449 hello-first。 | 26 case / 148 断言 PASS |
| B | Windows argv[0] probe 与 spawn 双重解析不一致（`/tools/worker.exe` 探针查 `<wd>/tools/…` 而 CreateProcessW 锚定盘根）；PATHEXT 被忽略。统一 resolver + PATHEXT(.com/.exe) + `.cmd/.bat` typed 拒绝（#1460 / thread WR2）。 | 12 case / 60 断言 PASS |
| C | `PluginRegistry::record()` 每调用滞留一份 registry 快照、永不回收 → 轮询调用方内存无界（thread WRu）。改为调用方所有的 `shared_ptr`，registry 零滞留，#943 契约保留。 | 37 case / 552 断言 PASS |
| D | 同 key GPU model 并发 load：两 ticket 各取 VRAM admission → 第二个被假拒绝 "device unavailable"，且双重解析权重（thread WRq）。in-flight coalescing：一票一载、失败 typed 广播、取消不静默重试。 | 6 case / 68 断言 PASS |
| E | `TaskCenter::m_catalog` 裸指针，宿主不经 shutdown() 销毁 DataManager 即悬垂（#1454-5）。QPointer + `destroyed()` 守卫（连接句柄成员化，shutdown 也按句柄断开）。 | 全套 18 case：17 过 + 1 跳过，0 失败 |
| F | `acquireWorker` check-then-mark 无同步 → 重入/跨线程双占同一节点；等待为固定 ~1s 忙等；adapter 嵌套轮询放大重入（#1462-7）。节点互斥 + 调用方预算有界等待 + 去嵌套。 | 2 case / 48 断言 PASS（含 40 轮×4 线程竞态检测） |

## 2. Issue / review thread 结论

- **本 PR 修复**：#1454-5、#1460-2、#1462-7；PR#1469 未解决 threads WRq/WRu/WRx/WR2。
- **ALREADY_FIXED_IN_MASTER（有 file:line 证据，未重做）**：#1447 全部 B 侧站点、#1448 B 侧、#1449 B 侧、#1454 P1+committer+logBuffer+worker 亲和、#1455 全部、#1459 全部、#1460 P1+batch runner、#1462 P1+rebind+unknown-method+重复注册+sidecar、#1463 全部；PR#1469 thread WRo（`3305166e7`）。
- **越界转交**：thread WRz（`src/core/radiometric_state.cpp`）归 A/公共，未触碰，PR 描述已标注。

## 3. 编译 / 测试成绩（本地，MSVC 2022 14.38 + Ninja + Qt 6.8 + vcpkg，Debug，`-j2`，`QT_QPA_PLATFORM=offscreen`）

10 个受影响套件在最终代码状态下**各跑两遍**，结果一致：ipc 26/148、external_process_win 12/60、plugin_loader 37/552、model_runtime_stress 6/68、execution_plane_9 全套 17 过+1 跳过、r4 `*1462*` 2/48、halffail 9/69、channel_parity 3/10、pollution_gate 3/19、unload_order 4/34 —— **全部通过**。每个 WP 均先捕获 RED 证据（A: watchdog 检出死锁；B: 3 断言失败；C: `64 == 0`；D: 3 失败）再转 GREEN。

## 4. 两轮独立 review（reviewer 均为未实现任何补丁的 subagent；总派生名额 3）

- **第一轮**：0 P0 / **2 P1** / 16 P2。两条 P1 均真实且已修：① WP-D corpse 路径在 `cv.wait` 后用等待前取得的迭代器 → shared_ptr UAF（改为 wait 后重新 find）；② WP-E `shutdown()` 绕过守卫 → 陈旧 catalog 清掉新 seam（连接句柄成员化 + 补回归用例）。可行动 P2 全部处置。
- **第二轮（delta）**：**0 P0 / 0 P1** / 5 P2。其中 1 条是 WP-C 签名变更引入的测试缺陷（range-for 绑临时 `shared_ptr` 成员 → 遍历已释放内存、断言被静默禁用），已修并复跑通过；其余为文档化 follow-up 或已入 CHANGELOG。

## 5. 未验证条件 / 遗留风险

- **平台**：仅 Windows/MSVC 实测；Linux/macOS 标 **NOT_VERIFIED**（除 WP-B 完全在 `#ifdef _WIN32` 内，其余改动平台中立）。
- **预先存在失败（master 同频率复现，非本 PR 回归）**：`test_plugin_host_process` 26/30、`test_plugin_host_boundary_r4` 3/8、`test_plugins_runtime_host` 5/6（master）——fixture DLL 的 `LOAD_WITH_ALTERED_SEARCH_PATH` 依赖解析（C 方向测试基建）；`test_runtime_python_channel_r4` 的 `/bin/sh` fixture 7/9（master）——本 PR 新增 2 例为跨平台实现且通过，**净增 Windows 覆盖**。
- **预先存在 flake**：`test_execution_plane_9 [ep9][observability]` 的 `runLocks held: 0`（`WorkflowRunCoordinator` release/persist 无 happens-before）。隔离 10 次：master 9 过 1 败，本分支 9 过 1 败，**同频率**；根因已记录为 follow-up，不在本波修复（#1448/#1450 持久化时序敏感）。
- **残留（已在代码注释如实记录，未修）**：`PythonWorkerProcessPool` 的 `isRestarting`/`crashBudgetLeft`/`restartCount` 写在节点锁外（信号处理器须保持锁内无回调）；头文件给出两种后续方案并标注超范围。
- **覆盖缺口（列为后续）**：`rideInFlightOnRefusal` 与 `FlightGuard` 异常路径无定向用例；WP-B probe/spawn TOCTOU 需注释或复用。
- **GPU**：未用实卡，WP-D 经 hardware-pin 测试 seam + fake provider 验证，**不作实卡声明**。
- **CI**：未创建/修改/等待任何 GitHub Actions。

## 6. 行为变化（已在 CHANGELOG 披露）

PATHEXT 默认 `.COM;.EXE` 下同目录 `foo.com` 与 `foo.exe` 并存时解析到 `foo.com`（与 CreateProcessW 自身一致）；`.cmd/.bat` shim 从 "not found in PATH" 改为 naming the shim 的 typed 拒绝。

## 7. 下一轮仍需维护

1. `test_execution_plane_9 [observability]` 的 persist/release happens-before（workflow 方向）。
2. `PythonWorkerProcessPool` 节点状态字段的完全加锁（引用计数节点或逐节点锁 + detach 后复验）。
3. WP-B probe/spawn 解析结果复用（消除 TOCTOU 与 ~100 行重复）。
4. `rideInFlightOnRefusal` / `FlightGuard` 定向用例。
5. PR#1469 thread WRz（`src/core/radiometric_state.cpp`，归 A/公共方向）。
6. 本机测试基建的 fixture DLL 依赖解析（`test_plugin_host_*` 26/30 失败）——C 方向测试基建。
