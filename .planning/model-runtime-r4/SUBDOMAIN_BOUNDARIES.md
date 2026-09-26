# SUBDOMAIN_BOUNDARIES — Track 15 四子域边界用例总表（WP-G 收口工件）

每行可追溯到 ctest 输出中的测试名（TEST_CASE 原文）与提交号。注入手段与断言逐行不同且真实
（无"同场景改名凑数"）。并发类用例均为真多线程（std::thread / QProcess 实体）。

## worker 子域（tests/test_runtime_worker_boundaries_r4.cpp，12 用例）

| # | 边界类型 | 注入手段 | 测试名（TEST_CASE 前缀） | 提交 |
|---|---|---|---|---|
| W1 | TTL 严格边界（==TTL 不过期） | 注入时钟精确 +1ms | "Lease TTL boundary is strict…" | TBD |
| W2 | 检疫>过期 优先级 | 检疫后持任务静默越 TTL | "Quarantine takes precedence over expiry…" | TBD |
| W3 | 检疫对 liveness 粘性 | 心跳帧×5 不解除 | "Liveness never lifts quarantine…" | TBD |
| W4 | reset 清零 streak/重复检疫计数 | 3 败→reset→再 3 败 | "reset clears the failure streak…" | TBD |
| W5 | TTL=0 退化配置 | 零 TTL + 1ms 静默 | "Degenerate TTL=0 expires on any silence…" | TBD |
| W6 | takeover 预算=0 | maxTakeoverRetries=0 | "maxTakeoverRetries=0 grants no takeover at all" | TBD |
| W7 | 未见 worker 的 poison 记账 | 无 onJobStart 直接失败×3 | "onJobOutcome on a never-seen worker…" | TBD |
| W8 | 二次 onJobStart 重置静默窗 | 900ms→start→900ms | "A second onJobStart re-arms the silence window" | TBD |
| W9 | 协议层深嵌套敌意帧 | 4096 层嵌套数组字节 | "parseFrame refuses deeply nested frames…" | TBD |
| W10 | 非对象根/畸形 v 门 | `[1,2]`/`"x"`/`42`/v:{} 等 | "parseFrame refuses non-object roots…" | TBD |
| W11 | 错误帧取消判别中性 | message 为对象/缺失/code=cancelled/legacy 文本 | "frameErrorMeansCancelled is neutral…" | TBD |
| W12 | error frame 旧字节兼容 | 无 code 键 + 单行传输 | "makeErrorFrame stays legacy byte-compatible…" | TBD |

## gpu 子域（tests/test_runtime_gpu_boundaries_r4.cpp，8 用例 + 压力/阶梯另计）

| # | 边界类型 | 注入手段 | 测试名 | 提交 |
|---|---|---|---|---|
| G1 | 设备 pin 不跨设备复用（#1094） | 双设备 backend + pin | "A device pin never reuses a warm session…" | TBD |
| G2 | pin 不存在设备 → NoDevice 无副作用 | deviceId=7 | "A pin to an unknown or unavailable device…" | TBD |
| G3 | Busy 公平界（warm 占位不重开） | 同设备 4 会话 + evictStale 回收 | "The per-device warm-session bound reports Busy…" | TBD |
| G4 | 复用路径 typed 诚实（granted<requested） | 同身份不同档复用【修复靶】 | "Reuse with a smaller granted footprint…" | TBD |
| G5 | 陈旧身份不复用、旧会话等待回收 | sig-1→sig-2 | "A stale identity is not reused…" | TBD |
| G6 | evictStale 保护在持会话（use_count>1） | 真共享 shared_ptr | "evictStale never recycles a session…" | TBD |
| G7 | 池析构归还全部 VRAM 记账 | 作用域析构 + backend 余额 | "Pool teardown returns every VRAM reservation…" | TBD |
| G8 | 零档请求/空 modelId 退化语义 | vramMb=0 无阶梯；modelId="" | "A request with no footprint and no ladder…" | TBD |

## chunk 子域（tests/test_runtime_chunk_boundaries_r4.cpp，7 用例）

