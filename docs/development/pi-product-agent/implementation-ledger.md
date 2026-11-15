# Pi 产品 Agent 实施记录

基线：`8d36bfd243476a445a89551d4fa18b5f7a0b5e9f`。实施方案：`docs/superpowers/plans/2026-11-15-vendored-pi-product-agent.md`。

用户已授权按方案实施。按 P0 → P5 顺序推进，阶段通过后继续，不把仅能回答文本或独立 MCP 产物当作产品闭环完成。

| 阶段 | 当前状态 | 验证要求 |
| --- | --- | --- |
| P0 固定 SDK、认证隔离与兼容验证 | 已通过并合并 | 固定依赖、认证透传 red/green、受控模型/工具/取消/会话、本地服务；不使用真实密钥 |
| P1 随包 Node/Pi、私有协议和 CMake | 已验证并合并 | 严格 receipt、离线准备、安装/搬迁、损坏负例、进程生命周期 |
| P2 当前 Qt 宿主工具与对话接口 | 本地验证通过，待 PR 合并 | 同一 GUI Task Center/DataManager、审批与取消、无重复执行 |
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

预算分工：P1 的 `executionMs` 仅限制 Node/模型活跃时间，等待任意宿主 `tool.result` 时暂停（包括审批和执行）。它不等于 P0 预算表中完整产品运行的 15 分钟上限。P2 当前 Qt 工具宿主须对整个运行另计时，仅暂停用户审批等待，超时后取消并等待其拥有的原生任务/提交终态；本项仍待 P2 实现，不把 P1 的工具等待当作无限制的产品执行许可。

完整 Windows staging 纳入 `scripts/stage_windows_application.ps1`，从配置的依赖位置部署 DLL、Qt 与翻译资源，保留原 Python/Pi receipts，并检查 Windows x64 `.node` 的 PE 依赖。Node 官方 exe 与当前 Windows x64 native prebuild 的实际 import 表已检查，仅导入 Windows 系统 DLL；其它平台资源仍完整保留并由 hash 约束。归档改用随包 Python 标准库，逐项读取 manifest 而不依赖 PowerShell 对隐藏文件/长路径的归档行为。正式 P1 构建/安装/搬迁结论待验证后追加。

#### 已完成的 P1 验证

- 最终受控 Node/SDK 套件 22/22 通过、0 跳过（`pi-p1-sdk-installed-final-tests.log`）：包含实际 SDK 自动压缩、压缩只调用 `result()` 的活动计时、全部模型/工具预算和已安装入口真实 JSONL 工具循环。修复了压缩事件名及 result-only 流活动监测；不二次消费 SSE。
- 原生两个目标构建通过。完整 `test_pi_process` 8 用例 / 720 断言通过（`pi-p1-native-process-final-tests.log`），包含 15K 文件完整安装 SDK 的生产校验、Qt 启动、Job 约束、ready 与 closed，无模型请求。进一步收紧相对路径和关闭超时 oracle 后，两个受影响用例 / 71 断言通过（`pi-p1-native-oracle-tests.log`）。原副驾驶回归 13 用例 / 50 断言通过（`pi-p1-native-copilot-regression.log`）。
- Windows 测试夹具用 `resolve(m.cwd)` 比较 Node/Qt 的路径，避免把反斜杠与正斜杠差异误判为环境泄漏；使用产品已有的 120 秒冷启动预算。曾观察一次 10 秒夹具超时，实际成功握手测得 7,669 ms；不把测试专用的更短时限当作产品性能要求。所有临时 DEBUG 记录已从源码移除，保留失败时的脱敏诊断。
- 实际生产 CMake 模块 configure 和独立 PiRuntime/PiProduct 组合安装通过。旧 wrapper 在源码变化后被 configure/install 拒绝（`pi-p1-cmake-v2-stale-negative.log`、`pi-p1-installed-v3-stale-negative.log`）；不是使用 stale 载荷继续发布。
- 实际 CPack 成功（`pi-p1-cpack.log`）：组件验证 ZIP 70,966,977 字节，SHA-256 `703476b2d729c538c72af8dc1facced3b38f5c5946428a8c7f136c0840bc682e`。该 NONE-language CMake fixture 产生的文件名为 `sicnu-pi-runtime-1.5.2-win32.zip`；实际载荷目标仍是已核验的 Windows x64，文件名不充当架构证明。这不是完整 GUI 应用发布包。
- ZIP 完整解压至仓库外带空格/中文的新目录；临时移走选定的原 SDK/wrapper stages 后，全量组合 receipt 校验、包内 Node/SDK initialize/shutdown 以及本地模型驱动的私有工具循环均通过（`pi-p1-outside-*.log`，工具循环 1/1、0 跳过）。源 stages 已恢复，记录 `pi-p1-relocation.json`。子进程 PATH 为空或含会报错的替代命令，不需要全局 Node/npm/Python。此处“离线”证明依赖与本地 fixture 可用；没有宣称真实 StepFun 可断网推理。

