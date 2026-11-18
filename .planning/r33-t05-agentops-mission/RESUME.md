# R33-T05 RESUME — 上下文恢复要点

新 shell 必须显式恢复（绝不假设旧环境仍在）：

```bash
EXPRS_TRACK=t05-agentops-mission
EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r33-t05-agentops-mission-20261117T173601Z
EXPRS_BRANCH=zcode/r33-t05-agentops-mission-20261117T173601Z
EXPRS_BASE_BRANCH=master
EXPRS_BASE_SHA=1dfb04382e0fda43ee99665bed8b98666b3f92d7
EXPRS_BUILD=$EXPRS_WT/build-dev
EXPRS_CONFIG=Debug
EXPRS_PLAN=$EXPRS_WT/.planning/r33-t05-agentops-mission
EXPRS_HOST_LOCK=$HOME/.cache/exp-rs-agent/host-build
cd "$EXPRS_WT"
```

- 构建前缀（双锁，-j4 上限）：
  `python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 900 -- python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 120 -- cmake --build "$EXPRS_BUILD" --config Debug --parallel 4 --target <目标>`
- 测试前缀：两层 `--parallel 1 --max-parallel 4`，payload `ctest --test-dir "$EXPRS_BUILD" -C Debug -j1 --output-on-failure -R <筛选>`。
- 锁忙退出 75 → 先做不占锁工作再重试；主机上 R33-T06 等其他轨可能正在构建。
- gh 网络偶发 TLS 超时 → 重试循环 3-5 次。
- 先读本目录 LEDGER.md / DECISIONS.md / PLAN.md，再继续。
- 提交前刷新：open PR/branches/issues + fetch origin；无开放 PR 时排他表以最新 master 为准。
