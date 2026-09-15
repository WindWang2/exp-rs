# EVIDENCE — cartography-production-11

政策：每个 capability claim → 本地命令 + exit code；不可执行项标 not-executed。无在线 CI 依赖。

## Phase 0

- `git fetch origin --prune && git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
- `gh pr list` → #1009 (MERGEABLE), #1008 (CONFLICTING)；#991/#992 已合并（master log c5d4aafe/1cea9892）
- `gh issue list` → #1001–#1007（全 OUT_OF_SCOPE，dedupe 见 BASELINE.md）
- subagent #1 只读审计（Explore, 67 工具调用）→ BASELINE.md Top10 缺口
- `git show a5b11b7f:src/workflow/pipeline_run_coordinator.cpp` 含 `<fcntl.h>`(:25) → #1009 所述断点 1 在基线不成立
- `git show a5b11b7f:src/agent/data_platform_tools.cpp` :1184/:1221/:1265 无限定 `BenchmarkService`；`benchmark_service.h` namespace `sicnu::experiment`；TU 内仅 `using namespace sicnu::dataset` → 编译必败 → D-007 build-unblock
- `git diff --name-only 007e70cf..a5b11b7f -- src/agent/cartography src/agent/mapspec src/app/cartography data/cartography docs/cartography` → 空（主仓库审计有效）
- 构建环境：Windows MSVC Ninja；preset dev-default → build-dev。CMAKE_BUILD_PARALLEL_LEVEL=2 / ctest -j1 / QT_QPA_PLATFORM=offscreen
- 负载测量：Git Bash 无 load average；以 tasklist RSS 观测（见 PERFORMANCE.md），保持 -j2 上限
- 首次 configure/build：待填
- OUT_OF_SCOPE：issues #1001–#1007（io/workflow/dataset/georef 域）；ISSUES.md 旧 D3 backlog C-1 已由 cartography_operators 关闭（五算子在册）；其余条目属 temporal/SAR/高光谱域，不实施
