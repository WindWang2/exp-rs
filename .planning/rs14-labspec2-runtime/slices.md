# Slices — RS14-04-labspec2-runtime

每片严格 RED → GREEN → REFACTOR → narrow tests → reviewer 杀伤 → commit → progress.md。

## Slice A — LabSpec v3 schema + validator + migrator 基础
- A1: schema enum {1,2}→{1,2,3} + v1/v2 禁键 guard 扩展（jsonschema 自检 + 既有 v1/v2 接受/拒绝测试先锁定）。
- A2: `src/lab/spec_runtime.{h,cpp}`：`parseRuntimeBlock` happy path（RED：模块不存在编译失败→最小实现）。
- A3: typed 负例矩阵（unknown key / 坏 id / step_indices 越界·无序·相交 / check 引用悬空 question / hint target 悬空 / escalation 不覆盖 / data_pack 不存在→drift 由上层测，此处只查形状 / 非 runtime 对象）。
- A4: `specFingerprint`（sha256 hex）+ 确定性测试。
- 验证：`test_lab_runtime`（新轻 target）+ jsonschema 校验三个 schema 状态。

## Slice B — session state machine + persistence
- B1: `session_state.{h,cpp}` 值类型 + `startSession`（确定性 session_id；plan_source；require_seed 无 seed → typed 拒绝）。
- B2: 转换矩阵：active→completed（gate 全 pass + 必答题全答才允许；否则 typed 拒绝列出未满足项）/→abandoned/abandoned→active/completed 终态拒绝/bad_transition。
- B3: canonical JSON round-trip byte 级确定（sorted keys、无时间戳、seq 单调）；`lab.session.schema/version/corrupt` 载入负例。
- B4: `session_store`：目录解析（env 覆盖）、save 原子（tmp+fsync+rename）、load 指纹漂移 `spec_drift` fail-closed、listSessions 字典序、not_found。
- 验证：`test_lab_runtime` 增用例。

## Slice C — checkpoint contract / artifact references
- C1: `checkpoint_verify.{h,cpp}` + 注入 `FileProbe` 接口；artifact_present（存在/min_bytes/sha256 可选/`..` 逃逸拒绝/budget 超限→unverifiable typed）。
- C2: operator_invoked（单 id 与 any-of；params_subset 匹配 tool_choices 记录；未记录→fail with evidence）。
- C3: question_answered（未答→fail；numeric expected range 判定）。
- C4: attempts 记数 + attempts_allowed 超限→verdict 附 `attempt_over_budget`；gate→stage `blocked_advance`/pass→`advanced`；observe 永不改变 stage 状态。
- C5: recordToolUse / recordAnswer / recordExecutionRef / allowed_tools 白名单（精确+前缀通配；不允许→`allowed:false` 照实记录，不拒绝——自由探索）。

## Slice D — hint policy / student choice recording
- D1: `hint_policy.{h,cpp}` revealHint：按 target_step/target_checkpoint + level 升序揭示；未揭示低级时跳级→typed 拒绝（escalation 纪律）；预算超→`lab.session.hint_budget`；hint 事件入 session。
- D2: 与 B/C 组合：hint 历史跨持久化保留（save→load→reveal 计数连续）。

## Slice E — existing lab adapter
- E1: `derivePlanFromLegacySpec` v2（expected_artifacts→artifact_present checkpoints；单 stage 全 steps）。
- E2: v1（无 artifacts→完成 checkpoint：全部 operator steps 的 operator_invoked any-of 集合）。
- E3: 派生 plan 开启 session→完成全链 E2E；plan_source 记录；**不回写文件**。
- E4: 14 个真实 v2 spec 全部可派生（corpus 测试，读 data/labs）。

## Slice F — 3 exemplar labs + deterministic offline fixtures + 门禁
- F1: lab17_optical_preprocessing（光学预处理+NDVI：s1 数据体检/s2 预处理/s3 指数与解读；checkpoints：artifact+operator+question；hints 3 级；require_seed）。
- F2: lab18_supervised_classification（监督分类基础：训练区/分类/精度自查问题）。
- F3: lab19_change_detection（变化检测基础：dNBR/差值+阈值判断题）。
- F4: 确定性 fixtures（纯代数无 RNG 生成器脚本）+ packs（generated-samples provenance）+ `gen_lab_packs.py` 集成。
- F5: registry lab17-19 条目 + `check_lab_registry.py` {1,2,3} + `gen_lab_docs.py` v3 渲染 + 文档再生成 + `test_labspec` 扩展（v3 corpus/v2 键入 v3 拒绝/v3 corpus drift guard）。
- 验证：test_labspec、test_lab_data_pack、check_lab_registry.py、gen_lab_docs.py --check、gen_lab_packs.py --check、jsonschema。

## Slice G — UI adapter / restart recovery
- G1: `src/app/widgets/lab_runtime_panel.{h,cpp}`（薄渲染：stage/checkpoint 状态、当前目标、提示按钮、完成态；全部调 src/lab）。
- G2: GuidedWorkflowWidget 接线：Start→startSession/resume；Run This Step→recordToolUse+save；panel 展示；独立 commit。
- G3: restart recovery E2E（core 级全链 + widget 级最小验证）；docs/labspec-runtime-integration.md + ADR 0174。

## Review Gate（编码后）
- adversarial-reviewer 假绿尝试 + P0-P3 分类；全部 P0/P1/P2 修复后 re-review 一轮。

## Final
- fetch origin → 与最新 master 语义 union（预期冲突：lab-registry.json/test_labspec.cpp/tests CMakeLists/gen_lab_*.py/生成 docs/.gitignore）→ 受影响最小测试集 → PR。
