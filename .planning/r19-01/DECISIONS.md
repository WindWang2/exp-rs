# R19-01 DECISIONS（无人值守模式：选项 + 所取默认）

| # | 决策点 | 选项 | 取默认 | 依据 |
|---|---|---|---|---|
| D1 | git fetch 直连超时 | a) 等待重试 b) gh API 只读核对 c) 跳过 recon | **b + push 时重试** | gh API 实测可达且给出远端 master SHA == 本地；基线即最新；不虚报 LIVE recon |
| D2 | 构建目录 | 复用 r18-b 构建 vs 独立 `build-r19-01-data-model-runtime` | **独立** | 任务书：构建目录绝不复用；源码树隔离 |
| D3 | 生成器 | Ninja（CLAUDE.md preset 默认无生成器，r18-b 用 Unix Makefiles）| **Ninja** | 任务书 §3 示意 `CMAKE_GENERATOR=Ninja`；-j2 显式传递覆盖 shell 别名 |
| D4 | 已修 issue 的重复劳动 | 重提交修复 vs 测试钉住 | **测试钉住/跳过** | 任务书明确"不能重新提交同一修复"；以代码可复现性为准 |
| D5 | #1576 OpenVINO traits | 改设备解析 vs 保持文档化决策 | **保持，PR 说明** | 注释已给出完整设计理由（CUDA-centric 解析无法看见 Intel GPU；selectDevice 已做降级修复）；改动会扩大行为面 |
| D6 | #1597/#1595/#1588/#1589 | 修（越界）vs CROSS_TRACK 记录 | **CROSS_TRACK 记录** | src/cli、src/preflight、src/science_context 不在本轨写集；越界违反 surgical 原则（且 1597/1595 实测已修）|
| D7 | T1.1 修法 | 特判 zarr.json vs 通用最长后缀匹配 | **通用最长后缀匹配（平局取先注册）** | 修根因（两段式扩展名结构不可达）而不只是症状；平局规则保持 bare .json → GeoJSON 的现行为（向后兼容）|
| D8 | 种子测试复现时机 | 等本轨构建 vs 借主仓库构建 | **等本轨构建** | 主仓库无现成构建；跨轨借用违反隔离规则 |
| D9 | 全局锁 | 每 worktree 独立锁 vs 共享 `$GIT_COMMON_DIR/r19-global-j2-build` | **共享** | 任务书 §3；实测 r19-02 持锁时本轨正确排队 |
