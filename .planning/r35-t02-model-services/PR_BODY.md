# fix(models): absolute worker read deadline, ensemble acquire cancellation, typed provider knobs (R35-T02)

## 原问题与影响

R35-T02 模型服务轨三个已被控制流读取 + 真实 fixture 复现的合同缺口：

1. **WP01 — python worker 读取时限可被绕过（P1）**：`python_worker_provider.cpp::readLine`
   只在"无数据"分支检查 deadline；一个每 <100ms 吐少量无换行字节的 worker 可以无限拖延
   交换直到 256 MiB body cap，且 deadline 用 wall-clock（NTP 回拨可延长）。真实 trickle
   fixture 在修复前 ~8.4s 才失败（timeout=1200ms），修复后 ~1.3s。
2. **WP18 — ensemble 成员获取阶段不可取消（P1）**：`model_ensemble.cpp::acquireEnsembleSessions`
   未把父任务取消传给 `acquireWithFallback`（单模型路径 `model_execution_service.cpp:247-249`
   已传）。父任务取消后，等待同 identity in-flight load 的 ensemble 会一直阻塞到 loader
   自行完成；后续成员照常启动。
3. **WP05 — provider 旋钮弱类型静默默认（P2）**：`model_catalog.cpp` 用
   `toInt(default)` 读 `runtime.provider.timeout_ms / max_body_mb`——字符串/bool/1.5/-5/1e12
   全部静默变成默认值，作者声明无声消失；程序构造 ModelInfo 的负值也静默 clamp。

## 已合并基线

- base = master `120677e6e1`（#2144 VRAM/wire 准入、#2137 feather/TensorBlob 等已在内，不重做）。

## 本次增量

- `readLine` 改 `QElapsedTimer` 单调绝对 deadline：每轮循环检查、等待用剩余时间
  （min(100, remaining)）；deadline 内完成的合法分段帧照常成功（partial-frame 语义保留）。
- `acquireEnsembleSessions` 传入 `[&context]{ return context.isCancelled(); }`，失败分支先
  `context.throwIfCancelled()`（与单模型路径相同优先级）；不可中断的第三方同步 factory
  如实标注为不可取消 lane。
- catalog provider 旋钮 typed 拒绝（跟随 `max_concurrent_members` 先例：floor/int-range/
  "must be a whole number"），missing/null→默认、0→文档化 provider 默认；python 工厂对
  程序化 ModelInfo 负值在进程启动前 typed 拒绝。

## 边界 / 接口兼容

- 不改 wire 协议、不改 4096 reserve、不动 T05 python_environment/SDK、不动 T03/T04 底层。
- 历史 manifest（无旋钮/null/0/合法正整数）行为不变；新拒绝只针对原先就无声变成默认的坏 token。
- `min(100, remaining)` 等待粒度：CPU 空闲时每帧 deadline 精度 ≤100ms 轮询 + 调度。

## 验证（本 worktree 实测，双 resource_guard 锁，CTest -j1 offscreen）

- 构建：`cmake --preset dev-default -DOpenCV_DIR=<host shared opencv5>`（Debug，-j4 上限，8 目标 100%）。
- **RED（pristine HEAD + 新测试）**：trickle case `8084 <= 5000` ms 界断言失败（deadline 被 8s trickle 绕过）；ensemble 取消 `waitUntil(...,5000)=false`（卡在 in-flight waiter）；manifest typed 12+6 断言失败（坏 token 无声过、负值 acquire 成功）。
- **GREEN + 二跑**：终验 ctest（-C Debug -j1，`-R 'R35-T02|…26 个 case 名过滤词…'` 与上同）**53/53 passed, 0 failed**。精确构成（按生成的 `*-tests.cmake` 核对）：新 case 7（deadline 4 / ensemble 1 / typed 2，跑两轮全过，trickle 2.15s）＋ test_provider_python 14/15 匹配 case（未匹配项为 "missing worker script…"，其合同由新 typed 测试与既有套件其余 case 覆盖）＋ test_ensemble_parallel 7/7 ＋ test_provider_fallback 3/3 ＋ test_model_manifest7 13/15 匹配 case ＋ test_r33_t02_wire_admission 9/9。
- 未执行：Windows/ORT/GPU lane（本机无环境），如实标 NOT_EXECUTED。

## base/head/review

- base: `120677e6e1`（master @ 2026-11-18 22:32 +0800）
- head: `c654aa2520`（最终 push head，见 PR）
- R1: 独立子 agent 全 diff 审查，**approve**（0×P0/P1/P2，6×P3：2 已修、4 有意选择）。
- R2: 独立子 agent 复查最终提交 diff，**approve-with-fixes**（0 P0-P2，4×P3 文档项，全部已修：53 构成精确化、LEDGER 统计、DECISIONS 措辞、占位符）。
