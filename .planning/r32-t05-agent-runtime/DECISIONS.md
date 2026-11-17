# DECISIONS — R32-T05（unattended 常规决定记录）

| # | 决定 | 依据 |
|---|---|---|
| D1 | #2076 用独立 `cancelTimeoutMs`（默认 30000ms，上限 120000ms）而非扩大 `closeTimeoutMs` | 传输关闭（shutdown 无需等待）与工具清理（sidecar 的 `#finish` 要求 pending 清空）是两类等待；WP05 oracle 要求"有界独立期限"。现有 `test_pi_process` 的 shutdown_timeout 用例只覆盖 close 路径，不受影响 |
| D2 | Cancelling 超时改报 `cancel_timeout`（原 `shutdown_timeout`） | 诊断准确性；无消费者依赖该字符串做 UI 映射（safeFailure 通用文案） |
| D3 | #2077 迟到 ready 在 Closing 态被校验后接受但不激活会话（不改状态、不发 ready 信号、重启 close 窗口） | WP06 oracle "禁止晚 ready 激活新会话"；`PiConversationRuntime` 已有 `m_closing` 守卫为证此语义正确 |
| D4 | #2073 在 `sendPrompt` 入口拒绝无 context（恢复输入框文本），`submitPendingPrompt` 失败路径清 pending 并用 `AgentToolHost::hasProjectContext()` 区分"无项目"与"忙租约" | oracle 要求准确诊断+可恢复+无延迟重放；新增查询 API 是向后兼容最小扩展 |
| D5 | #2074 dock 与两个 service 的 owner 指针改 `QPointer`；`setContext` 拒绝时仍镜像新 owner 并标记 `m_projectContextInvalid`，`finishRunUi` 重试 rebind | 服务层所有解引用已有真值守卫，QPointer 使守卫在对象销毁后真实生效；拒绝不再静默 |
| D6 | #2075 流式期项目切换改为延迟采纳（`m_projectIdentityRefreshPending`），`finishRunUi` 统一应用；checkpointReady/Failed 的失配路径先清 pending 再报保存失败 | issue 给出的修复方向（清 pending 在 return 之前）；createConversationRuntime 已重置这些标志，防御性重排只影响同 runtime 内的失配 |
| D7 | #2078 无效 direct token 时跳过 armed 审批（不置 pendingApproval），armed 条目仍消费 | 与既有 RING_FULL 路径的"错误即不批准"语义一致；oracle"合法一次批准仍成功且只能消费一次"。注：run() 的 post-hoc 诊断当前恒 non-repairable，该差异暂无公共可观察面——测试钉住 typed 拒绝+消费+正控制，代码层修复经 review 确认 |
| D8 | #2079 在 `DecisionRecord::fromJson` 做内层字段类型校验（inputs.run_id/rule_id/risk_class/root_cause_code、selected.action/plan_id），journal 层传播 typed refusal | 单一解析门槛覆盖 resume_reconciler/delivery_assembler 全部消费点；"损坏→typed refusal"与 E2E-04 一致 |
| D9 | #2080 脱敏阈值 8 字符：低于此长度的 key 视为本地占位凭据（不做子串替换、不触发碰撞拒绝），两侧（C++ `redact()` 与 JS `redactCheckpoint`）常量一致 | 短 key 子串替换必然碰撞 schema/hash/普通文本（"1"会替换所有数字）；<8 字符无可保护熵。真实密钥（≥8）保持全严格语义不变（既有 fixtureKey 测试不回归） |
| D10 | #2109 用 `install(TARGETS … RUNTIME DESTINATION bin)` + stage 脚本改为硬失败校验 | 与 app 可执行文件安装约定一致；多配置生成器的 per-config 子目录问题由 install 规则消除；"缺worker明确失败" |
| D11 | #2111 机械替换 36 处 `fs::path(std::string)` → `sicnu::portable::pathFromUtf8`（含 4 处 `path / string` 除法） | 复用 T03 维护的既有 portable API；Linux 腿加非 ASCII 全链测试，Windows ACP 差异为 NOT_EXECUTED |
| D12 | #2112 大小写不敏感比较（逐字节 tolower） | 声明已接受 A-F；错误信息保留声明原文 |
| D13 | #2106 给 `test_python_agent_mvp` 设 `RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}` + LABELS SLOW；`test_python_agent_gui_delivery` LABELS LONG_RUN | CI 的 `run: build/test_python_agent_mvp` 是消费契约（改 target 输出而非改 CI 更小）；TEST_INFRA 惯例 TIMEOUT≥300→SLOW、≥20min→LONG_RUN。不触发/等待 CI |
| D14 | #2081 只修 `tests/test_bilingual_delivery_ui.cpp` 的 fixture（绑定真实 DataManager、真实 registry 工具）；`sicnu_zh_CN.ts` 缺失的 Base 上下文写 HANDOFF 交 T06 | 整文件归本轨；.ts/lupdate 门禁归 T06；测试期望改为宿主 approvalPlan 的真实结构（`approved_single_call`） |
| D15 | test_pi_process 本地验证：用真实 vendor recipes + 宿主 node 组装 stage（`/tmp/mk_pi_stage.py`，不入仓） | `verify()` 只校验 receipt 哈希、不执行 node 二进制；fixture 自建自洽 receipt，不弱化任何断言；锁定 SDK 全量 lane 在 Linux 无 Windows 发行版仍 NOT_EXECUTED |
| D16 | #2107 按 issue 建议改 `__shm_dtype__` 为整数 code `0` | 保留 -32603 "segment not found" 断言语义；本机 AF_UNIX 受限另测 |
