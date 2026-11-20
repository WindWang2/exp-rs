# PLAN — R35-T02（20 WP，按真实价值推进，每片独立可交付）

首包（本会话优先）：
- **WP01** python worker readLine 绝对交换时限与 trickle 防拖延（静态候选已由控制流读取确认：数据持续到达时 `continue` 绕过 deadline；wall-clock 非单调）。
  - 修复：QElapsedTimer 单调 deadline、每轮循环检查、等待用剩余时间。
  - RED：当前 HEAD 用真实 QProcess trickle fixture 复现（trickle 有界 8s，防 CTest 挂死）。
  - GREEN：修复后超时误差有界；同时覆盖合法分段帧、完全静默、stderr 喧嚣、失败后 session 不可复用、新 session 恢复。

后续候选（按顺序，够时间即做，各带 RED→GREEN 证据）：
- WP18 增量：ensemble `acquireEnsembleSessions`（399–426）未传 cancelPredicate（对照 model_execution_service.cpp:247–249 已传）→ 父任务取消在成员加载等待阶段不可退出。静态候选需复现后修。
- WP05 manifest typed 参数（provider.timeout_ms/max_body_mb 的 toInt(default) 弱类型）。
- WP09 GPU leaf 两测试合同冲突裁决（test_runtime_gpu_boundaries_r4 vs test_gpu_plane）。
- 其余 WP 视会话余量。

验证：`ctest --test-dir build-dev -C Debug -j1 -R <本轨及受影响>`，实际注册 case 名以 `-N` 为准；构建 -j4 双锁。
交付：R1 独立 review（子 agent 读全 diff）→ 修复 → R2 → push → PR。
