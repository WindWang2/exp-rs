# Plan — RS14-04-labspec2-runtime：LabSpec v3 + Undergraduate Lab Runtime

Date: 2026-09-22 · Baseline `origin/master@4f6632e1f` · ADR: `docs/adr/0174-labspec-runtime-2.md`（本 PR 新增）

## 1. Problem statement

master 的 LabSpec（ADR 0146）解决了"一个 lab 一个事实源"，但它是**静态操作说明**：steps 线性、无阶段目标、无可验证检查点、无提示政策、无可复现性要求；运行期没有任何学生状态——`GuidedWorkflowWidget` 的 step index 重启即失，`LabStepCard.isCompleted` 从未置 true，lab copilot 没有学生模型。结果是：(a) 学生得到的是 PDF 式的"点按钮"流程，看不到自己的科学过程证据；(b) 平台无法回答"这个学生做没做到 X、用了哪个工具、第几次尝试"；(c) 未来 Agent 无法把 LabSpec 当可执行 recipe 消费（无 checkpoint 契约可对账）。

## 2. User stories

**本科生视角**
- US1: 我能在实验里看到分阶段目标（stage objective），而不是一次性 20 个 step 的清单。
- US2: 我能自由尝试任何工具（不被锁死流程），但每个阶段结束时我能点"检查"，机器告诉我 checkpoint 是否达成、证据是什么（哪个文件、哪次操作、哪道思考题）。
- US3: 我卡住时能按层级请求提示（提示有预算，超额被 typed 拒绝并被引导求助教师），且提示历史被记录。
- US4: 我关闭软件明天再打开，实验从上次的阶段/检查点/答案继续（spec 被改动时给出明确 typed 提示而非静默错位）。
- US5: 我能看到实验完成状态（哪些 gate 未过、哪些问题未答）。

**AI Agent 视角**
- US6: 我能以纯 C++/JSON 接口读取 v3 LabSpec（stages/checkpoints/hints/reproducibility），获得每个 checkpoint 的机器可验证契约，作为未来 recipe source（本 track 不做执行）。
- US7: 我能读取 session JSON（canonical、versioned、typed），无 GUI 依赖地回答"学生在哪、试了几次、用了什么工具、哪些 checkpoint 通过"。
- US8: 一切失败 typed 化：unknown/unsafe/unsupported/drift 都有稳定错误码，禁止 silent fallback。

## 3. Architecture

```
data/schemas/labspec.schema.json          spec_version enum {1,2,3}；v3 新增可选 runtime block（additive）
data/labs/lab17/18/19_*.lab.json          3 个 v3 exemplar（走 ADR 0146 data-commit 全路径）
data/labs/lab-registry.json               append lab17-19 canonical 条目
        │ 权威加载器最小扩展 ▼
src/app/widgets/lab_spec_loader.{h,cpp}   接受 v3 键（仅接受；v1/v2 路径零改动）
        │ 深校验/运行时 ▼
src/lab/  （新 static lib sicnu_lab_runtime，Qt-free，jsoncpp+std，layer guard）
  spec_runtime.{h,cpp}      runtime block 解析 + typed 校验（LabRuntimePlan 值类型）
  spec_migrate.{h,cpp}      v1/v2→derived plan（兼容 adapter）+ v3 canonical 校验 + spec 指纹
  session_state.{h,cpp}     LabSession 值类型 + validated 状态转换 + canonical JSON
  session_store.{h,cpp}     原子持久化（tmp+fsync+rename）/目录解析/列表/加载/指纹对账
  checkpoint_verify.{h,cpp} 纯验证（注入 fs 接口 + byte budget，3 check kinds）
  hint_policy.{h,cpp}       提示揭示 + 预算/升级政策（typed 拒绝超额）
        │ 渲染（薄） ▼
src/app/widgets/lab_runtime_panel（最小 UI：目标/检查点/提示/完成态，全部调 src/lab 服务）
tests/test_lab_runtime.cpp                新轻量 Catch2 target（jsoncpp+std，不链 Qt/QGIS）
```

