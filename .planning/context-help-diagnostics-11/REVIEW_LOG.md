# REVIEW LOG — F20 context-help-diagnostics-11

## Review #1（主 agent 全 diff review）

Phase 6 提交前自审：与既有 gate 的兼容性逐条核对（test_help_core 的可用性呈现测试改 3 参聚合初始化；presenter 中文断言不受 tr 决策影响；markdown writer 中文 chrome 保留）。发现并当场修复 4 个编译错误（聚合初始化、resolver 前置声明命名空间、QStringSet、corpus 命名空间限定）。

## Review #2（独立对抗 review，subagent #2，只读，2026-09-16）

范围：`git diff origin/master...HEAD` 全量（3 commits）+ 与 master 源码交叉验证。

结论：**P0=0，P1=1，P2=2，P3=12**。

### P1

- **P1-1 workbench.obia 谓词错位**：审计误将 rasterTool lambda 内的 `d.availability`（command_defs.cpp:474）归给 obia；实际 obia 无谓词、恒可用。我在 requirementFacts 加的 obia raster 特例制造了 facts↔shell 双通道漂移，corpus 把错位固化为断言，DECISIONS/CAPABILITY_MATRIX 含失实声称。
  **处置：全部撤销/更正**（selection_context 特例、coveredCommandIds 行、suggested 行、corpus 翻转为正向"无谓词诚实可用"断言、文档更正）。

### P2

- **P2-1 未知 retry 值注册空壳页**：已修——错误分支清空 d.id，条目真正丢弃。
- **P2-2 withDiagnosticLink 硬编码中文 + 裸 id**：已修——提示模板 tr(英文源) 移至成员函数调用点（WorkflowSessionController 上下文），ts 补 zh 译文。

### P3（12 项）disposition

| # | 内容 | 处置 |
|---|---|---|
| 1 | bridge m_catalog 死成员 + 每次重建 codes | 保留重建（仅失败路径、~470 描述符，成本可接受）；m_catalog 补公开 catalog() 访问器供调用方取 fallback 页 |
| 2 | CamelCase/UPPER_SNAKE 候选与未来短码碰撞 | 记录；当前码表无碰撞，后果限于帮助主题错链 |
| 3 | helpId 属性成功路径残留 | 已修：成功分支清属性 |
| 4 | terminology_provider.h 注释路径漂移 | 已改 |
| 5 | disabledReason 属性只写 | 记录：属性为机器可读契约面（agent/测试/未来面板消费），屏幕阅读器通道由 ribbon accessibleDescription 承担 |
| 6 | layer.* 前缀家族在 facts 通道的行为面 | 记录：与旧 ContextRules 文本一致，registry 路径不变，仅 facts 通道新增行 |
| 7 | test_i18n gate 1 硬编码文件列表；占位符校验对当前数据空真 | 记录：gate 2 的 1:1 对应 oracle 扎实；文件列表为显式契约面（新增 NOOP 文件须登记） |
| 8 | corpus 泄漏扫描未覆盖 keywords | 已加 |
| 9 | JSON 首行缩进/尾换行/agent 行并线 | 已修 |
| 10 | RetrySense::Derived 无运行期派生解析实现 | 记录：master 既有语义，非本 track 引入 |
| 11 | zero-diff gate REGEN 写仓库授权面 | 记录：opt-in 环境变量、默认只读、确定性成立 |
| 12 | TEACHING_REFUSAL 白名单豁免 | 记录：双 gate 白名单闭合 + 理由注释在位 |

## 复验

Review 修复后：重建 9 目标 + 全套件 R3/R4 双跑（见 TEST_MATRIX 终态）。
