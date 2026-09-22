# LabSpec 创作指南

LabSpec 是实验的声明式规格：一份 JSON 同时驱动**引导式实验面板**、**headless 执行**
（`{operator_id, params}` 与 headless 调用同一条 `JobRequest` 路径）、**自动判分**
（`grading_ref` → `data/pipelines/*.json`）与**实验文档**（`scripts/gen_lab_docs.py`
生成，禁止手改 `docs/labs/` 下的自动生成页面）。本页面是唯一允许手写的 `docs/labs/`
文档（ADR 0146）。

## 文件布局

| 路径 | 内容 |
|------|------|
| `data/schemas/labspec.schema.json` | JSON Schema（draft-07），LabSpec 的规范性契约 |
| `data/labs/<id>.lab.json` | 每个实验一份，`<id>` 必须等于文件名主干 |
| `docs/labs/*.md`、`docs/labs/README.md` | 生成产物：`python3 scripts/gen_lab_docs.py` |

`data/*` 默认被 gitignore（大栅格），仅 `data/labs/`、`data/schemas/` 等配置目录被
显式 re-include；实验输入按 `data/samples/...` 相对路径引用即可。

## 最小示例

```json
{
  "spec_version": 1,
  "id": "lab02_spectral_analysis",
  "title": "Spectral Analysis",
  "title_zh": "光谱指数与波段运算",
  "objective": "掌握多光谱影像的光谱特征分析方法……",
  "prerequisites": [
    { "path": "data/samples/landsat_sample.tif", "note": "7 波段样本影像" }
  ],
  "steps": [
    {
      "title": "Load Sample Data",
      "title_zh": "加载样本数据",
      "description_zh": "通过 File > Add Raster Layer... 加载样本影像。",
      "action": "addRasterLayer",
      "completion_hint": "影像已显示在地图画布上。"
    },
    {
      "title": "Calculate NDVI",
      "title_zh": "计算 NDVI",
      "description_zh": "执行 rs:spectral_index 算子计算 NDVI。",
      "operator_id": "rs:spectral_index",
      "params": {
        "input": "data/samples/landsat_sample.tif",
        "output": "outputs/lab02_ndvi.tif",
        "index": "NDVI", "red": 4, "nir": 5
      },
      "teaching_note": "健康植被 NDVI > 0.3，水体 < 0。",
      "completion_hint": "植被区域呈高值。"
    }
  ],
  "grading_ref": { "pipeline": "data/pipelines/landsat_ndvi.json" },
  "thinking_questions": ["为什么植被 NIR 高、Red 低？"]
}
```

## 字段说明

| 字段 | 必填 | 说明 |
|------|------|------|
| `spec_version` | ✓ | 合同版本：`1` 或 `2`（见下节） |
| `id` | ✓ | `^lab[0-9]{2}_[a-z][a-z0-9_]*$`，等于文件名主干；`labNN` 零填充保证排序稳定 |
| `title` / `title_zh` | ✓ | 英文 / 中文标题 |
| `objective` | ✓ | 实验目标，同时作为面板中的描述 |
| `prerequisites[]` |  | `{path, note?}`，实验数据引用（相对仓库根） |
| `steps[]` | v1 ✓ / v2·v3 可省 | v1 至少一步，字段见下；v2/v3 可省略（操作序列由流水线定义，ADR 0166——lab12–lab14 即此形态）；若给出则与 v1 同规则校验，且不得为空数组 |
| `grading_ref` |  | `{pipeline}` 指向既有判分管线；引用会被 drift guard 校验 |
| `thinking_questions[]` |  | 思考题，渲染进文档 |

### 步骤字段

