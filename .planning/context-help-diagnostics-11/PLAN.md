# PLAN — F20 Context Help, Diagnostics & UX Guidance 11.0

事实基线见 BASELINE.md / CURRENT_ARCHITECTURE.md；缺口清单见 BASELINE.md「Phase 0 发现的缺口」。
原则：不重复造轮子。master 已有成熟的 help authority（HelpId/Registry/ContentStore/providers/coverage gates/CLI/MCP/GUI surface），本 track 的价值在于**把已有 gate 补到真实全量、把已有 authority 接到 GUI 错误路径、把 i18n 契约补上验证、把生成物接上 zero-diff、把可访问性从 tooltip 单通道升级为可编程读取通道**。

## Package ↔ 缺口 ↔ 交付映射

| WP | 消费的缺口 | 交付 |
|---|---|---|
| A census | 缺口 1、2、3、10 | 统一命令 census 扫描器（消除 test_help_coverage 与 test_command_contract_9 双扫描器漂移，扩前缀/idiom 盲区）；补 10 命令知识页；补 7 harness 码诊断页；SicnuDialogHelp↔HelpId 关联契约（最小） |
| B resolver | 缺口 9、11 | `ContextualHelpResolver`（sicnu_help 纯逻辑）：输入 surface id + ContextFacts + availability → 返回 short tip / detail helpId / next step；AvailabilityFacts 全命令覆盖（由 ContextRules 派生而非静态 24 条表），中文 label |
| C disabled reason | 缺口 9、12 | disabled reason 机器可读化：facts→结构化 reason（code+params+text），全部禁用命令必有 reason（census gate）；GUI tooltip 与 accessible 通道同源 |
| D error diagnostics | 缺口 4、5 | 修 retry 解析（未知值 fail-visible 而非静默）；`DiagnosticCatalog` 接入 GUI 错误呈现（任务失败/消息栏经由 1–2 个接线点显示 code→帮助入口）；harness 码页补全 |
| E i18n pipeline | 缺口 7、8 | NOOP 上下文入 ts 的 drift gate（扫描源码 QT_TRANSLATE_NOOP 上下文 ↔ ts name= 对比）；presenter/topic_text 骨架文案 tr() 化（英文源文本+zh_CN ts 翻译，符合 repo 既有管线）；lupdate 扫描范围与 gate 一致化 |
| F accessibility | 缺口 12 | 禁用原因写入 accessibleDescription/accessibleText（与 tooltip 同源）；a11y 契约测试（tooltip 不承载唯一信息的机器断言：凡禁用 action 必有 accessible 描述=reason） |
| G generated reference | 缺口 6 | zero-diff gate：测试内重新生成 5 页 markdown 与 docs/generated/help/ 逐字节 diff；漂移时报差异文件+首行 |
| H UX eval corpus | 全部 | `tests/test_ux_guidance_corpus.cpp`：场景表（错误码/禁用/空数据/离线/模型缺失）→ 断言 resolve() 产出（非空、中文、含下一步动作、无 secret 模式） |

## Phase 计划（对应 commit）

- **P1（契约/authority 层）**：census 扫描器统一 + commands.json/workbenches 知识补全（A 的数据面）；retry 解析修复（D 数据面）。commit `feat(help): unify command census oracle...`
- **P2（resolver + availability 全量）**：ContextualHelpResolver + AvailabilityFacts 从 ContextRules 派生全覆盖 + 中文 label（B、C 逻辑面）。commit
- **P3（error diagnostics）**：harness 码页补全 + DiagnosticCatalog GUI 接线（D）。commit
- **P4（surface）**：help_system_controller / ribbon / palette 的 reason 通道同源化；CLI/agent 暴露 resolver 查询（若低成本）（B/C/D surface）。commit
- **P5（i18n + a11y）**：E + F。commit
- **P6（generated + corpus + E2E）**：G zero-diff + H corpus + 全部 targeted suite 双跑。commit
- **P7（review）**：subagent #2 对抗 review，P0/P1 修复。commit
- **P8（终验）**：rebase、双验证、push、PR。

## 测试策略

- 现有 8 个 help/i18n 测试为回归底线；凡 master 上已红的先在 BASELINE 记录、本 track 修复为绿；禁止为绿删断言。
- 新增 oracle 与被测实现独立：census 扫描器直接源码正则+运行期注册表双源；zero-diff 读仓库文件 vs 运行期重生成；corpus 用场景→期望断言（不调用被测 render 自证）。
- 全部测试 `QT_QPA_PLATFORM=offscreen`、`ctest -j1` 或直接跑二进制。

## 资源

build -j2（RSS/负载监控降 -j1），test -j1；冷构建预计较长，仅构建 8+新增 test target，不全量构建。
