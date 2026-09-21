# Slices — RS14-18

每个 slice 粒度 = 一个 red test → minimal implementation → refactor → green → commit。

## Slice A — curriculum schema + manifest 严格载入
- RED：`tests/test_curriculum.cpp` 首个 CASE：加载 shipped manifest（先造最小 fixture）
  断言 `status()=="ok"`；负例：未知 schema 版本 → `unknown_schema`；未知顶层键拒绝。
- GREEN：`curriculum_catalog.{h,cpp}` 最小载入（目录搜索 + 严格键集 + schema 常量），
  挂进 `src/agent/CMakeLists.txt` + `tests/CMakeLists.txt`。
- 产物：`data/schemas/curriculum.schema.json`、`data/curriculum/undergraduate_rs.curriculum.json`（骨架：先 1 模块 1 lab，后续 slice 扩全）、`scripts/check_curriculum.py`、`.gitignore` re-include。

## Slice B — 引用与结构校验（typed errors）
- RED→GREEN 逐个：duplicate_module_id/index、cyclic_prerequisites（A→B→A）、
  unknown_lab_reference（三级解析链）、undeclared_external_lab、unknown_data_pack、
  empty_learning_outcomes、nonpositive_effort、invalid_lab_role。
- refactor：错误收集器统一 `sicnu.curriculum.error/1` 形状。

## Slice C — 算子可用性三态 + availabilityReport
- RED：fixture manifest 引用 `rs:spectral_index`（available）、`rs:definitely_not_an_operator`
  （unknown）、forward_references `rs:infer`（declared_unavailable）。
- GREEN：`availabilityReport()`；CapabilityKnowledge 缺 entry → `registered_no_capability_note`（warning 级，不失败）。

## Slice D — CurriculumProgress 值模型
- RED：emptyDoc/markCompleted/幂等/冲突/未知 lab/模块完成度/JSON 确定性往返（byte 相等）。
- GREEN：`curriculum_progress.{h,cpp}`；`CurriculumCatalog::progressFor` 投影。

## Slice E — lab15_data_inspection
- RED：test_labspec 已是权威 drift 门禁（新 lab 落地自动被扫）；新增断言：curriculum
  shipped manifest 中 lab15 可解析 + 其算子 available。
- GREEN：lab15.lab.json（参数逐字段抄 schema()）+ gen_lab_packs pack map 行 + 生成 pack +
  gen_lab_docs 产物 + manifest 扩至 m01…m05 完整映射。

## Slice F — lab16_accuracy_assessment
- 同 E 链路 + `accuracy_agreement.rules.json`（权重和=100、derivation 写闭式界）。
- manifest 扩至 10 模块 16 labs 终态。

## Slice G — offline 全解析 + 回归
- RED：manifest 全引用 repo 相对路径、无 URL/URI；pack 全 present；external 引用仅 temporal_analysis。
- GREEN→验证：`ctest -R "test_curriculum|test_labspec|test_lab_data_pack" -j1` +
  `python3 scripts/check_curriculum.py` + `check_lab_registry.py` + `gen_lab_packs.py --check`。

## Commit 粒度
每 slice 一个 commit；A/B/C/D 为 C++ 契约层，E/F 为内容层，G 为门禁层。
