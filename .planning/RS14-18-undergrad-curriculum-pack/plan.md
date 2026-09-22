# Plan — RS14-18 Undergraduate Curriculum Pack

## Problem statement

平台已有 14 个 v2 LabSpec、判分/部署/数据三套契约，但它们是**平铺的**：没有 module、
先修链、学时、学习成果、进度模型——本科生看不到「这门课长什么样、我在哪、下一步学什么」，
AI Agent 也无法机器可读地回答「这门课包含什么、本机能不能跑、缺什么」。本 track 构建
curriculum 组织层填补该空隙。

## User stories

**本科生视角**
- 我能读一份课程清单：10 个模块、16 个实验、每个实验的先修模块/学时/数据包。
- 每个实验我能看到：目标、学习成果、常见错误（teacher notes）、判分挂钩说明。
- 完成一个实验后，我的进度文件（JSON）能算出模块/总完成度。

**AI Agent 视角**
- `CurriculumCatalog` 机器可读：`manifest()`、`modules()`、`availabilityReport()`。
- 可用性报告是 typed 三态：`available`（注册表+镜像均认可）/ `registered_no_capability_note`
  （注册表有、镜像无 → warning 级）/ `unknown`（不存在 → error 级）；`external`/`declared_unavailable`
  引用是显式声明而非静默回退。
- 所有失败 typed、机器可读（`sicnu.curriculum.error/1` 词汇），无 silent fallback。

## Architecture

```
data/curriculum/undergraduate_rs.curriculum.json   ← sicnu.curriculum/1（新事实：教学组织）
data/schemas/curriculum.schema.json                ← JSON Schema draft-07（创作合同）
        │ 只引用（不复制）▼
data/labs/*.lab.json ── lab-registry.json ── packs/ ── grading/
        │
src/agent/harness/curriculum_catalog.{h,cpp}   ← 单例，仿 LabSpecCatalog：加载/校验/查询/可用性报告
src/agent/harness/curriculum_progress.{h,cpp}  ← 纯 value model：完成度计算 + 确定性 JSON 往返
scripts/check_curriculum.py                    ← 轻量 CI 门禁（schema+引用+DAG，不重复算子检查）
tests/test_curriculum.cpp                      ← 全部行为的 RED→GREEN 契约测试
data/labs/lab15_data_inspection.lab.json       ← 新 lab（M1）
data/labs/lab16_accuracy_assessment.lab.json   ← 新 lab（M6）
```

单一事实源声明：lab 内容仍归 LabSpec，pack 归 gen_lab_packs，身份归 lab-registry；
curriculum manifest 只新增「模块组织/成果/学时/教师注记/外部引用声明」，
且校验器把「manifest 与既有契约不一致」报为错误，而不是自行纠偏。

## Public API / data schema

### `sicnu.curriculum/1` manifest

```jsonc
{
  "schema": "sicnu.curriculum/1",
  "id": "undergraduate_rs",
  "title": "...", "title_zh": "遥感科学与技术本科实验课程",
  "audience_zh": "...", 
  "modules": [{
    "id": "m01_rs_data",            // ^m[0-9]{2}_[a-z][a-z0-9_]*$
    "index": 1,                      // 唯一，1..N
    "title": "...", "title_zh": "遥感数据与波段/元数据",
    "summary_zh": "...",
    "learning_outcomes": ["…"],     // ≥1
    "prerequisite_modules": [],      // 引用既有 module id；DAG 无环
    "estimated_effort_minutes": 240, // 模块合计下限声明 >0
    "optional": false,
    "labs": [{
      "lab_id": "lab15_data_inspection",     // 必须可解析（见校验）
      "role": "core",                 // core | optional | external
      "estimated_effort_minutes": 120, // >0
      "required_data_packs": ["lab15_data_inspection"], // pack 基名必须存在
      "teacher_notes": {
        "objectives_zh": ["…"],
        "common_mistakes_zh": [{"mistake_zh": "…", "why_zh": "…", "check_zh": "…"}],
        "grading_hook_zh": "…"
      }
    }]
  }],
  "forward_references": [{            // 诚实标注当前环境不可用能力
    "capability": "rs:infer",
    "reason_zh": "模型权重不入库（离线约束）；接线点见 docs/integration.md",
    "wiring": "docs/integration.md#model-inference"
  }]
}
```

lab 解析规则（校验器，按序）：
1. `data/labs/<lab_id>.lab.json` 存在 → 严格加载器解析通过 → ok；
2. 否则须命中 lab-registry canonical/alias → 解析其 source → ok；
3. 否则 `role` 必须为 `external` 且 id 出现在 lab-registry `out_of_scope` → ok（typed external）；
4. 都不满足 → typed error `unknown_lab_reference`。

### C++（namespace `sicnu::agent::harness`）

