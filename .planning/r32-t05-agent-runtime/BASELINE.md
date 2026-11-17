# BASELINE — R32-T05

- 执行时间：2026-11-17（研究快照 2026-11-16）
- 实际 default branch（API）：`master`
- 本轨 base SHA：`53412c86bfd5742f0df0243b51500ba444f7dde1`（origin/master，合并 #2061 R30-T07）
- worktree：`/home/kevin/projects/rs-studio/exp-rs-r32-t05-agent-runtime-20261116T184011Z`
- 分支：`zcode/r32-t05-agent-runtime-20261116T184011Z`

## 已合并的既有能力（不重复实施）

- #2066–#2067：受控 Pi SDK vendor/校验 + 自有进程协议（P0/P1）
- #2068：Pi 工具经当前 Qt 宿主运行（P2）— AgentToolHost 接入 TaskCenter/Python 宿主
- #2069：settled Pi 会话无重放恢复（P3）— 完成态 checkpoint
- #2070：run 度量与双语交付稳定（P4）— payload-free usage
- #2072：Windows Unicode Python 资源 + agent UI 重叠修复（P5）
- #2063：审批后 Python 输出发布 + 桌面任务共享
- #2071：R31 计算/数据/执行（输出声明、named 多产物、VRT NoData、采样准入、模型 identity/预算）

## Open PR 排他

- **#2131**（R31-T02，head `zcode/r31-t02-pi-python-agent-20261115T135525Z`）：AS-015 端点安全一致性（`LlmStreamingClient::isLoopbackHost/endpointSecurityRefusal` 静态助手，Pi 启动前拒绝 insecure endpoint）+ `prepare_pi_runtime.py` verify_stage 遍历顺序 + 新增 `test_r31_t02_{core,gui}`。
  - 与本轨重叠文件：`src/agent/agent_copilot_dock_widget.cpp`（仅 `insecure_endpoint` statusChanged 处理与 `finishRunUi` 的 `m_piSessionActive` 条件）、`src/agent/runtime/pi_process.cpp`（仅 `start()` 入口安全拒绝）、`pi_conversation_runtime.cpp`（仅 startSession 入口）、`tests/CMakeLists.txt`（追加块）。
  - **本轨不重复其实施**；本轨的 #2073–#2080/#2106/#2107/#2109/#2111/#2112 均未被其覆盖。
  - 潜在文本冲突点小且局部（同文件不同函数）；rebase 时逐段理解。

## 证据边界（研究阶段的轻量检查，不作为交付通过）

- Pi product packaging 10 PASS、Python runtime packaging 19 PASS（包装脚本层级）
- Pi checkpoint suite 在未设锁定 SDK 路径时前置失败（Node 24.21.0 vs 锁 24.22.3）
- scratch probe：短 dummy key `1` → `CHECKPOINT_REDACTION_FAILED`（#2080 协议边界确认）
- 旧 Python worker 测试在本机 AF_UNIX 创建被拒（#2107 需真实环境复现）
- 未做：本 HEAD C++/CTest/GUI/真实ORT/Windows发布/云模型全量验证 —— 本轨用自己的新 HEAD 重验

## 关键架构事实

- Pi 仍 opt-in；Native 为迁移默认。不切换默认值。
- `AgentToolHost`：QPointer manager + `contextValid` atomic；`beginRun` 要求 ownerThread + manager + contextValid + lease 空闲。
- `AgentCopilotDockWidget`：`m_dataManager`/`m_canvas` 为裸指针（#2074）；`refreshConversationIdentity()` 流式期覆写 ID（#2075）。
- `PiProcess` 状态机：Idle→Starting→Ready→Running→(Cancelling|Closing)→Closed；cancel 用 `closeTimeoutMs`（≤3000ms，`start()` 校验上限）做期限（#2076）；`close()` 在 Starting 窗口与迟到 ready 冲突（#2077）。
- 脱敏：C++ `Private::redact()` 序列化子串替换 + 接收侧 `redact(json)!=json` 拒绝；JS `redactCheckpoint` 对 identity 字段值含 secret 即抛（#2080 短 key 全串碰撞）。
- 插件宿主 worker：`RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}`（多配置生成器实际落 `${BIN}/${CONFIG}/`），无 `install(TARGETS)`；stage 脚本查 `${BuildDir}/exprs_plugin_host_worker.exe` 多配置下不存在 → 静默跳过（#2109）。
- SDK checksum：声明允许大写 hex，比较大小写敏感（#2112）。