| # | 边界类型 | 注入手段 | 测试名 | 提交 |
|---|---|---|---|---|
| C1 | 分区算术 0/负/INT_MAX【修复靶：typed】 | tileWidth=0、负宽、INT_MAX 积 | "Partition arithmetic is typed at the zero, negative and overflow boundaries" | TBD |
| C2 | tileSpecAt 越界/溢出【修复靶：typed】 | index==total、>total、巨分区 | "tileSpecAt refuses out-of-range indices…" | TBD |
| C3 | graph source 空缓冲契约违约【修复靶：typed】 | 返回 true 且 pixels=null | "A graph source breaching the buffer contract…" | TBD |
| C4 | completedTiles 交付语义钉桩 | sink 第 3 tile abort | "completedTiles counts payloads delivered…" | TBD |
| C5 | 0 字节 journal 补头（review P1 修复钉桩） | publish 故障 + resize 0 | "A zero-byte journal is re-headed on resume…" | TBD |
| C6 | journalAppend 故障点（真实分支，原零覆盖） | ArmedFault NextN=2 | "An injected journal-append failure commits nothing…" | TBD |
| C7 | marker 故障点（真实分支，原零覆盖） | ArmedFault NextN=1 | "An injected marker failure keeps the run resumable…" | TBD |

## exec 子域（tests/test_runtime_exec_boundaries_r4.cpp，6 用例）

| # | 边界类型 | 注入手段 | 测试名 | 提交 |
|---|---|---|---|---|
| E1 | 准入阶梯 ±1 字节精确边界 | 预算=峰值/峰值-1/最小-1 | "Admission ladder exact boundaries…" | TBD |
| E2 | AdmissionRefused 结构化 reason | 不可满足形状 | "AdmissionRefused carries the planner's structured…" | TBD |
| E3 | Spill 档真实 scratch 预算 | scratch=need / need-1 | "The spill rung needs declared scratch…" | TBD |
| E4 | governor 预算覆盖契约钉桩 | 配置 1MiB vs 调用方 1TiB | "Governor-configured budgets override a looser caller budget…" | TBD |
| E5 | 泄漏报告跨 governor 隔离【修复靶】 | A 泄漏→B 干净→报告为空 | "A fresh governor starts with a clean leak-report slate" | TBD |
| E6 | 写门背压（真线程解除阻塞） | 满额持住 + async 等待 | "Write gate: exact-cap admits…" | TBD |

## 跨子域补充套件

| 套件 | 用例数 | 说明 | 提交 |
|---|---|---|---|
| test_runtime_pool_stress_r4 | 1（4 线程×250 op） | 有界池真并发：typed outcome 全谱、预算不越界、id 唯一、限时完成、终态零残留（-j1 RUN_SERIAL 双跑） | TBD |
| test_runtime_oom_ladder_r4 | 2 | OOM 阶梯真实层级（planner 4 档 + 池 3 档），见 PROVIDER_OOM_MATRIX.md | TBD |
| test_runtime_publish_orphan_r4 | 4 | crash-orphan 窗口 A/重入/幂等/真线程竞争【竞争 fence 修复靶】/provenance 链完整 | TBD |
| test_runtime_python_channel_r4 | 5 | 退出码分类轴/SIGKILL+stderr 全量/进程树杀（/proc 对账）/typed AwaitStatus/in-flight 幂等 | TBD |
| test_provider_guard_r4 | 2 | provider 默认格硬守护（typed absence + UnsupportedRuntime） | TBD |
| test_model_manifest_r4 | 17 | P1-8 拒绝类 14×四元组 + 顺序回归 3（独立 WP-A 工件） | TBD |

## 计数

- 四子域核心边界用例：12+8+7+6 = **33**（每子域 ≥3 ✓，总量 ≥18 下限的 1.8 倍）
- 全部新增用例：33 + 15 + 17 = **65**（含压力套/阶梯/orphan/通道/provider/manifest）

（提交号在 Phase 2/4 验证绿后回填；本表由 WP-G 收口时逐行与 ctest 输出对账。）