```cpp
class CurriculumCatalog {   // curriculum_catalog.h —— 单例，仿 LabSpecCatalog
  static CurriculumCatalog &instance();
  void setDirectory(const std::string &dir);   // 默认 $SICNU_CURRICULUM_DIR → <cwd>/data/curriculum → SICNU_SOURCE_DIR/data/curriculum
  std::string directory() const;
  int reload();                                // 返回加载的 module 数；失败载入 loadProblems
  bool loaded() const;
  std::string status() const;                  // "ok" | "unavailable"
  const std::vector<std::string> &loadProblems() const;
  Json::Value manifest() const;                // 原始文档（未知时 null）
  std::vector<std::string> moduleIds() const;  // index 排序
  Json::Value module(const std::string &id) const;
  Json::Value labRef(const std::string &moduleId, const std::string &labId) const;
  // 可用性报告（机器可读；oper subjects: 每 lab 的 operator 集合 ∪ declared forward_references）
  Json::Value availabilityReport() const;      // {schema:"sicnu.curriculum.availability/1", generated_from, modules:[{module_id, labs:[{lab_id, role, resolvable, operators:[{operator_id, state, note?}], data_packs:[{name, present}]}]}], forward_references:[…]}
  Json::Value progressFor(const Json::Value &progressDoc) const; // 进度文档 → 完成度投影（见下）
};

struct CurriculumProgress {  // curriculum_progress.h —— 纯值语义，无 IO 副作用
  static Json::Value emptyDoc(const std::vector<std::string> &labIds); // {schema:"sicnu.curriculum.progress/1", completed:{}}
  static std::vector<std::string> validateDoc(const Json::Value &doc, const std::vector<std::string> &knownLabIds);
  static bool markCompleted(Json::Value &doc, const std::string &labId, const std::string &evidence, std::string *error);
  struct ModuleStat { std::string moduleId; int total; int done; };
  static std::vector<ModuleStat> moduleCompletion(const Json::Value &doc, const Json::Value &manifest);
  static Json::Value summary(const Json::Value &doc, const Json::Value &manifest); // {overall_percent, modules:[…]}；deterministic 序列化由调用方用 jsoncpp 写出
};
```

进度文档 `sicnu.curriculum.progress/1`：`{schema, student_note?, completed:{lab_id:{evidence,completed_at_iso}}}`；
`completed_at_iso` 由调用方注入（测试注入固定值保证 deterministic replay）。未知 lab_id 拒绝（typed error），
重复标记幂等（同 evidence 覆盖、异 evidence 报错），不存在「静默忽略」。

### typed 错误词汇（`sicnu.curriculum.error/1`，codes）

`unknown_schema` / `unknown_module_reference` / `duplicate_module_id` / `duplicate_module_index` /
`cyclic_prerequisites` / `unknown_lab_reference` / `undeclared_external_lab` / `unknown_data_pack` /
`empty_learning_outcomes` / `nonpositive_effort` / `invalid_lab_role` / `progress_unknown_lab` /
`progress_conflicting_evidence`。所有错误带 `path` + `message_zh`。

## 校验与门禁分层

| 层 | 谁 | 检查 |
|---|---|---|
| 创作 | `data/schemas/curriculum.schema.json` | jsonschema draft-07 结构 |
| CI 轻门禁 | `scripts/check_curriculum.py` | schema + 引用存在性 + DAG + pack 存在性（不算子/不判分） |
| C++ 行为 | `tests/test_curriculum.cpp` | 全部 typed 错误路径 + 可用性三态 + 进度语义 + 确定性 |
| 既有链回归 | test_labspec / test_lab_data_pack / check_lab_registry | 新 lab 不破坏既有门禁 |

算子可用性**不在 python 层重复**（避免第二真相源）：python 只查结构/引用；算子三态只在 C++
（单权威 `RSOperatorRegistry::hasOperator` + `CapabilityKnowledge::entryForOperator`）。

## 课程内容映射（10 模块 / 16 labs）

| 模块 | labs（role） |
|---|---|
| m01 遥感数据与波段/元数据 | **lab15_data_inspection（新, core）** |
| m02 预处理：辐射/几何/镶嵌 | lab06_georeferencing, lab08_atmospheric_correction, lab10_mosaic (core) |
| m03 指数与波段运算/派生 | lab02_spectral_analysis, lab09_pca_analysis, lab05_terrain_analysis (core) |
| m04 图像增强与滤波 | lab01_image_enhancement, lab07_image_fusion (core) |
| m05 分类：监督/非监督/OBIA | lab03_classification, lab11_obia_classification (core) |
| m06 精度评价 | **lab16_accuracy_assessment（新, core）** |
| m07 变化检测：光学/SAR | lab04_change_detection, lab12_sar_processing (core) |
| m08 时间序列与物候 | temporal_analysis (**external**，temporal track 所有，只读引用) |
| m09 高光谱入门 | lab13_hyperspectral_analysis (optional 模块) |
| m10 成果制图 | lab14_cartographic_mapping (core) |

