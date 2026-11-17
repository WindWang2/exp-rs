# HANDOFF — R32-T05 → 其他轨

## → T06（桌面与交付体验）

1. ~~#2081 的 .ts 部分~~ **已由 master #2133 交付**（Base 上下文 pin + freshness 保留）。本轨 rebase 时采纳其权威版本：`test_bilingual_delivery_ui.cpp` 的 fixture 修复与 .ts 同步到位；freshness 门禁的 QgsFieldMappingModel 相关项如仍缺，属 #2133 后残差，归 T06。
2. **本轨新增 tr() 文案**（context `AgentCopilotDockWidget`）：三条英文源串（"The agent is still finishing work…"、"Open or create a project…"、"No project context is available…"）需下次 lupdate 纳入 zh_CN catalog；本轨不做整文件 lupdate。
3. **#2109 stage 变更**：`stage_windows_application.ps1` 现要求 install 后 `bin/exprs_plugin_host_worker.exe` 存在（缺失即 throw）。T06 的 Windows 发布验收需在含本轨改动的分支重跑该脚本。

## → T02（模型推理）

- 本轨改动了 `src/agent/agent_copilot_dock_widget.cpp`（setContext/finishRunUi/sendPrompt/refreshConversationIdentity/checkpoint 守卫）与 `pi_process.{h,cpp}`（cancelTimeoutMs/迟到 ready/短 key redact）。PR #2131（R31-T02）在同文件的 `finishRunUi` 与 `start()` 处有小增量；rebase 时逐段合并，语义不冲突（本轨不动 AS-015 判定）。
- `PiProcess::Config` 新增 `cancelTimeoutMs`（默认 30000）；若 T02 侧构造 Config 需同步无需显式设置（默认值向后兼容）。

## → T04（执行与业务后端）

- `AgentToolHost::hasProjectContext()` 为新增只读查询（`beginRun` 语义未变）；dispatcher/TaskCenter 未触碰。
- `test_python_agent_mvp` 现输出到 `${CMAKE_BINARY_DIR}`（#2106）：若 T04 的脚本/文档引用 `<build>/tests/test_python_agent_mvp` 路径需同步。

## → T03（数据与 IO）

- #2111 消费 `sicnu::portable::pathFromUtf8`（36 处 SDK 插件路径构造）。现成 API 已够用，无需新增；如后续 pathFromUtf8 语义变化请保持"UTF-8 字节 → fs::path 精确转换"契约。
