# Pi 产品 Agent 实施记录

基线：`8d36bfd243476a445a89551d4fa18b5f7a0b5e9f`。实施方案：`docs/superpowers/plans/2026-11-15-vendored-pi-product-agent.md`。

用户已授权按方案实施。按 P0 → P5 顺序推进，阶段通过后继续，不把仅能回答文本或独立 MCP 产物当作产品闭环完成。

| 阶段 | 当前状态 | 验证要求 |
| --- | --- | --- |
| P0 固定 SDK、认证隔离与兼容验证 | 已通过并合并 | 固定依赖、认证透传 red/green、受控模型/工具/取消/会话、本地服务；不使用真实密钥 |
| P1 随包 Node/Pi、私有协议和 CMake | 实施中 | 严格 receipt、离线准备、安装/搬迁、损坏负例、进程生命周期 |
| P2 当前 Qt 宿主工具与对话接口 | 待 P1 通过 | 同一 GUI Task Center/DataManager、审批与取消、无重复执行 |
| P3 双语 Python NDVI 产品闭环 | 待 P2 通过 | 两语言真实 Python 像元、任务、资产、地图与退出保留 |
| P4 真实模型、分阶段默认 | 待 P3 通过 | 真实模型链路与确定性 fixture 分开报告、资源与成本记录 |
| P5 合并后 master Windows 发布 | 待 P4 通过 | 新包完整性、无系统 Node/npm/Python、可见双语交付 |

## 已固定的准备条件

- Pi SDK 候选 `1.5.2`，上游提交 `b4afdddf44e059c67ccf5097e5317d913521b470`。
- Node Windows x64 `24.22.3` 官方 ZIP 已下载并核对 SHA-256：`e612af99f626ab415ea225af6bce5c971feb448cb5cae250bb1c3fa50c5f7d9f`。
- 编写测试的公开接口已随方案获准：`ModelRuntime.create/registerProvider`、受控会话 prompt/tool/abort、运行时 prepare/verify-stage；不测试私有字段。
- 只由主任务运行 C++ 构建与整套测试；本机构建并行上限 2、测试串行，使用 `resource_guard.py`。
- SDK 准备禁止 npm 生命周期脚本；真实用户 provider、Pi 配置、认证文件和密钥不用于 P0。

## 阶段记录

### P0

#### 决策与实测（2026-11-15）

选择完整 Pi SDK 1.5.2，保留真实会话压缩和生命周期；暂不采用 core + ai，避免在产品中重新实现这些能力。公开 `AuthContext` 透传小补丁已通过原版失败、补丁通过验证：原版在诱饵环境下发现 44 个 OpenAI 模型，受控配置期望 0；补丁阻断 ambient env/file 探测。源码、发行 JS/d.ts、前后 hash 与上游身份一起锁定。默认不传该选项时保持上游行为。补丁的上游提案尚未提交，不计为已上游接受。

生产闭包共 123 个包、15,541 个文件、209,703,676 字节（约 200 MiB），保留 3 个 WASM、6 个 native prebuild 及依赖许可证/资源。该体积是未压缩 Node + SDK payload，不是 Windows ZIP 增量。元数据未发现缺失 license 字段，不代表完成逐包人工许可证审计。

| 实测项目 | 结果与边界 |
| --- | --- |
| Node / SDK | 24.22.3 Windows x64 / Pi 1.5.2，官方归档和 npm integrity 固定 |
| 缓存离线重建 | `--offline` 重建到 `build-logs/Pi Offline Relocation`，成功；不使用 npm 生命周期脚本 |
| 搬迁 receipt + 导出 smoke | 逐文件校验及 SDK/ai 公开导出通过；不创建模型会话。该次进程日志首末时间相差约 419 秒，主要包含 15K 文件的全量校验，不当作模型延迟 |
| 无网络会话初始化 | 3,563.9022 ms；RSS 132,612,096 字节，峰值 RSS 132,620,288 字节，JS heap used 59,714,112 字节；空工具、无 prompt、0 模型请求，单次基线，非长期/冷盘保证 |
| SDK 行为 | 本地 Chat Completions fixture 的真实工具循环、分段参数、结构化结果、usage、拒绝/默认资源隔离、模型/工具取消、真实 SDK 压缩与禁止自动重试；与真实 StepFun/NDVI/GUI 分开记账 |
| 打包负例 | 14 项通过，含 receipt/recipe/目标漂移、SRI、归档路径、patch hash、额外资源、目录联接越界 |
| 原生基线 | `test_agent_copilot_ui` 构建通过，13 个 Catch2 用例 / 50 个断言通过 |

#### P1/P2 将强制的初始预算

以下为本次确定的产品默认值，P0 尚不具备私有进程协议，因此只有 SDK 结果大小（512 KiB）与禁止自动重试已在 P0 执行。其余必须随 P1/P2 的对应接口实施并测试，P4 再记录真实模型数据；不把候选值写成已实现的安全保证。

