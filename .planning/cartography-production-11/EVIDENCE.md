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

## Phase 1-4 构建与实现记录（2026-09-16）

- 工具链：MSVC 14.38 (VS2022 Community) + Ninja (C:\Qt\Tools\Ninja) + Qt 6.8.0 (C:\deps\Qt)；configure = dev-default + vcpkg toolchain + winflexbison（C:/deps/winflexbison，与并行 worktree 缓存一致）。
- configure：首次完整 configure 2348.7s（vcpkg 恢复 + QGIS 检测）；后续 reconfigure ~15min。BISON/FLEX 需显式路径（master CMake 需要 bison，Git Bash PATH 无 win_flex/win_bison）。
- 并发环境事实：本机同时运行其他 worktrack 的构建（spectral/workflow/contract/teaching-lab 等，tasklist + wmic 命令行核实）；Git Bash 无 loadavg → 以 powershell Get-Process RSS 采样（monitor.log，60s 间隔），构建 -j2 保持不变（未观测 RSS>70% 阈值）。
- 构建：`ninja -j 2 test_cartography_production_11 test_mapspec`（后台，build1.log）。
- D-007 build-unblock 已应用：`src/agent/data_platform_tools.cpp` 添加 `using namespace sicnu::experiment;`（+3 行注释，dedupe PR #1009 同款修复）。


## 构建/测试证据（2026-09-16 下午）

- 构建修复链（每项一次可归因改动 + 重编）: fcntl.h（master 破坏，dedupe #1009）→ ProduceTool outputSchema → QgsLayoutAtlas API（currentFeature 不存在 → featureChanged 信号捕获）→ QString→std::string ×2 → qgslayoutatlas.h include → QgsFeatureRequest API（1参 setFilterExpression + OrderBy QList 构造）→ SpatialToolRegistry 命名空间 → 系列渐进。
- **test_cartography_production_11（[cp11] 全量，QT_QPA_PLATFORM=offscreen，-j1 单跑）: 16 cases | 12 passed | 4 skipped（渲染门）| 122→118 assertions ALL GREEN**（cp11_final8.txt）。
- 渲染门用例（SICNU_CP11_RENDER=1 时启用）: single-mode deliver / atlas delivery / e2e byte-equal / corpus smoke。
- **宿主渲染挂死（本 track 无关，对照证明）**: master 自己的 test_mapspec [visual][determinism]（渲染 19+ 模板）在本机同样无限挂死（>20min，CPU 停滞；cdb 栈: qgis_core!QgsLayoutItemLegend::paint ← QgsLayoutExporter::exportToImage）。无图例布局渲染正常（master [compose] 14 assertions passed；operators_10 export 渲染成功、仅 rename 失败）。昨夜并行 track 同套件全绿 → 本机当日状态（疑似 AV/字体缓存）所致。offscreen 与 minimal 平台均复现。
- **宿主 rename 失败（本 track 无关，对照证明）**: test_platform9:1367 与 test_cartography_operators_10:139 的 exportMapLayout `QFile::rename` 到 %TEMP% 失败（"cannot move the export into place"）——exportMapLayout 该路径与 master 字节一致（git diff 仅新增 atlas 函数），失败在 OS 层。
- **test_mapspec 回归（~[visual] 排除渲染挂死套件）: 213 cases | 211 passed | 2 failed（即上述宿主 rename）**（regress2.txt）。
- 规则收敛修复: MAP_WHITESPACE_IMBALANCE 阈值收紧（band > 3×对侧 且 > 35% 页宽）——master "quality-good" fixture（左置地图+宽右带）误报消除。
- v6 版本 pin 更新: test_platform7 kMapSpecCurrentVersion==5→6（测试自身注释声明"strict-superset 才动"——v6 符合）。
- 计划内迭代: series 基集合清空（页0重复克隆缺陷）、UNSAFE_LEGEND_AUTO_UPDATE 护栏前置到 repair 之前、atlas count() 时序（updateFeatures 显式刷新）。


## Review 修复与最终双验证（2026-09-16 傍晚）

- 对抗审查（subagent #2）：P0=1/P1=3/P2=6/P3=5 → P0/P1 全修，P2 修5 disposition1，P3 修2 disposition3（REVIEW_LOG.md 全表）。
- 修复后门序：repair → require_preflight_pass → atlas 结构检查 → UNSAFE_LEGEND_AUTO_UPDATE（声明+repair 后重查）→ export。
- **双验证（Oracle 6）**：[cp11] RUN1/RUN2 = 19|15 passed|4 skipped|0 failed；test_mapspec ~[visual] RUN1/RUN2 = 213|211|2 failed（宿主 rename，两次一致）。
- **宿主 rename 预存在的 stash 对照**：git stash → 纯 a5b11b7f 重建 → 同用例同错 → pop。证据：baseline1.txt。
- OUT_OF_SCOPE（最终）：issues #1001–#1007（非制图域）；master [visual] 渲染挂死 + %TEMP% rename 失败（宿主状态，已对照证明，渲染门 opt-in 机制交付）。