单一事实源声明：lab 内容/身份/文档/pack/判分全部维持既有权威（schema+registry+gen 脚本+rules）；本 track 新增的唯一事实 = **runtime block（教学运行契约）** 与 **session（学生过程事实）**；两者都 versioned，文档与 UI 只是投影。

## 4. Public API / data schema

### 4.1 LabSpec v3 `runtime` block（草案，TDD 中固化）

```jsonc
{
  "runtime": {
    "data_packs": ["lab17_optical_preprocessing"],      // 可选；必须解析到 packs/*.pack.json
    "stages": [{
      "id": "s1_preprocess",                             // ^[a-z][a-z0-9_]*$，唯一
      "title": "...", "title_zh": "...",
      "objective": "...", "objective_zh": "...",         // stage 级教学目标
      "step_indices": [0,1,2],                           // 升序、界内、stages 间不相交
      "allowed_tools": ["rs:clip", "rs:ndvi"],           // 可选；精确 id 或 "rs:pre*" 前缀通配
      "checkpoints": [{
        "id": "ckpt_ndvi", "title": "...", "title_zh": "...",
        "checks": [
          {"kind":"artifact_present","path":"outputs/labs/.../ndvi.tif","min_bytes":1,"sha256":"<可选>"},
          {"kind":"operator_invoked","operator_id":"rs:ndvi","params_subset":{...}},   // 或 "operator_id_any":[...]
          {"kind":"question_answered","question_id":"q1"}
        ],
        "attempts_allowed": 3,                           // 缺省 0 = 不限
        "advance": "observe"                             // observe(默认)=只记录 | gate=未过则 blocked_advance
      }]
    }],
    "questions": [{"id":"q1","prompt":"...","prompt_zh":"...","kind":"free_text|numeric|choice",
                   "choices":["..."],"expected_numeric":{"min":..,"max":..}}],
    "hints": {"policy":{"max_reveals_per_target":3,"escalation":["nudge","hint","worked_example"]},
              "entries":[{"target_step":2,"target_checkpoint":"ckpt_ndvi","level":1,"text":"...","text_zh":"..."}]},
    "reproducibility": {"require_seed":true,"deterministic_operators_only":true,"notes":"..."}
  }
}
```

校验规则（全部 typed）：runtime 内 id 唯一；step_indices 升序界内且 stages 间不相交；checkpoint check 引用的 question_id 存在；hints entry 的 target 存在；escalation 覆盖 level 1..N；data_packs 指向存在的 pack 文件（drift-guard）；未知 kind/target/字段拒绝。

### 4.2 Session（`sicnu.lab-session/1`，canonical JSON，无时间戳，seq 单调）

```jsonc
{
  "schema":"sicnu.lab-session/1",
  "session_id":"lab17_optical_preprocessing/student1/1",   // <lab>/<student>/<seq>，确定性
  "lab_id":"...","lab_spec_fingerprint":"sha256:<hex>","lab_spec_version":3,
  "plan_source":"authored_v3|derived_from_v2|derived_from_v1",
  "student_id":"...","seed":42,
  "state":"active|completed|abandoned",
  "stages":[{"stage_id":"...","status":"active|blocked_advance|advanced"}],
  "checkpoint_results":[{"checkpoint_id":"...","verdict":"pass|fail|unverifiable","attempt":1,
                          "evidence":[{"check_index":0,"ok":true,"observed":"...","expected":"..."}],"seq":5}],
  "question_answers":[{"question_id":"q1","answer":"...","seq":3}],
  "hint_events":[{"target":"checkpoint:ckpt_ndvi","level":1,"seq":2}],
  "tool_choices":[{"stage_id":"...","operator_id":"rs:ndvi","params_subset":{...},"allowed":true,"seq":4}],
  "execution_refs":[{"kind":"experiment_run","id":"..."}],
  "last_seq":5
}
```

