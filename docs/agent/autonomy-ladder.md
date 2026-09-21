# Teaching Autonomy Ladder — AI 助教自治等级（L0–L5）

同一套核心能力上的**自治阶梯**：课程 / LabSpec / 教师 / 会话可以在实际
tool/action 之前逐级控制 AI 替学生做多少——从 L0 完全不帮到 L5 全自动执行。
控制是**代码级 gate**，不是 UI 隐藏；agent（科研）域可显式选择 L5，但科学
验证始终保留。设计见 ADR 0172。

## The ladder

| Level | Capability | What the assistant may do |
| --- | --- | --- |
| L0 | *(no assistance)* | 保持沉默；只允许只读查询 |
| L1 | `concept_hint` | 讲解这一步背后的概念 |
| L2 | `error_localization` | 说出结果为什么异常 + 如何验证 |
| L3 | `next_step_recommendation` | 建议下一步（仍由学生动手） |
| L4 | `plan_generation` | 起草完整方案（由学生运行） |
| L5 | `autonomous_execution` | 执行工作（科学验证强制保留） |

阶梯顺序即比较顺序；能力是最小等级的门槛。`read_only_query`（L0）是查询类
tool/action 的能力投影——查看数据始终允许。

## The policy (`sicnu.autonomy-policy/1`)

```json
{
  "schema": "sicnu.autonomy-policy/1",
  "level": "L2",
  "mode": "exam",
  "max_level": "L4",
  "capability_overrides": {
    "concept_hint": { "decision": "allow", "reason_code": "AUTONOMY_ALLOWED" }
  },
  "source": "course"
}
```

严格解析：未知 schema 版本 / level / mode / source / capability / decision
一律 typed error，绝不静默应用；policy 永远不会被半应用。

**Precedence:** `course < labspec < teacher < session`。按字段取最高优先
级来源的声明；`max_level` 取最紧的上限（任何来源都不能放松别人的上限）；
未知来源被忽略（无法授予任何东西）。合并规则只有一处：
`resolveEffectivePolicy`。

**Modes:** `exam`（上限 L2）/ `practice`（上限 L4）/ `instructor`（L5）/
`agent`（显式 L5 opt-in，无模式上限，验证强制）。

## The gates (before the action, not in the UI)

1. **Assistance gate** — `labAsk()`（`lab_copilot.cpp`）：intent → capability →
   decide；deny ⇒ 带类型码的 refusal envelope；downgrade ⇒ 用降级后的能力
   作答，result 携带 `autonomy` 块。
2. **Execution gate** — `ExecutePlanTool::execute()`（`plan_tools.cpp`），在
   preflight/compile/submit **之前**：这是爆炸半径最大的单点（一次调用 = N 个
   变更），lab routed_tool、MCP `tools/call`、CLI 都经过它。
3. **Status surface** — `harness:autonomy_status`：只读投影
   （`sicnu.autonomy-status/1`），UI 与未来 agent 的 machine-readable 表面。

会话上下文（role/domain/policy）由**宿主注入**——tool input schema 与
`role` 一样刻意省略这些字段，composing model 永远没有机会声称权限。session
层能提升等级，属于特权层：只有同时携带宿主注入的教师凭据
（`SICNU_LAB_TEACHER_TOKEN`，与 teacher 表面同一道门）时才生效；自带
`autonomy` 块而没有凭据的注入被忽略（fail-safe：会话保持课程/LabSpec 策略）。
限制性配置放在 course/labspec 层，本就不需要 session 块。

## Structural rules (no override can lift them)

- lab 域 + student 角色 + `autonomous_execution` ⇒ deny
  `AUTONOMY_LAB_STUDENT_EXECUTION`（ADR 0155 的教学约束在自治轴上的重述）。
- 非 lab 域的 L5 执行要求 `mode=agent`（显式 opt-in）⇒ 否则
  `AUTONOMY_AGENT_MODE_REQUIRED`；允许时 `verification_required=true`
  （OutputVerifier / harness_verification 不变）。
- 未知能力 ⇒ deny `AUTONOMY_UNKNOWN_CAPABILITY`。
- 辅助能力超等级 ⇒ **降级**到已解锁的最高能力；执行能力超等级 ⇒ **拒绝**
  （永不静默替换）；L0 ⇒ 拒绝（不降级——"只读回答"也是帮助）。

## Reason codes (closed)

`AUTONOMY_ALLOWED` · `AUTONOMY_UNKNOWN_CAPABILITY` ·
`AUTONOMY_LEVEL_TOO_LOW` · `AUTONOMY_DOWNGRADED` · `AUTONOMY_MODE_CEILING`
· `AUTONOMY_COURSE_CAP` · `AUTONOMY_OVERRIDE_DENIED` ·
`AUTONOMY_LAB_STUDENT_EXECUTION` · `AUTONOMY_AGENT_MODE_REQUIRED`

每个拒绝/降级都带中文解释（`autonomyReasonZh`）并写入审计日志
（`sicnu.autonomy-decision/1`：sequence 单调、无墙钟、1000 条上限 FIFO、
线程安全）——"为什么被禁"事后可答。

## Wiring points (for the other tracks)

- **Course policy**: `AutonomyPolicyHolder::instance().installCoursePolicyJson(text)`
  （宿主启动时安装；非法文档拒绝并保留旧 policy，typed problems 可查）。
  默认课程 policy 是 research 默认（L5/agent）——既有科研流程行为不变；
  教学宿主安装限制性 policy。
- **LabSpec layer**: 实验文档的可选 `autonomy` 块（见
  `data/labs/labspec.schema.json`），由 gate 通过 `LabSpecCatalog` 读取。
- **Teacher/session layer**: 宿主注入 `input["autonomy"]`
  （`{level?, mode?, max_level?, capability_overrides?}`；不带 `source`——
  layer 身份由注入位置决定）。
- **Status rendering**: `autonomyStatusProjection(policy, role, domain)` 或
  `harness:autonomy_status` 工具；UI 只渲染投影，不自行实现业务逻辑。
- **Fake action providers**: `fake_action_provider.h` 提供确定性语料库
  （无 LLM、无网络），policy 测试全部基于它。

## Files

| Path | Role |
| --- | --- |
| `src/agent/autonomy/` | Qt-free 策略层（静态库 `Sicnu::autonomy`，仅依赖 jsoncpp） |
| `src/agent/harness/lab_copilot.cpp` | assistance gate |
| `src/agent/harness/plan_tools.cpp` | execution gate |
| `src/agent/harness/autonomy_tools.cpp` | `harness:autonomy_status` |
| `src/agent/harness/harness_error.{h,cpp}` | 9 个 autonomy reason code 入封闭错误表 |
| `tests/test_autonomy_*.cpp` | 轻量套件（schema/分类/引擎/优先级/审计/投影/holder）+ 重量级 gate 套件 |