| 字段 | 说明 |
|------|------|
| `title` / `title_zh` / `description_zh` | 必填；`description_zh` 是教学正文 |
| `operator_id` | 算子绑定：`rs:*` 或 `opencv:*`，必须能在 Processing Registry 解析 |
| `params` | 算子参数对象；仅能与 `operator_id` 同时出现（缺省视为空对象），键名/取值受算子 schema 校验 |
| `action` | UI 动词：主窗口槽名（如 `addRasterLayer`）；与 `operator_id` 互斥 |
| `teaching_note` | 原理说明（渲染为文档引用块） |
| `completion_hint` | 完成标志 |

三者取其一：算子步骤（`operator_id`）、界面步骤（`action`）、手动步骤（两者皆无）。

## LabSpec 2（spec_version: 2）

LabSpec 2 是 v1 的**严格超集**（lab platform 12.0）：所有 v1 字段原样有效，另新增七个可选
结构化字段。加载器同时接受 1 和 2，并**拒绝在 v1 文档中出现 v2 专有键**（版本必须显式声明，
杜绝静默语义漂移）。v2 字段是创作/契约数据：文档生成器、判分工具与 drift 测试直接消费 JSON；
运行时 `LabSpec` 结构体保持 v1 形状，所有消费方零改动。

| 字段 | 说明 |
|------|------|
| `objective_zh` | 中文教学目标（多行）；`objective` 保持原语义 |
| `prerequisite_knowledge[]` | 先修知识/实验（字符串列表）；`prerequisites[]` 仍专指**数据引用** `{path, note?}` |
| `principles[]` | `{heading, body, formulas?[]}` 结构化原理块（渲染进生成文档） |
| `glossary[]` | `{term, term_zh, definition_zh}` 中英术语表 |
| `expected_artifacts[]` | `{path, kind?: raster\|vector\|file, note_zh?}` 完成实验应产出的成果 |
| `param_ranges` | `算子id → 参数 → {min?, max?, values?, note_zh?}` 教学参数允许范围（`min<=max` 由加载器校验；运行时权威仍是 Processing Registry 的参数 schema） |
| `grading_rules` | 判分规则文件路径（`data/labs/grading/<id>.rules.json`），即 `lab --lab <id> --grade` 实际读取的 `sicnu.lab.rules/1` 文件 |

迁移：`python3 scripts/upgrade_labspec.py [--check]` 把 `data/labs/*.lab.json` 提升到
v2（文本级 `spec_version` 替换，不重排格式）。历史遗留的 D3 单数字命名文件
（`lab8_temporal_analysis.lab.json` 等）与严格加载器的 `labNN` 契约不兼容，已移入
`data/labs/legacy/` 存档；对应教学文档（`docs/labs/lab8…11`）保留为手写页面。

## 路径约定

- **输入**：以 `data/` 开头的参数值在提交时解析为运行时绝对路径
  （`SICNU_DATA_DIR` → 工程根回溯）。
- **输出**：以 `outputs/` 开头的参数值解析到 `output/labs/<labId>/<名>`，
  目录在执行前自动创建（该目录已被 gitignore）。
- 其余字符串（表达式、枚举值）不做路径改写。

## 创作与校验流程

1. 新建 `data/labs/labNN_slug.lab.json`（复制上方最小示例）。
2. 算子与参数：对照 `src/operators/rs/*_operator.cpp` 中 `schema()` 的端口定义，
   或直接参考对话框 `runOperatorTask(...)` 提交的参数形状。
3. 自检：`python3 -c "import json,jsonschema;jsonschema.validate(json.load(open('data/labs/<id>.lab.json')),json.load(open('data/schemas/labspec.schema.json')))"`。
4. 重新生成文档：`python3 scripts/gen_lab_docs.py`；零 diff 检查：
   `python3 scripts/gen_lab_docs.py --check`。
5. 跑 drift guard 与行为测试（offscreen）：
   `ctest -R "test_labspec|test_guided_workflow_widget" -j1`。

违反 schema 的实验会在引导面板中呈现为**类型化错误条目**（附原因），绝不会静默回退到
内置内容——C++ 中不存在 fallback 实验。
