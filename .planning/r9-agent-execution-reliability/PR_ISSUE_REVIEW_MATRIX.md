# R9 PR / Issue / Review Thread 对照矩阵

## PR #1469 未解决 review threads × 代码现状（基线 8a0864276）

| Thread | path:line | 问题 | 现状 | 本分支动作 |
|---|---|---|---|---|
| PRRT_...WRo | src/agent/mcp_server.cpp:2663 | recording enable 失败但 pipeline 已启动 → 双重副作用 | **ALREADY_FIXED**（3305166e7：prepare→launch→commit+rollback，:2637-2775） | 不重做；PR 里注明 |
| PRRT_...WRq | src/operators/runtime/model_runtime.cpp:984 | 同 key 并发 load 两 ticket + VRAM admission 假拒绝 | REMAINING | WP-D |
| PRRT_...WRu | src/sdk/exprs/plugin_registry.cpp:273 | record() 快照永不回收（内存无界） | REMAINING | WP-C |
| PRRT_...WRx | src/sdk/exprs/ipc_channel.cpp:428 | setEventSink 回放在 mMutex 下回调 → 再入死锁 | REMAINING | WP-A |
| PRRT_...WR2 | src/sdk/exprs/external_process.cpp:525 | Windows 前斜杠 rooted argv[0] probe/spawn 不一致 | REMAINING | WP-B |
| PRRT_...WRz | src/core/radiometric_state.cpp:55 | 冒号误判 provider URI | 非 B（src/core 归 A/公共，须协商） | 转交记录，不越界改 |

## Issue 子项 × 现状（审计基线同上；仅列 B 相关 Issue）

### #1447 untyped jsoncpp（B 侧全部已修）
| 子项 | 状态 | 证据 |
|---|---|---|
| operations_coordinator.cpp:85 | ALREADY_FIXED | repair_approval.cpp:74-86 全量类型校验 |
| plugin_discovery.cpp:170 | ALREADY_FIXED | plugin_discovery.cpp:168-175 isInt64 守卫 |
| sicnu_worker_main.cpp:377 | ALREADY_FIXED | sicnu_worker_main.cpp:382-393 isString 门禁 |
| task_center_executor.cpp:85 | ALREADY_FIXED | 死代码删除（:85-89） |
| 其余 5 站点 | OOS_B（help/faultlab/science_context/preflight，已由 A 侧修） | — |

### #1448 stores fail-open（B 侧全部已修）
| 子项 | 状态 | 证据 |
|---|---|---|
| workflow_run_coordinator terminal persist 失败仍放锁 | ALREADY_FIXED | :244-252,521-540 拒绝 resume 直到持久 checkpoint |
| mcp_server 先建行后校验 | ALREADY_FIXED | prepare/commit 两段握手 :2650-2690 |
| experiment/batch/teaching 侧 | OOS_B（A/C 已修） | — |

### #1449 destruction windows（B 侧全部已修）
| 子项 | 状态 | 证据 |
|---|---|---|
| plugin_registry record() 悬垂 | ALREADY_FIXED | :259-278 返回快照副本（#943） |
| data_provider_registry find() | ALREADY_FIXED | 返回 shared_ptr（:64-78） |
| scratch_registry check-then-lock | ALREADY_FIXED | :430-447 锁内复查 |
| ipc_channel peerProtocol 无锁读 | ALREADY_FIXED | atomic<int>（ipc_channel.h:136） |
| dataset/experiment/workspace/science_context 侧 | OOS_B | — |

### #1454 task-center cluster
| 子项 | 状态 | 证据 |
|---|---|---|
| [P1] cancel-vs-submit 跳过收尾 | ALREADY_FIXED | task_center.cpp:3029-3060 |
| committer temp-retention | ALREADY_FIXED | output_committer.cpp:285-309 |
| logBuffer 无界深拷贝 | ALREADY_FIXED | :91-108 环形 2000 行上界 |
| local_worker_pool 跨线程 QProcess | ALREADY_FIXED | :138-165 owner 线程判定 |
| [P2] m_catalog 裸指针锁外解引用 | **REMAINING** | task_center.cpp:1614-1619, 4086-4098 → WP-E |

### #1455 operators runtime/io（全部已修）
| 子项 | 状态 | 证据 |
|---|---|---|
| corpse-session reclaim 误删 ledger | ALREADY_FIXED | model_runtime.cpp:905-918 per-ticket ledgerHolder |
| fabric io CancelToken | ALREADY_FIXED | io_fabric_operators.cpp:103-119 等 |
| OTB watchdog 1h | ALREADY_FIXED | otb_operator_base.cpp:151-156 env 覆盖 |

### #1459 workflow/runtime（全部已修）
| 子项 | 状态 | 证据 |
|---|---|---|
| optional deref | ALREADY_FIXED | workflow_runtime.cpp:608-613 |
| 消息子串 fallback | ALREADY_FIXED | typed PlaneSubmitRejected :46-56,277-284 |
| 锁内 QFile::remove | ALREADY_FIXED | :1492-1496 锁外 unlink |
| 全量内存 digest | ALREADY_FIXED | scratch_registry.cpp streamFnv1a 1MiB |

### #1460 cli/sdk
| 子项 | 状态 | 证据 |
|---|---|---|
| [P1] advisory 硬 gate + 相对 argv[0] 错 CWD | ALREADY_FIXED | external_process.cpp:447-457,519-526 validateArgvAnchored |
| [P2] PATHEXT 忽略 | **REMAINING** | :292-299,492,503 只试原名+.exe → WP-B |
| ipc setEventSink 顺序 | ALREADY_FIXED（顺序）但**锁内回放死锁未修** | :419-437 → WP-A |
| batch runner 失败记 ok | ALREADY_FIXED | cli_batch_runner.cpp:472-484 |

### #1462 python/plugins/platform
| 子项 | 状态 | 证据 |
|---|---|---|
| [P1] startWorker zombie | ALREADY_FIXED | python_worker_process_pool.cpp:552-585 |
| rebind UAF | ALREADY_FIXED | python_plugin_adapter.cpp:126-132 |
| unknown-method 无 reply | ALREADY_FIXED | app_interface_bridge.cpp:505 + daemon timeout |
| 重复注册劫持 | ALREADY_FIXED | plugin_runtime_host.cpp:436-444 first-wins |
| manifestOperator 悬垂 | ALREADY_FIXED | 已删除 |
| durable_sidecar Win32 | ALREADY_FIXED | :157-175 |
| [P2] acquireWorker 忙等重入 | **REMAINING** | pool:134-152 + adapter:92-107 → WP-F |

### #1463 agent_ops（全部已修）
| 子项 | 状态 | 证据 |
|---|---|---|
| goal-normalization 驱逐 | ALREADY_FIXED | session_journal.cpp:109-130 pin + supersede |
| bench containment 前缀 | ALREADY_FIXED | trace.cpp:91-130 词法归一 |
| recipe 扫描竞态 | ALREADY_FIXED | recipe_registry.cpp:71-83 ec 重载 |