状态转换（validated，非法→`lab.session.bad_transition`）：session `active→completed`（仅当全部 gate checkpoint 最近 verdict=pass 且必需 questions 已答）、`active→abandoned`、`abandoned→active`、`completed` 终态。stage `active→advanced` 仅当该 stage 全部 checkpoint 最近 pass；gate 未过时 verify 后置 `blocked_advance`（**永不阻止记录新操作**）。

### 4.3 核心 C++ API（Qt-free 值语义）

```cpp
namespace sicnu::lab {
struct LabDiag { std::string code, message; };                       // 点分小写 code，模式对齐 sicnu::data::Diagnostic
template<class T> struct LabResult { bool ok; T value; std::vector<LabDiag> diags; };  // 模式对齐，不引 Qt

struct LabRuntimePlan { /* stages/checkpoints/questions/hints/repro */ };
LabResult<LabRuntimePlan> parseRuntimeBlock(const Json::Value& doc);            // spec_runtime
LabResult<LabRuntimePlan> derivePlanFromLegacySpec(const Json::Value& doc, int spec_version); // spec_migrate
std::string specFingerprint(const std::string& fileBytes);                      // sha256 hex

struct LabSession { /* 上述 JSON 的值类型 */ };
LabResult<LabSession> startSession(const LabRuntimePlan&, const SessionMeta&);  // session_state
LabResult<> recordToolUse(LabSession&, ...); recordAnswer(...); revealHint(...); recordExecutionRef(...);
struct CheckpointReport { std::string checkpoint_id; std::string verdict; std::vector<CheckEvidence> evidence; };
LabResult<CheckpointReport> verifyCheckpoint(const LabRuntimePlan&, const LabSession&, const FileProbe&); // checkpoint_verify
struct HintReveal { int level; std::string text, text_zh; };
LabResult<HintReveal> revealHint(const LabRuntimePlan&, LabSession&, const HintTarget&);   // hint_policy

class LabSessionStore {                                                         // session_store
  static std::string resolveStoreDir();               // SICNU_LAB_SESSION_DIR → <cwd>/.sicnu/lab/sessions → ...
  LabResult<LabSession> save(const LabSession&);      // tmp+fsync+rename，canonical bytes
  LabResult<LabSession> load(const std::string& session_id, const std::string& currentSpecFingerprint);
  std::vector<SessionSummary> listSessions() const;   // 字典序确定性
};
}
```

错误码词表（append-only）：`lab.session.schema / lab.session.version / lab.session.corrupt / lab.session.not_found / lab.session.bad_transition / lab.session.spec_drift / lab.session.hint_budget / lab.session.hint_unknown_target / lab.session.answer_unknown_question / lab.session.attempt_over_budget / lab.runtime.schema / lab.runtime.field / lab.runtime.reference / lab.runtime.version`。

## 5. Migration / compatibility

- Schema：enum {1,2,3}；allOf guard 扩展——v1 禁 v2+v3 全部键，v2 禁 v3 键（`runtime`）；v3 无新增必填。
- 权威加载器 `lab_spec_loader`：v3 键**接受不深析**（保证 corpus/registry/docs 门禁不红）；v1/v2 校验路径与错误文本零改动（先加锁定测试再改）。
- C++ migrator：`derivePlanFromLegacySpec` 把 v1/v2 派生为单 stage 默认 plan（checkpoints 由 expected_artifacts 派生 artifact_present 检查；v1 无 artifacts 则仅完成态 checkpoint：全部 steps 的 operator_invoked 由 tool 记录对账）。派生计划在 session 里留 `plan_source`，绝不回写 spec 文件。
- `check_lab_registry.py`：{1,2}→{1,2,3} 最小 delta；`gen_lab_docs.py`：渲染 stages/checkpoints 概要（v3 页），v1/v2 输出 byte 不变。
- 14 个既有 v2 spec **不迁移不回写**（adapter 语义兼容）。

## 6. Observability

