# RESUME — R29-T02 数据、空间 I/O、任务执行与真实产物

## 固定轨道标识（每个 shell 入口先恢复）

```bash
export EXPRS_TRACK="t02-data-runtime"
export EXPRS_REPO="/home/kevin/projects/rs-studio/main"            # 只读参考（别人的检出）
export EXPRS_WT="/home/kevin/projects/rs-studio/exp-rs-r29-t02-data-runtime-20261108T164136Z"
export EXPRS_BRANCH="zcode/r29-t02-data-runtime-20261108T164136Z"
export EXPRS_BASE_SHA="130990a39655338da3790f0aab0bea38a2fb7d20"   # origin/master @ 2026-11-09
export EXPRS_BUILD="$EXPRS_WT/build-dev"
export EXPRS_PLAN="$EXPRS_WT/.planning/r29-t02-data-runtime"
export EXPRS_HOST_LOCK="${XDG_CACHE_HOME:-$HOME/.cache}/exp-rs-agent/host-build"
export QT_QPA_PLATFORM=offscreen
export CMAKE_BUILD_PARALLEL_LEVEL=4
export CTEST_PARALLEL_LEVEL=1
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
```

## 主机锁规约

- host 锁：`$EXPRS_HOST_LOCK`（四轨 canonical 路径，`--parallel 4 --max-parallel 4`）。
- 本构建锁：`--lock-dir "$EXPRS_BUILD"`。测试 payload `ctest -j1`。
- 锁忙退出码 75 → 记账后做无锁工作，稍后重试；不删活锁。

## 当前状态（最新在前）

- [x] worktree 创建自 origin/master=130990a396，分支 zcode/r29-t02-data-runtime-20261108T164136Z。
- [x] D-T05-CHAIN1 根因复读（r28-t05 ISSUE_TRIAGE）：marker 身份键含 canonicalParams（现已含 outputPath），但 markerMatches 只查版本+身份，不验证产物存在；alreadyPublished 直接绿色返回。
- [x] rs_change_streaming.cpp:769-822 已有 per-operator 重开+identity 元数据校验（fail-closed typed error）——公共契约参考。
- [ ] 构建 build-dev（进行中/待启动）。
- [ ] WP01 基线复现六假设。
- [ ] WP02 框架级 published 验证。

## 活动 PID / 锁

- （无）