最终退出记录：纯打包测试 14 + 10 + 2 项全部通过（`pi-p1-final-*-tests.log`）。在成功 configure/CPack 之后，分别破坏自有 wrapper stage 的入口、SDK stage 的 WASM，实际 `cmake --install` 均以 integrity 原因拒绝，目标前缀未写入任何文件；finally 已核对恢复原字节（`pi-p1-install-corruption-report.json`）。独立 Standards 和 Spec 审查覆盖源码至 `03c44b4d8`，均为 0 未关闭项。P1 可以提交 PR；P2–P5 保持待办。

[PR #2067](https://github.com/WindWang2/exp-rs/pull/2067) 首轮 Windows CI 在英文系统 CP1252 控制台发现中文归档路径的成功提示编码失败（SDK 14 / product 10 已过）。三个 CLI 入口现显式输出 UTF-8，保留 library import 无副作用；真实 CP1252 子进程回归从 3 处失败转为 archive/CLI 4 项通过，既有入口负例亦通过（`pi-p1-cli-encoding-{red,green}.log`）。CI 还在搬迁并隐藏源 stages 后显式运行真实安装入口工具循环，避免将前阶段的可选 skip 误算为已完成该检查。以上未更改任何 SDK/wrapper payload；等待新 head 的远端状态确认后按已授权的本地验证条件合并。

P1 已按已授权的本地验证条件合并为 `85ce917bea0ece4df6548a677b97a72ecde8659b`，本地 master 已同步。[Pi Windows CI](https://github.com/WindWang2/exp-rs/actions/runs/43711268840) 随后完成：SDK/协议、真实 CMake/CPack/搬迁、Node 出站防火墙阻断的启动探针全部通过；这不是其它平台流水线全部通过的声明。

### P2

基于上述 master 实现两个对话运行时和当前 Qt 工具宿主。Native 实现迁出 Dock 的历史与续轮；Pi 实现只适配已校验的私有协议。两个后端共享最终 provider-safe 名称与 canonical ID 的显式目录映射。Dock 保留卡片、设置及视图服务，不持有 Pi 的第二套历史。

宿主复用现有 Dispatcher、WorkflowExecutor、SpatialToolRegistry、Task Center 和 DataManager。Python 单调用仍沿 Dispatcher 的事务提交；当前通用多步流水线不能提供相同 Python staging 保证，因此包含 `python:run_script` 的 plan 明确拒绝并要求独立提交，不能经 generic pipeline 绕过审批/提交。P2 初期仍默认 Native；Pi 通过明确的后端选择启用。会话恢复、双语可见验证和真实 StepFun 尚待 P3/P4，不提前声称完成。

P2 迁移补充（尚待测试退出）：宿主使用 ExecutionPlane 的提交入口和空的第二完成 watcher，仅在自己的原生 Job/Task 终态对账后调用既有事务 payload builder，避免默认 watcher 先发布。任务历史可清除，宿主仍保留已观察的真实快照并检查 JobEngine；idle run 账本回收，最近 4096 个 run ID 保留为防重用记录。

Python 审批在后台 capture/recheck，绑定已冻结的 inline 源码、原 script_path/canonical 引用和脚本 SHA、bundled runtime/lock/environment/location 身份。批准后再次核对；worker 在创建环境根或执行脚本前以 expected identity 校验实际使用的环境。模型传入的 expected 字段由宿主实际探测值覆盖，不构成授权。等待审批暂停整个运行时钟，后台探测与原生执行计入 15 分钟预算。

New/Open/Exit 的既有中断确认现在也识别模型流/待审批（即使 Task Center 尚无任务），批准中断后有界等待 agent 自己的模型与原生双终态，超时留在当前工程。QgsProject cleared 另外撤销旧审批和运行；publication handler 绑定可撤销的工程上下文，原窗口上下文失效后不能向替换工程发布输出。

`run_workflow` 复用原生 meta 定义，目前支持 `pipeline` 对象单字段；其余记录/自动加载参数明确拒绝，不静默忽略。Python 多步计划仍明确要求拆成独立脚本调用。发现工具复用原目录 SearchQuery 的过滤/排名，并在当前有界目录内分页。结果超预算时保留有界核心资产/执行/身份引用并说明截断，不传递未界限的大日志或 output 字段。

#### P2 本地退出验证（2026-11-16）

原生构建及主窗口受影响对象编译通过（`pi-p2-native-build-5.log`）。审查后的实际回归发现跨模块静态 JobEngine 查询会提前释放宿主租约，现改为经 TaskCenter 的 virtual 查询回到拥有模块；清除任务历史后仍须实际 worker 退出才能完成。旧 UI fixture 则用公开注册表接口隔离并恢复模拟工具，不放宽生产目录准入。Python probe 保留 30 秒绝对期限，子进程粗粒度期限改为向上取整；增加显式开关的固定阶段/数值诊断。GUI 测试等待增加短休眠，失败时保留脱敏卡片 JSON，不再空转等待已结束的轮次。

| 本地检查 | 最终结果 |
| --- | --- |
| 当前 Qt 工具宿主 | 9 用例 / 252 断言，全部通过 |
| Python 审批与 worker 身份 CAS | 7 / 78，全部通过 |
| 原副驾驶 UI 回归 | 13 / 60，全部通过 |
| Native 对话适配器 | 8 / 178，全部通过；上述修复未修改其已通过源码 |
| Pi 对话适配器与实际安装 SDK | 3 / 118，全部通过、0 跳过；上述修复未修改其已通过源码 |
| 未发布 Python staging 清理 | 3 / 211，全部通过；上述修复未修改其已通过源码 |
| Native/Pi × 英文/中文真实 Python GUI NDVI | 四组均通过，1 个生成式 Catch 用例 / 272 断言，0 跳过 |
| 翻译 freshness | 23,839 条，0 unfinished、0 新缺失 |
| 独立 Standards / Spec 及修复增量审查 | 分别 0 未关闭项 |

NDVI 模型由本地 SSE 夹具真实提出工具请求，审批后通过当前 GUI 的 Task Center、Dispatcher 和 DataManager 执行随包 Python，最终回复不是成功 oracle。四组均核对 4×3 全部像元、9 有效/4 变化、Byte/NoData 255、EPSG:32648、原网格、输出 assetId、3 个工程资产及 2 条输入血缘；终态卡片与 run monitor 一致。耗时依次 42.789 / 354.339 / 48.351 / 360.199 秒，Pi 包含完整安装 SDK 校验，不能当作模型首 token 延迟。

可重复原始证据：`build-logs/pi-p2-native-targeted-summary.log`、`pi-p2-test_python_agent_gui_delivery.log`、`pi-p2-ndvi-final-summary.log`、`pi-p2-test_{native_conversation_runtime,pi_conversation_runtime,python_output_discard}-run1.log`、`pi-p2-translation-final.log`、`pi-p2-{standards,spec,increment}-review.md`。P2 的首次整套失败日志另保留为 `*-run1.log`；最终结论没有把首次失败算作通过。真实 StepFun、可见应用验收和最终 Windows 发布仍属 P3–P5，尚未声明通过。