AI 模块处理（诚实边界）：`rs:infer` 进 `forward_references`（无离线权重，不得虚构 lab）；
离线可完成的「推理与验证」教学链已由 m05/m06 承载；`docs/integration.md` 写明未来
model-pack 接线点（model pack + rs:infer + 精度验证闭环）。

## 新 lab 设计（参数以 schema() 源码为准，实现时逐字段核对）

- **lab15_data_inspection**：① action 加载 landsat_sample.tif；② `io:inspect {input, includeStatistics:true}`
  读元数据报告；③ `rs:extract_bands` 抽 NIR 子集（schema 待核）→ `io:inspect` 对比波段数/dtype 变化。
  教学点：波段顺序/CRS/dtype/统计即元数据；pack 引 `data/samples/landsat_sample.tif`（generated-samples）。
- **lab16_accuracy_assessment**：① action 加载分类结果与参考；② `rs:feature_stack` 堆叠
  classified+reference（schema 待核）；③ `rs:band_math {expression:"abs(b1-b2)"}` 不一致图；
  ④ `io:inspect {includeStatistics:true}` 读 overall agreement（mean）；⑤ `rs:zonal_stats`
  分区统计混淆证据。fixtures：`tests/fixtures/lab/landcover_truth.tif` + `landcover_reference.tif`
  （committed-fixture，sha256 由 gen_lab_packs 固定）。判分规则 `accuracy_agreement.rules.json`
  （已知答案：两 fixture 的不一致像素数闭式可算）。

## Migration / compatibility

纯新增：新目录 `data/curriculum/`、新 harness 文件、新测试、新 lab15/16 + pack map 两行 +
registry/gate 兼容性核查。`check_lab_registry.py` 的 pack 双射在新 pack 落地后自动满足；
不修改任何既有 lab/契约文件。`.gitignore` 需 re-include `data/curriculum/`（对齐 data/labs 先例）。

## Observability

- 可用性报告本身就是观测面（typed，机器可读）。
- 测试日志即证据；CLI/Agent 消费零额外日志。
- 无新增运行时全局状态（单例 catalog 仿既有 LabSpecCatalog 模式）。

## Security / trust boundary

- manifest/进度文档均仓库内或本地文件；无网络。
- 进度文档是**学生自报数据**：校验器显式声明它不构成成绩证据（字段 `student_note`），
  成绩证据仍归 grading transcript —— 防止第二成绩真相源。
- 校验器永不修改既有契约文件（只读）。

## Performance budget

- manifest 解析 O(modules+labs)，报告构建 O(labs × operators) —— 常数级（<100 labs）。
- 进度完成度 O(labs)。无栅格 IO、无算子执行 —— 全部是元数据层。
- 测试套件新增 1 个可执行文件，链接既有库，无新重依赖。

## Test strategy

RED→GREEN per slice；`tests/test_curriculum.cpp` 覆盖：
- happy path（shipped manifest：10 模块、16 labs、DAG 合法、全部引用可解析）
- 每个 typed error 至少 1 个负例（fixture 目录驱动，仿 test_harness_lab_evals 的 RAII 目录覆盖）
- 可用性三态（真实 registry：`rs:spectral_index` available；`rs:definitely_not_an_operator` unknown；
  forward_references declared_unavailable）
- 进度：empty/mark/幂等/冲突/未知 lab/完成度计算/JSON 确定性往返（两次序列化 byte 相等）
- external 引用（temporal_analysis）typed 通过且标注不可判分
- offline：manifest 全部引用 repo 内路径（无 URI/URL 字段校验）
teaching-mode/agent-mode 一致性：同一 manifest 经 `progressFor`/`summary` 投影结果与 python
`check_curriculum.py` 引用结论一致（C++ 测试内嵌等价断言）。

## Work packages（slices，详见 slices.md）

A schema+manifest 载入 → B 算子可用性 → C catalog+进度模型 → D 新 labs + 实例 manifest →
E offline/pack 引用全解析 + 回归。

## Rollback / kill-switch

全部为新增文件；回滚 = 删除分支。唯一触碰既有文件的点：`src/agent/CMakeLists.txt`（+2 对源文件）、
`tests/CMakeLists.txt`（+1 target）、`scripts/gen_lab_packs.py`（pack map +2）、`.gitignore`
（+1 re-include）——均可单文件 revert。

## Definition of Done

1. `sicnu.curriculum/1` schema + shipped manifest（10 模块/16 labs）落地且经三层门禁。
2. `CurriculumCatalog` + `CurriculumProgress` 公共 API 与本计划一致，全部 typed 行为有测试。
3. lab15/lab16 通过 test_labspec（算子+参数 drift）、test_lab_data_pack（pack drift）、
   check_lab_registry、gen_lab_docs 零 diff。
4. `tests/test_curriculum` 窄域绿色；test_labspec/test_lab_data_pack 回归绿色。
5. 可用性报告对 rs:infer 的声明诚实（declared_unavailable，非虚构）。
6. 深度 review 两轮通过，P0–P2 清零；与最新 master 动态去重后 PR。
