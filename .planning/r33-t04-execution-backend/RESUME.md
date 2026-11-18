# RESUME — r33-t04-execution-backend

## 固定变量（每个新 shell 前部恢复）

```bash
EXPRS_TRACK=t04-execution-backend
EXPRS_REPO=/home/kevin/projects/rs-studio/main
EXPRS_BASE_BRANCH=master
EXPRS_BASE_SHA=1dfb04382e0fda43ee99665bed8b98666b3f92d7
EXPRS_BRANCH=zcode/r33-t04-execution-backend-20261117T173248Z
EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r33-t04-execution-backend-20261117T173248Z
EXPRS_BUILD=$EXPRS_WT/build-dev
EXPRS_CONFIG=Debug
EXPRS_PLAN=$EXPRS_WT/.planning/r33-t04-execution-backend
EXPRS_HOST_LOCK=$HOME/.cache/exp-rs-agent/host-build
```

## 状态快照

- 2026-11-18 创建。基线 master `1dfb04382e`（#2134/#2135/#2136/#2137/#2139 已全部合并；当前 0 个开放 PR）。
- 同批 R33-T01（SAR）、T02（模型runtime）、T03（data/dataset）worktree 活跃；T03 正在改 `src/data/CMakeLists.txt`（T04 若触碰该文件须最小 hunk）。T06 已近收尾。
- 远端无 R33-T04/T05 分支。T04 写集（src/jobs、src/runtime 非GPU、src/processing/framework 调度/缓存消费/输出提交、src/workflow、CLI workflow入口、src/sdk/exprs/workflow_schema.*、src/verify/**、src/verify_adapters/**、src/verification/** 参考、experiment/teaching/study/grader/science_context 后端、src/data/execution_fingerprint.*、rs_spectral_index_operator.cpp、rs_change_streaming.cpp、cartography_operators.cpp）全部可用。
- 构建资源：整机 -j4 上限，双锁 = host lock `$EXPRS_HOST_LOCK` + build dir 锁，经 `scripts/dev/resource_guard.py`。

## 恢复步骤

1. 恢复上述变量；`cd "$EXPRS_WT"`；核对 `git symbolic-ref --short HEAD == $EXPRS_BRANCH`。
2. 读 `LEDGER.md`、`DECISIONS.md`、`PLAN.md` 最新检查点。
3. `git fetch origin --prune`，若 master 前进，先完成当前 WP 再干净 rebase。
