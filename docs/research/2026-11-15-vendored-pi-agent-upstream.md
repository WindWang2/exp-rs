# Vendor Pi 作为产品内 Agent：上游核实记录

日期：2026-11-15。范围：上游能力、实际公开 API、Windows 分发约束和接入选择；不代表已完成集成或联网模型验证。产品实施方案另文记录。

## 1. 来源、版本和许可证

- 官方站点为 [pi.dev](https://pi.dev/)，原 `badlogic/pi-mono` 已重定向至 [earendil-works/pi](https://github.com/earendil-works/pi)。现行 npm 命名空间为 `@earendil-works`，不能沿用旧 `@mariozechner` 的接口示例。
- 本次直接查询 [npm registry latest](https://registry.npmjs.org/@earendil-works/pi-coding-agent/latest)、上游 Git refs 和 [发布页](https://github.com/earendil-works/pi/releases/tag/v1.5.2)：最新正式版 **1.5.2**，发布于 **2026-11-10**；`v1.5.2` 与 `stable` 均指向 **`b4afdddf44e059c67ccf5097e5317d913521b470`**。查询时 `main` 为 `0db9861ec01842f0ade3fa93d95a59f378d92a8a`；实施应锁正式版 SHA，不跟随 main。
- `pi-ai`、`pi-agent-core`、`pi-coding-agent` 均为 **1.5.2**，最低 Node **22.19.0**。依据：[agent package.json](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/agent/package.json#L1-L50)、[AI package.json](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/package.json#L1-L115)、[coding-agent registry](https://registry.npmjs.org/@earendil-works/pi-coding-agent/1.5.2)。该 registry 的 `dist.integrity` 为 `sha512-mUWI04rqF1Vd/m1q+Ps1XeWsTXEVvRUnsmRa83I3Dzua0Tx5bYS93imeqtIfrXf7PkkaR6ERz+AuQhIJlom5yA==`，包不是 shrinkwrap 分发。
- 许可证为 **MIT**，允许修改和随产品分发，须保留版权与许可证文本；传递依赖须另做 notices/SBOM。依据：[固定版本 LICENSE](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/LICENSE#L1-L21)。
- 本次只读取公开源代码与 registry；GitHub REST API 匿名额度耗尽后改用 `git ls-remote`，未读取本机 Pi/模型凭据，未调用任何模型，未安装或运行 npm 生命周期脚本。本地只读证据下载在 `build-logs/pi-upstream-research/`；GitHub codeload 源归档 SHA256 为 `24867a3b2b203a65bc88d09afb20c7aa2db943159cf9d5add1d6e449bebe88ad`，它不是发布页的 sourcepack 校验值。

## 2. 当前能力与本地旧文档的重要差异

**官方事实**：Pi 现已内置 **MCP + Codemode**。Codemode 让模型在 JavaScript 沙箱内组合工具调用，和 Python 分析环境是两个独立能力；Pi 不内置产品级权限确认流程。依据：[官网](https://pi.dev/)、[官方 README 的权限说明](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/README.md#L89-L100)。

**对本项目的推断**：`pi/README.md` 与 ADR 0122 中“Pi 故意不提供 MCP”是历史依据，后续实施需更新。保留现有外部 Pi 扩展兼容性有意义，但它启动独立 `exp-rs --mcp` 进程的形态，不应直接用作 GUI 内 Agent 的默认执行入口。产品宿主需要保持任务、审批、取消及资产提交的单一权威。

## 3. 三种可选运行层

| 方式 | 官方能力 | 对本项目的判断 |
|---|---|---|
| `pi-agent-core` + `pi-ai` | 工具循环、streamFn、状态、事件、上下文转换、before/after tool hook、AbortSignal；core 本身不带 coding tools | 最小控制面，便于严格白名单，但会话树、压缩策略和持久化需自行接线；不是本次“由 Pi 提供完整通用 harness”的默认推荐 |
| `pi-coding-agent` SDK | `createAgentSession`、ModelRuntime、会话树、压缩、重试、资源加载和扩展系统；可覆盖各默认组件 | **建议的候选**：专用 Node sidecar 内嵌 SDK，关闭发现/附加能力，仅代理宿主工具；须先解决下文 AuthContext 的 P0 缺口 |
| 官方 CLI RPC | 长驻 stdin/stdout JSONL 命令/响应/事件，含 prompt、abort、模型、会话、compact 等 | C++ 对接简便，但 CLI 保留 bash 等管理命令及输入展开语义；自定义反向宿主工具仍需扩展/自定义进程协议；难以比受控 SDK 更小 |

依据：[core AgentOptions](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/agent/src/agent.ts#L113-L160)、[SDK 文档](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/docs/sdk.md)、[CLI integration](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/docs/cli-integration.md#L41-L85)、[RPC 命令](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/docs/rpc-commands.md)。SDK 运行在 Node 子进程仍然具有进程隔离；“SDK”不意味着要把 Node 嵌入 C++ 进程。

`pi-durable` 是独立的持久化 conversation/task/document 产品。**1.5.2 coding-agent 的依赖并不包含 pi-durable**；不应为使用 SDK 顺带引入另一套 durable task scheduler。依据：[coding-agent dependencies](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/package.json#L43-L65)、[durable package](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/durable/package.json)。

## 4. 工具、MCP 与 Codemode 的实际控制点

**官方事实**：`createAgentSession({ tools })` 在 1.5.2 接收 **字符串工具名/模式数组**，不是旧版 Tool 对象数组；自定义工具走 `customTools: ToolDefinition[]`。默认工具含 `read/bash/edit/write`。`noTools: "all"` 使默认选择为空，`noTools: "builtin"` 仅压制默认内置工具。依据：[sdk.ts 48–104](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/sdk.ts#L48-L104)。

**注意**：1.5.2 的 `codemode.mode` 只有 `on`/`only`，没有 `off`；禁用应通过不注册扩展/工具实现，不能写不存在的设置。依据：[Codemode schema](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/settings-schema.ts#L148-L159)。

**关键例外**：非空 `tools` allowlist 不保证自动去除全部 MCP 工具：未显式包含 `mcp__` 模式时，MCP 工具可能仍为 Codemode/tool_search 注册。`excludeTools` 在 allowlist 后生效，包含 MCP。工具注册与可直接声明是两个概念。依据：[sdk.ts 73–92](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/sdk.ts#L73-L92)、[agent-session.ts 1563–1651](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/agent-session.ts#L1563-L1651)。

**项目建议**：自定义空 ResourceLoader，不载入 MCP/Codemode 扩展；显式 `tools: [宿主代理工具名...]`、`customTools: [...]`，同时 deny `mcp__* / codemode / tool_search / read / bash / powershell / edit / write / grep / find / ls`，启动后核对全部注册/可调用工具集合。不要仅配置 `--no-tools` 或“不给模型 bash 提示”。本轮 Shell 后续再加的约束也意味着不能旁路暴露 Pi 的 PowerShell、Codemode 或 CLI bash 命令。

core `AgentTool.execute(toolCallId, params, signal, onUpdate)` 支持结构化结果、更新、错误和顺序/并行模式。信号只提供合作取消：宿主代理必须将它传成真实 native task cancel，并等任务确认终止；不能把流式输出结束当成计算已取消。依据：[AgentTool 定义](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/agent/src/types.ts#L468-L511)。

## 5. 发现、凭据和落盘：可用 API 与 P0 缺口

| 控制目标 | 1.5.2 可核实接口 | 结论 |
|---|---|---|
| 设置不读取用户/项目配置 | `SettingsManager.inMemory(settings)` | 公开 API，源码明确 no file I/O |
| 会话不隐式写 `~/.pi` | `SessionManager.inMemory(cwd, options?, entries?)` | 公开 API，可从宿主存储的 entries 恢复 |
| 凭据仅进程内 | `new InMemoryCredentialStore()`，传入 `ModelRuntime.create({credentials})` | 公开 `pi-ai` API；**AuthStorage.inMemory 虽在内部源码存在，但顶层 SDK 未导出，勿在方案中作为公开调用** |
| models.json/cache 不落盘 | `ModelRuntime.create({modelsPath:null, modelsStore:new InMemoryModelsStore(), allowModelNetwork:false, refreshOnCreate:false})` | 公开 API；`PI_OFFLINE=1` 限制后续模型目录网络刷新 |
| 禁扩展/技能/提示/上下文文件发现 | 自定义 `ResourceLoader`，全部资源返回空列表，指定系统提示，reload/extendResources no-op | 公开接口，有官方 full-control 示例；无需把用户目录当资源源 |
| 默认 loader 的禁用项 | `noExtensions/noSkills/noPromptTemplates/noThemes/noContextFiles`、`disabledBuiltinExtensions` | 有公开 flag，但不足以单独证明不发现 SYSTEM.md/追加提示文件；本项目宜完全替换 loader |
| 禁 slash/skill/template 执行 | `session.prompt(text,{expandPromptTemplates:false})` | 同时跳过 extension commands、skill commands 与模板展开；每个 prompt 入口都要传 |
| 完全禁止 ambient auth 探测 | **ModelRuntime 公开选项缺少 AuthContext/内置 provider allowlist** | **P0：原样 SDK 无法仅靠上述设置保证此目标** |

依据：[settings-manager.ts 358–364](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/settings-manager.ts#L358-L364)、[session-manager.ts 1814–1817](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/session-manager.ts#L1814-L1817)、[pi-ai exports](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/index.ts#L18-L32)、[ModelRuntime options/create](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L110-L125)、[full-control 官方示例](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/examples/sdk/12-full-control.ts#L32-L69)、[loader options](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/resource-loader.ts#L303-L342)、[仍发现系统提示](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/resource-loader.ts#L677-L703)、[prompt 展开开关](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/agent-session.ts#L2162-L2224)。

**P0 的可追溯原因**：ModelRuntime 始终构造 builtin providers，内部 `createModels({credentials,modelsStore})` 没有透传 AuthContext；注册自定义 provider 后又自动执行 `refresh({allowNetwork:false})`。全 provider availability/auth check 可走 ambient auth，Google Vertex 会检查 `~/.config/gcloud/application_default_credentials.json` 的存在。空 CredentialStore 和清理环境变量无法单独阻止此文件存在探测。依据：[ModelRuntime 构造/初始化](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L200-L264)、[全 provider availability](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L343-L361)、[registerProvider 触发 refresh](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L965-L985)、[ambient fallback](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/auth/resolve.ts#L68-L90)、[Vertex 探测](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/providers/google-vertex.ts#L64-L90)。

**项目推断/建议修补方向，尚未实现**：Pi `createModels` 已公开支持 `authContext`。vendor 层可保留一个小补丁，让 CreateModelRuntimeOptions 透传 `{env:async()=>undefined,fileExists:async()=>false}`，并按需增加 builtin provider allowlist。该补丁必须带独立 diff、锁 SHA、升级验证和无外部 credential 路径访问的断言；不能使用私有字段 cast 或 monkey-patch 绕过。依据：[createModels options](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/models.ts#L384-L390)、[默认 AuthContext 的真实 fs.access](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/auth/context.ts#L19-L45)。

## 6. 隐含网络、重试、压缩和运行结束

**官方事实**：ModelRuntime 创建时只有显式 `allowModelNetwork:true` 才进行网络模型目录刷新；后续 `runtime.refresh()` 默认可能允许网络，`PI_OFFLINE` 会抑制它。SDK 使用内置 provider 和模型目录不是“自动向所有 provider 发 LLM 请求”。依据：[create 逻辑](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L218-L264)、[refresh 默认](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L885-L900)。

**默认值需覆盖**：压缩开启；agent-level retry 开启、最多 3 次；install attribution 开启；analytics 关闭；Codemode 为 on；cache warming 为 idle。尤其 cache warmer 会重发最多 1 token 的请求来保持缓存，不能默认带进产品。建议显式 `cacheWarming:"off"`、`enableInstallTelemetry:false`、`enableAnalytics:false`，保留可视化的有限 Pi 重试/压缩预算。`PI_TELEMETRY=0` 防止环境覆盖 attribution 配置。依据：[settings-defaults.ts](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/settings-defaults.ts#L4-L61)、[cache warmer](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/cache-warmer.ts#L88-L123)、[telemetry 开关](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/telemetry.ts#L8-L13)、[身份头](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/client-identity.ts#L6-L16)。这些设置不等价于操作系统沙箱。

**两层重试**：SDK 支持 `retry.provider.maxRetries` 与 agent `retry.maxRetries`。OpenAI SDK 请求本身设置 `maxRetries:0`，Pi 自己用可取消的 wrapper 控制 provider retry，默认 wrapper 为 0；agent 默认 3。项目应显式 `retry.provider.maxRetries:0`，只保留一处可见的 agent 预算，避免 native/sidecar 再叠加重复调用。依据：[SDK request options](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/sdk.ts#L339-L353)、[retry schema](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/settings-schema.ts#L44-L66)、[provider retry](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/utils/provider-retry.ts#L105-L128)、[OpenAI 请求](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/api/openai-completions.ts#L378-L395)。

**结束/取消语义**：coding-agent `agent_end` 带 `willRetry`，后面可能继续自动压缩、恢复或排队任务；`agent_settled` 才表示本轮自动工作收束，并带 `aborted`。`session.abort()` 同时 abort retry/compaction/branch summary/core run，然后 await idle。产品应在 `agent_settled` 后结合 native TaskCenter 任务终态和资产 commit 结果决定最终 UI 状态；RPC prompt success 仅表示已接受。依据：[事件类型](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/agent-session.ts#L209-L225)、[abort](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/agent-session.ts#L2598-L2619)、[RPC acceptance](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/docs/cli-integration.md#L43-L76)。core 没有同名 agent_settled，core 的 agent_end listener 也仍可能被 await；不要把两层事件混为一谈。依据：[core event 注释](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/agent/src/types.ts#L521-L531)。

**上下文/存储**：SDK 提供自动/手动压缩与会话树；inMemory 可由外部 entries 重建。项目可让 Pi 负责通用 transcript/compaction，宿主负责存储版本、科学工作流状态、审批、task/provenance/asset ID。禁止恢复时盲目重放有副作用工具。使用 core 时须自己补齐这些接线和压缩恢复测试，这正是本轮优先 SDK 的原因。依据：[SDK session lifecycle](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/docs/sdk.md#L19-L55)、[manual compact](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/agent-session.ts#L2910-L2925)、[session entries](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/session-manager.ts#L1526-L1534)。

## 7. StepFun step-5-preview

**官方事实**：`ModelRuntime.registerProvider(name,config)` 支持 `baseUrl/api/models/authHeader/headers`；chat model 可设 contextWindow/maxTokens/reasoning/compat。OpenAI Chat Completions 路由名为 **`openai-completions`**，其 provider 实现使用 OpenAI SDK 的 `baseURL:model.baseUrl`。依据：[provider config](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/provider-composer.ts#L57-L110)、[注册](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/src/core/model-runtime.ts#L965-L985)、[OpenAI 客户端](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/src/api/openai-completions.ts#L774-L790)。StepFun 自己公布了 Step 5 Preview 的 agentic 能力与长上下文：[官方模型说明](https://www.stepfun.com/step-5-preview/)。

**项目配置推断，未调用真实 API 验证**：用独立 provider id（例如 `exp-rs-stepfun`）、`api:"openai-completions"`、用户给定 base `https://api.stepfun.com/step_plan/v1`、model id `step-5-preview`；不把 `/chat/completions` 填入 baseURL。密钥从宿主安全通道送入进程内 CredentialStore，避免 models.json、环境、命令行和日志。contextWindow/maxTokens、reasoning_content、开发者角色、流式 usage、tool-call 参数、工具结果格式和中断必须在实施中做契约测试和显式授权的 live smoke；不能仅凭“OpenAI 兼容”宣布通过。Step Plan 特定端点沿用用户提供值，本研究没有使用真实密钥，也没有核实其账户配额或部署能力。

## 8. Windows vendor 与依赖闭包

**官方事实**：npm 直接安装不固定传递依赖，官方 installer 固定依赖；README 支持 `--ignore-scripts`。发布页提供 SHA256SUMS 覆盖的 sourcepack，含发布模型数据和 native prebuilds；普通 GitHub codeload 源归档不等价于它。依据：[安装要求](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/README.md#L37-L40)、[sourcepack](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/README.md#L155-L167)。

`coding-agent` 的直接运行依赖包含 pi-ai/core/tui/mcp/codemode（schema 经 core/ai 引入）、`quickjs-wasi`、`@silvia-odwyer/photon-node` 等；它不是单个 TypeScript 文件或零依赖 SDK。`pi-ai` 还有 OpenAI、Anthropic、Google、AWS SDK 等。core 自身仅依赖 pi-ai/schema，但 **core + pi-ai** 仍不等于“仅 OpenAI SDK”体积。依据：[coding-agent package](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/package.json#L43-L65)、[pi-ai dependencies](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/ai/package.json#L77-L88)、[core dependencies](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/agent/package.json#L25-L28)。

`photon-node` 使用 WASM 资产，不能因名字带 node 就宣称是必须编译的 `.node`；官方 build 明确复制 `photon_rs_bg.wasm`。`pi-tui` 确有 Windows x64/arm64 `.node` 平台辅助模块，加载失败可以返回 undefined。官方 CLI bundler 另把可选 `bufferutil/utf-8-validate/kerberos` 作为 external。SDK 路径与 CLI bundle 不同，不能只复制 `dist/bundle/cli.js` 便认为 SDK 闭包完整。依据：[资产复制](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/coding-agent/package.json#L35-L36)、[tui 预编译资产](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/tui/package.json#L17-L31)、[native helper 查找/降级](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/packages/tui/src/native-platform.ts#L28-L58)、[bundler external 列表](https://github.com/earendil-works/pi/blob/b4afdddf44e059c67ccf5097e5317d913521b470/scripts/build-coding-agent-bundle.mjs#L18-L28)。

**项目建议**：Windows 包同时 vendor 固定 Node runtime、受限 sidecar bundle、精确 production lock/integrity、需要的 `.wasm/.node`/模型快照/许可证，而非要求用户安装 Node 或联网 npm。依赖采集阶段禁生命周期脚本，分开审核必须构建的资产，生成 manifest/SBOM/SHA256；不要把整个 monorepo devDependencies/native 编译器一起装进产品，也不要在首版未经验证 tree-shake 掉声明依赖。当前只做依赖清单和源码检查，尚未实际安装/审计完整 Windows production 闭包；其干净机器离线启动、import smoke、无隐式下载和 no-system-Node/Python 测试属于实施的 P0 验收，不能将本研究当作打包已通过。

## 9. 给实施方案的结论

推荐 **Pi 1.5.2 coding-agent SDK + 固定 Node sidecar + 宿主唯一工具代理**，以 Pi 提供完整通用 loop/上下文能力；不要新增 pi-durable 调度器。先完成 ModelRuntime AuthContext 透传的受控 vendor 补丁及隔离验证，再做产品切换。若团队不接受维护此小补丁，需重新决策为 core + 显式 createModels/authContext，并把自行接入压缩、恢复和 transcript lifecycle 的成本写入范围，不能默默降成只有工具循环的“完整 Pi”。

验收至少覆盖：工具集合无旁路、用户目录/credential 无发现、无自动模型目录网络/cache warming/遥测、StepFun 流式工具闭环、长上下文压缩后工具继续、每个取消边界、sidecar 崩溃后的 native task reconciliation、Pi settled 与资产提交的最终一致性、Windows 离线闭包。这些是针对本项目边界的设计建议，尚未由本研究执行。