| 项目 | 初始值 / 计数方式 |
| --- | --- |
| 运行时准备 | 全量 receipt 校验独立异步阶段，15 分钟上限，可取消；SDK ready/握手 120 秒，启动自动重试 0 |
| 协议数据 | 每帧 2 MiB UTF-8；用户 prompt 64 KiB；工具参数 1 MiB；结构化工具结果 512 KiB；stderr 尾部 256 KiB，单行 64 KiB |
| 调用与并发 | 宿主工具串行；待处理上限 16；每工程同时一个可写 agent 会话（不限制用户手动 Task Center 工作） |
| 单轮运行 | 最多 16 次模型 completion、32 次工具尝试（含未知/拒绝调用）；模型空闲 120 秒，运行执行时间 15 分钟，等待用户审批暂停执行计时 |
| token | 初始 context 32,768 / 单次 output 4,096；单轮累计 usage 131,072 / output 16,384，按 SDK usage 统计；不是 StepFun 最大上下文声明 |
| 重试 | SDK session retry 关闭，provider retry 0；未知执行结果先对账，不自动重放宿主写任务 |
| 资源 | Node V8 heap 512 MiB；Windows WorkerProcessGuard 初始 process committed-memory 限额 1 GiB；RSS 与 heap 单独报告，不称为文件/网络沙箱。Python 沿用自己的进程限额，不与 Node 合算 |

产品 wrapper 使用随包 `.mjs` ESM，不要求用户安装 TypeScript 编译器或 jiti。不可变第三方 SDK receipt 与可更新的第一方 wrapper receipt 分开，后者必须绑定前者 hash，避免每次编辑 wrapper 重建依赖闭包。

#### 可重复命令与证据

使用已核验的 Node/Python 绝对路径，先 `scripts/prepare_pi_runtime.py --destination <prefix> --cache-dir <cache> --offline --node-archive <official-zip>`，再 `--smoke <prefix>`。设置 `SICNU_TEST_PI_NODE_MODULES=<prefix>/libexec/sicnu-geo-rs/pi-agent/node_modules` 后，用随包 Node 执行：

```text
node.exe --test --test-concurrency=1 pi/product-runtime/test/auth-context.test.mjs pi/product-runtime/test/controlled-session.test.mjs
node.exe pi/product-runtime/test/session-metrics.mjs
python.exe -I -B -m unittest discover -s tests -p test_pi_runtime_packaging.py -v
```

原始日志：`build-logs/pi-p0-auth-red-recheck.log`（有效失败）、`pi-p0-sdk-final-tests.log`、`pi-p0-packaging-final-tests.log`、`pi-p0-offline-rebuild.log`、`pi-p0-relocated-smoke.log`、`pi-p0-metrics.json` 与 `pi-p0-native-baseline-*.log`。它们不含真实模型密钥、用户 profile 或真实模型请求。正式测试结论和 PR 在复核通过后追加；尚未声明 Qt 接入、Python NDVI、真实 StepFun 或最终 Windows 发布通过。

P0 退出：SDK 8/8、打包 14/14、原生 13 用例/50 断言、离线重建与搬迁 smoke 均通过。独立 Standards 0 项；Spec 提出的 usage 和资源/预算记录两项已关闭，最终 0 项。可重复 metrics 探针第二次为 4,632.0068 ms / RSS 126,291,968 字节（日志 `pi-p0-metrics-repro.json`），不取两次最低值作性能保证。

[PR #2066](https://github.com/WindWang2/exp-rs/pull/2066) 已合并为 `c28bbb17c183281f0be07e915844477943e709dd`，本地 master 已同步。按用户已确认的本地验证合并条件执行；合并前远端快照：两项 Python 打包和四项平台 CI 运行中，macOS Python 打包已取消，未称全部远端通过。

### P1

实现中：不可变 SDK 与三文件 product wrapper 分开准备/安装。Wrapper receipt 绑定 SDK recipe digest 及 SDK receipt 原字节 SHA-256；Qt 启动前异步全量校验，编译时绑定预期 vendor recipe。私有 JSONL 只开放 initialize、prompt、tool.result、cancel、shutdown；schema、会话/generation/run/call、序号、大小和预算在两侧验证。此阶段尚未接入 Dock，不声称已在产品 GUI 运行。

完整 Windows staging 纳入 `scripts/stage_windows_application.ps1`，从配置的依赖位置部署 DLL、Qt 与翻译资源，保留原 Python/Pi receipts，并检查 Windows x64 `.node` 的 PE 依赖。Node 官方 exe 与当前 Windows x64 native prebuild 的实际 import 表已检查，仅导入 Windows 系统 DLL；其它平台资源仍完整保留并由 hash 约束。归档改用随包 Python 标准库，逐项读取 manifest 而不依赖 PowerShell 对隐藏文件/长路径的归档行为。正式 P1 构建/安装/搬迁结论待验证后追加。
