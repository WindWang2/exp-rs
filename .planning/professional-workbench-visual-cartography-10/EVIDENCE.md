# EVIDENCE

## Phase 0

- `git fetch --all --prune` → ok；`git rev-parse origin/master` → `7d78059d1a6d316d606656759a506d17bc5e3b55`
- `git worktree add ../exp-rs-professional-workbench-visual-cartography-10 -b zcode/professional-workbench-visual-cartography-10 origin/master` → ok
- `git check-ignore -v .planning/professional-workbench-visual-cartography-10/GOAL.md` → 匹配 `!` 否定规则（negation），`git add -n` 确认可跟踪
- `cmake --preset dev-default`（CMAKE_BUILD_PARALLEL_LEVEL=2）→ **exit 0**，"Build files have been written to: .../build-dev"（2026-09-14）
- 去重分析：见 BASELINE.md（30+ PR 逐条、4 个并行 10.0 OPEN PR、DEDUPE 对照）
- 措辞自查：`grep -E "尽量|适当|必要时|合理|充分|酌情" GOAL.md | grep -vc "grep -E"` → 见下

### 存在性断言（Phase 0 版，最终 PR 前重跑）

## Phase 1 (WP-A) 本地验证（第一轮）

- `cmake --build build-dev --target test_object_identity test_agent_workbench_context -j2` → 全部 Built
- `QT_QPA_PLATFORM=offscreen ctest -R "objectKindToken|primaryObject|selectedLayerIds|resolveSelectionAssetTargets|workbenchContextToJson|workbench:context" -j1` → **9/9 Passed**（2.79 s）
- 修复记录：`<QgsMapLayer>` → `<qgsmaplayer.h>`（大小写）；ContextRules 4 个选择谓词的重复定义移除（保留 selection_context.cpp 版本）；
  test_agent_workbench_context 链接改为 sicnu_agent（spatial_tool.cpp 拖入工具族）；
  jsoncpp 链接改用 `sicnu_link_jsoncpp` helper；构建文件损坏（与旧后台构建并发生成）已通过重新 `cmake .` 修复。
- 发现：PRE_TEST 发现模式下 ctest -R 需按 TEST_CASE 名称匹配，非二进制名。
