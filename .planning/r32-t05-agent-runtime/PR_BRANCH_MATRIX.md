# PR_BRANCH_MATRIX — R32-T05（2026-11-17 实查）

- 本轨分支：`zcode/r32-t05-agent-runtime-20261116T184011Z`（base `53412c86`，未推送）
- Open PR：仅 #2131（R31-T02，`zcode/r31-t02-pi-python-agent-20261115T135525Z` → master，+1553/−22）
  - 重叠文件与处置：
    - `agent_copilot_dock_widget.cpp`：#2131 改 statusChanged 的 insecure_endpoint 分支与 finishRunUi 一行条件；本轨改 setContext/sendPrompt/submitPendingPrompt/refreshConversationIdentity/checkpoint 守卫/finishRunUi 尾部 —— 函数级不相交，rebase 局部。
    - `pi_process.cpp`：#2131 改 start() 入口（endpoint 拒绝 + config 重置）；本轨改 redact()/cancel()/close()/ready 处理/start 校验行 —— 不相交。
    - `pi_conversation_runtime.cpp`：#2131 独有（本轨未改）。
    - `tests/CMakeLists.txt`：两侧都在文件尾部追加块 —— 相邻追加，rebase 时保留两者。
    - `prepare_pi_runtime.py`/`llm_streaming_client.*`/`test_r31_t02_*`：#2131 独有。
- 远程分支快照（11 远程分支）未逐一展开：其余 10 个 tip 对应已合并 PR head（快照结论，未变化信号：open PR 数仍为 1）。
- 本地未推送事实：本轨分支有 5 个 commit 未推送；无对他轨分支的写入。

## 更新（2026-11-17 rebase 后）

- master 前进至 `53c960a2`（#2131 R31-T02 AS-015 合入 + #2133 T06 Windows Unicode 工作区/bilingual 交付合入）
- 本轨已 rebase：`53412c86..53c960a2` 期间的重叠处置：
  - `tests/test_bilingual_delivery_ui.cpp`：#2133 的权威版本（fixture + .ts Base 上下文）取代本轨等价修复——整文件取 theirs，理由：100% 超集覆盖（含 reject-无执行断言）
  - `tests/test_agent_copilot_ui.cpp`：#2133 的 producesFile 修复（含 DataType::Raster）取代本轨 #2130 等价修复
  - `tests/CMakeLists.txt`：两侧尾部追加块都保留（R31-T02 + R32-T05）
  - dock/pi_process 生产文件：函数级合并且语法验证通过（双方修复共存）
- #2130/#2081 的测试侧修复现以 master 版本为准；本轨 commit 中对应 hunk 已在 rebase 中消解
