# Recon — RS14-18 Undergraduate Curriculum Pack

基线：origin/master `4f6632e1f6`（2026-09-21，PR #1145 合并后）。open PR：0（已核实）。
open issues 与本 track 避让清单一致，未发现新增重叠方向。

## 已有能力（全部复用，不重建）

| 契约 | 权威位置 | 说明 |
|---|---|---|
| LabSpec v1/v2 | `data/labs/*.lab.json`（14 个，全部 v2）+ `data/schemas/labspec.schema.json` | 严格加载器 `lab::loadLabSpecsFromDir`（`src/app/widgets/lab_spec_loader.{h,cpp}`），未知键拒绝、v2 键版本门禁、id==文件主干、正则 `^lab[0-9]{2}_[a-z][a-z0-9_]*$` |
| lab 身份注册表 | `data/labs/lab-registry.json`（`sicnu.lab-registry/1`） | canonical 仅 3 条（lab12/13/14，别名制）；`out_of_scope`: lab8_temporal_analysis（temporal track 所有）、temporal_phenology_timeline；`scripts/check_lab_registry.py` 为强制门禁（6 项检查含 pack 双射） |
| 数据部署契约 | `data/labs/packs/*.pack.json`（`sicnu.lab-pack/1`，17 个） | `src/agent/lab_data_pack.{h,cpp}` 校验；`scripts/gen_lab_packs.py` 确定性生成，`--check` 零 diff 门禁；committed-fixture 需 sha256+bytes |
| 判分契约 | `data/labs/grading/*.rules.json`（`sicnu.lab.rules/1`） | 权重和=100、blocking 语义、可解释 transcript；CLI `lab --grade/--batch/--self-check` |
| 科学数据要求 | `data/labs/data-specs/`（`sicnu.lab-data-spec.v1`） | 4 个（lab8/9/10/11 时代） |
| 文档生成 | `scripts/gen_lab_docs.py`（ADR 0146） | JSON 单一事实源 → `docs/labs/*.md`；`--check` 由 test_labspec 强制 |
| 算子存在性 | `sicnu::operators::RSOperatorRegistry::instance()`（`hasOperator/operatorNames`，185 个 `rs:`/`opencv:`/`io:`/`cartography:` 注册）+ `sicnu::processing::AtomicAlgorithmRegistry::findAdapter` | test_labspec 已做全量 lab 算子 drift（存在性+参数 schema `UnknownParameterPolicy::Error`） |
| Agent 能力镜像 | `data/agent/capabilities/*.json`（15 族）+ `sicnu::agent::harness::CapabilityKnowledge::entryForOperator` | `tests/test_capability_drift` 守护 |
| harness 目录缝 | `src/agent/harness/lab_spec.h`（LabSpecCatalog 单例：setDirectory/reload/status/labIds/lab/stepDoc） | 默认目录搜索 `$SICNU_LAB_SPEC_DIR → <cwd>/data/labs → SICNU_SOURCE_DIR/data/labs` |
| 会话/进度 | 仅 `context_checkpoint.h`（HarnessSessionState，lab 链无进度概念） | 课程进度是真空隙 |
| 测试接线 | `tests/CMakeLists.txt: sicnu_add_test(NAME)`（qt_add_executable + Catch2 + sicnu_add_test Discover） | `src/agent/CMakeLists.txt` 显式列源文件进 `sicnu_agent` SHARED 库 |

## 缺口（本 track 填补）

1. **curriculum 层完全不存在**（全仓 grep "curriculum" 零命中）：无 module/learning outcomes/先修链/学时/进度模型。
2. 无 module → lab → pack → operator 的**机器可读可用性投影**（test_labspec 是测试不是运行时查询接口）。
3. 模块缺口：
   - 「遥感数据与波段/元数据」无专属 lab（`io:inspect` 已注册，可建 lab15）。
   - 「精度评价」无专属 lab（band_math/feature_stack/zonal_stats 已注册 + `tests/fixtures/lab/landcover_*.tif` 已有，可建 lab16）。
   - 「AI 推理」：`rs:infer` 已注册但仓库无离线 ONNX 权重（`find models -name '*.onnx'` 零命中）→ **不得虚构**；离线路线 = feature_stack + feature_normalize + supervised_classification（ML 推理语义）+ 精度验证；`rs:infer` 标注为环境依赖能力（typed forward reference）。
   - 「时间序列/物候」：`lab8_temporal_analysis` 由 temporal track（PR #1135）所有，registry `out_of_scope` → 课程包**只引用不修改**，引用类型标 `external`。

## 不做什么

- 不改 LabSpec v2 schema/加载器（新增教学元数据放 curriculum manifest，不给 labspec 加键）。
- 不改 Processing Registry / Provenance / Experiment / capability mirror 的任何核心行为。
- 不触碰 out_of_scope labspec（lab8_temporal_analysis 等）；不修复避让清单中任何 issue。
- 不建第三套 registry/store/provenance；不做 GUI 面板实现（只做 value-type 进度模型）。
- 不重写既有 14 个 labs 的内容；新 lab 只补真空隙。

## 风险

| 风险 | 缓解 |
|---|---|
| 新 lab 参数与算子 schema 漂移（test_labspec `UnknownParameterPolicy::Error`） | 步骤参数从 `schema()` 源码逐字段抄录；实现后跑 test_labspec |
| pack 双射门禁（check_lab_registry#5 + test_lab_data_pack drift） | 新 lab 同步扩展 `gen_lab_packs.py` pack map 并重新生成 |
| gen_lab_docs 零 diff 门禁 | 新 lab 落地后跑 `gen_lab_docs.py`，提交生成产物 |
| 课程 manifest 与 lab-registry 形成第二真相源 | manifest **只引用** lab id / pack 名，不复制任何 lab 内容字段；校验器把不一致报为 typed error |
| 20 并发 track 资源 | 新 C++ 仅 2 对 .h/.cpp 纯 Json 单测可验；测试链接最小化（sicnu_agent 已含 harness）；`-j1` |

## 与其他 tracks 的接口

- 上游只读依赖：LabSpec 目录、lab-registry、packs、RSOperatorRegistry、CapabilityKnowledge。
- 对外提供：`data/curriculum/undergraduate_rs.curriculum.json`（`sicnu.curriculum/1`）、`sicnu::agent::harness::CurriculumCatalog`（机器可读查询/可用性报告）、`CurriculumProgress`（UI value model，JSON 确定性序列化）。
- temporal track：经 manifest `role: "external"` 引用 `temporal_analysis`；其 labspec/pack 归属不变。
- 未来 GUI：进度模型即投影源；未来 Agent：catalog 查询接口 + manifest 本身即机器可读契约（`docs/integration.md` 记录接线点）。

## 与 open issues 去重矩阵

本 track 新建目录 `data/curriculum/`、新文件 `src/agent/harness/curriculum_*.{h,cpp}`、新测试 `tests/test_curriculum.cpp`、新 labs `lab15/16`。避让清单（#1146–#1187）无一位于这些缝上；若实现中发现既有链路缺陷，记录 blocked/observed 不扩大 scope。

## 结论

课程包 = 对既有 lab 契约链的**组织/验证/投影层**，唯一新增事实是「教学组织元数据」本身。扩展缝稳定：harness 目录 + data/curriculum + gen_lab_packs pack map。