- session JSON 本身即过程观测面（seq 单调、append-only 事件）；`listSessions()` 提供 summary。
- verifyCheckpoint 的 evidence 数组携带 observed/expected（对齐 grader "failures as evidence" 惯例）。
- 每次持久化原子完成；load 报 typed 诊断；无日志依赖（纯值返回，宿主自行呈现）。

## 7. Security / trust boundary

- 新解析器全部 `CharReaderBuilder` + `stackLimit=64` + try/catch（不复制 #1154/#1155 缺陷）。
- session 文件是**学生本地数据**：不做身份认证、不联网、路径解析拒绝 `..` 逃逸（artifact path 必须相对 root 且不越界）；sha256 只用于漂移/证据，不做信任根。
- hint 预算是教学信任边界：超额 typed 拒绝（对齐 lab_copilot "宁可少帮" 纪律），永不 silent 放水。
- spec 指纹漂移 fail-closed（`lab.session.spec_drift`），杜绝静默错位继续。

## 8. Performance budget

- 全部为元数据层操作：单 spec 校验 O(stages×checks)；session save/load O(事件数) 且单文件；verify 单文件读 ≤64 MiB budget（超限 unverifiable，不读 whole-tree）；`listSessions` O(文件数) 只读头部 schema 行。
- 无栅格 IO、无算子执行、无网络；测试目标不链 Qt/QGIS。

## 9. Test strategy（TDD 分片见 slices.md）

- 新轻量 target `test_lab_runtime`（Catch2 + jsoncpp + std，无 Qt/QGIS）：schema/校验负例×N、状态机全转换矩阵、持久化原子性/确定性 round-trip、指纹漂移、hint 预算、checkpoint 3 kinds + budget + 注入 fs。
- 既有门禁回归：`test_labspec`（含新 exemplar 自动纳入）、`check_lab_registry.py`、`gen_lab_docs.py --check`、`gen_lab_packs.py --check`、`test_lab_data_pack`。
- 杀伤力要求：每条核心行为先 RED（删守卫必须被击落），reviewer 做假绿尝试。
- E2E：核心级"start→操作→提示→答→verify→persist→新 store 实例 resume→complete"全链（离线、无网络）；UI 只做薄渲染（controller 全逻辑在 core 已测）。

## 10. Work packages（= slices）

A schema+validator+migrator → B session 状态机+持久化 → C checkpoint 契约 → D hint/choices → E legacy adapter → F 3 exemplar labs+fixtures+registry/docs 门禁 → G UI 薄适配+restart recovery。依赖：A→(B,C,D,E)→F→G；B 先于 C/D（session 承载记录）。

## 11. Rollback / kill-switch

- 新模块 `src/lab/` + 新测试 target 完全独立，revert PR 即全部消失；根 CMakeLists 仅 +1 行。
- schema enum +1 是 additive；既有 v1/v2 文档与路径零改动，`check_lab_registry` 改动为常量集合扩展（单独 commit，可单独 revert）。
- UI 改动限于新增 panel 文件 + GuidedWorkflowWidget 少量接线（独立 commit）；kill 时删 panel + 接线 commit 即可，core 不受影响。

## 12. Definition of Done

- 公共 DoD（计划/代码/测试一致、targeted 绿、无新 warning、两轮 review、动态去重、union merge）＋ track DoD：
  1. E2E 教学场景（lab17）：学生经 stages/checkpoints/hints 完成实验，重启可恢复，获得过程证据（比"点按钮"多出机器可验证反馈）。
  2. 机器可读接口：v3 spec + session JSON + Qt-free API（US6-8 全绿）。
  3. 单一事实源：不复制 pack/rules/provenance/experiment；只投影与引用。
  4. typed 失败全覆盖（错误码词表测试锁定）。
  5. 离线：全部测试与 exemplar 无网络。
  6. 资源上界：budget/上限有测试；构建全程 `-j1`。
  7. PR 前重新动态去重（master/open PRs）。
