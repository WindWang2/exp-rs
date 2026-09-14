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

## 回归矩阵（第一轮，本地证据）

| 套件 | 结果 | 备注 |
| --- | --- | --- |
| test_object_identity | 5/5 cases 全绿 | 新增 |
| test_agent_workbench_context | 4/4 全绿 | 新增 |
| test_selection_context + ContextFacts | 8/8 全绿 | 扩展字段回归 |
| test_provenance_section | 6/6 全绿（61 断言） | **master 上 4/6 红** → 本 track 修复（见下） |
| test_command_registry | 9/9 全绿 | 新命令族注册回归 |
| test_shortcut_conflicts | 7/7 全绿 | **master 无法编译** → 本 track 修复（见下） |
| test_mapspec（含 test_cartography_operators_10） | 601/602 断言 | 1 个失败为 PNG 渲染确定性，master 同样失败（本机字体环境），非本 track 引入 |

## 基线修复（如实声明，均为独立 commit 候选）

1. `tests/test_shortcut_conflicts.cpp`：master 在本工具链无法编译——
   `QRegularExpressionMatchIterator::captured()` 不存在（应为 match.next().captured()）。
   wb9 时段可编译，GCC 16 拒绝。最小修复：取 match 再 captured。
2. `tests/test_provenance_section.cpp`：PR #953 i18n 机械重写把
   `src/app/workbench/provenance_section.cpp` 的 tr() 源文改为英文，测试期望未同步
   → master 上 4/6 用例红。期望更新为现行源文（61 断言全绿）。
3. `test_mapspec` 的 PNG 渲染确定性用例在本机失败（master 同样失败）：环境级
   字体差异，记 OUT_OF_SCOPE，不由本 track 追逐像素确定性。

## OUT_OF_SCOPE（发现但不属于本 track ownership）

- master 的 PNG 渲染确定性用例在本机不稳定（字体环境）——建议制图 track 在
  渲染确定性测试中固定字体/禁用系统字体替换。

## Phase 8 最终验证（最终 HEAD）

- `git fetch origin && git rebase origin/master` → up to date
- `git diff --check` → 干净
- 冲突标记扫描（src/app、src/agent/cartography、新增 tests）→ 空
- 秘钥/凭据模式扫描（track diff）→ 空
- 存在性断言（goal-template 两条命令）→ 无 MISSING 输出
- 措辞自查 `grep -E "尽量|适当|必要时|合理|充分|酌情" GOAL.md | grep -vc "grep -E"` → 0
- 最终回归：26/26 套件全绿 + mapspec 601/602（环境用例 1 项，master 同）
- 工具调用计量（预算代理指标）：本 track 会话 ~700+ 工具调用，触及文件 69；
  各 Phase 时间戳/调用数入 TEST_MATRIX/PROGRESS。
