# feat(lab): temporal, SAR, hyperspectral and cartography lab tracks

## 摘要（D3 · Lab Content Expansion）

平台有 111 个 `rs:` 算子，本科实验只有 7 个。本 PR 把平台未进课堂的四块能力翻译成
可教学、可 headless 复现的实验（实验 8–11），每 lab 交付六件套：
**LabSpec（`sicnu.labspec.v1`）+ 中文原理讲解 + 数据需求规格（供 D1）+ 判分意图
（供 D4，不实现判分器）+ headless 管道 + 思考题/预期结果**。

| Lab | 主题 | 算子链 | 文档 |
| --- | --- | --- | --- |
| 实验8 | 时序（趋势/物候/异常） | `rs:temporal_summary` → `rs:temporal_index_series` → `rs:temporal_trend` → `rs:temporal_phenology` → `rs:temporal_anomaly`×2 | [lab8](docs/labs/lab8_temporal_analysis.md) |
| 实验9 | SAR（压斑+变化检测，#785/#803 教学） | `rs:sar_calibrate`×2 → `rs:sar_speckle`×3 → `rs:sar_change` → `rs:sar_terrain_correction` | [lab9](docs/labs/lab9_sar_processing.md) |
| 实验10 | 高光谱（MNF/PPI/SAM-SID/解混） | `rs:mnf` → `rs:endmember_extraction` → `rs:sam_classify`(sam+sid) → `rs:spectral_unmixing` | [lab10](docs/labs/lab10_hyperspectral_analysis.md) |
| 实验11 | 制图出图（专题生产+合规排版） | `rs:temporal_composite` → `rs:threshold_raster` + MapSpec v5 + `cartography:compose/export` | [lab11](docs/labs/lab11_cartographic_mapping.md) |

## 交付物布局（diff 全部在 data/labs/、docs/labs/、scripts/、tests/）

- `data/labs/labspec.schema.json` — LabSpec v1 JSON Schema。**D2 尚未合入**（`labspec.schema.json`
  不在 master，D2 分支也无 schema 文本）；按 track Autonomy default 7 的兜底条款由本 track
  定义最小 v1 并标注"pending D2 alignment"，D2 合入后以其为准迁移（DECISIONS D002）。
- `data/labs/*.labspec.json`（4）— 全部通过 schema 校验；`operator_id` 全部在
  `rs_operators_init.cpp` 注册（并新增 `tests/test_lab_chains` 在运行时断言 registry 可解析）。
- `data/labs/pipelines/*.pipeline.json`（4）— headless 数据链（`pipeline_schema.json` 格式）。
- `data/labs/data-specs/*.json`（4）— D1 数据需求（离线可得，含验收标准与判分辅助码图）。
- `data/labs/grading/*.intent.json`（4）— 判分意图（容差已按 fixture 模型数值重校准，Phase 6 评审后）。
- `data/labs/mapspecs/lab11_thematic_map.mapspec.json` — MapSpec v5 合规专题图文档。
- `data/labs/spectral-library/lab10_sicnu_library.json` — `sicnu-spectral-library` v1 光谱库
  （fixture 生成器以它为唯一真值源；判分 H6 钉死管道 refs 与库零漂移）。
- `scripts/gen_lab_fixtures.py` — 确定性 fixture 生成器（仅写入 gitignored `data/labs/_tmp/`，
  时序数据遵守 ≤12 期/≤512×512/8-bit-scaled float32 上限）。
- `scripts/run_lab_pipelines.py` / `scripts/verify_lab_outputs.py` — 离屏运行 + 判分意图复核。
- `scripts/export_lab_map_mcp.py` — 实验11 排版导出（GUI 二进制 `--mcp` stdio 协议）。
- `tests/test_lab_chains.cpp` + CMake — 独立测试目标：管道 operator_id 注册解析、LabSpec
  工件一致性、实验11 MapSpec validate + 合规 preflight（五要素）+ 离屏编译。**刻意不挂到
  `test_mapspec` 聚合**（该套件为红、由 D10 负责，见 `docs/verification/READINESS.md:38`）。
- `ISSUES.md` — 算子缺口登记（T1–T3 / S1–S2 / H1–H3 / C1–C2）；`src/operators/` 零改动。
- `docs/labs/README.md` — 索引实验 8–11 + headless 验证说明。

## 科学正确性保障（Phase 6 对抗评审，≤2 只读子代理）

- **科学性镜头**：无 P0——所有算子语义声明逐一对照 C++ 源码核验（趋势斜率按天、物候波段、
  异常基线规则、σ0=(DN²−noise)/A²、Lee 保均值、#785 heading±90、#803 逐波段 NoData、
  MNF 信噪比排序、PPI 确定性 RNG、SAM/SID 语义、解混约束、MapSpec v5 治理语义）。
  评审发现 6×P1「容差与 fixture 模型不自洽」，已全部按数值重算重校准并提交
  （c52cd98e7f）：基线窗口改为扰动前生长季、控制时相改为「无连片负异常」判据、
  Otsu 掩膜语义改为「有植被覆盖占比 80–95%」、PPI 水体端元角度容差按 SNR 重算。
- **教学性镜头**：md↔LabSpec 零漂移逐项核验通过（含 T7 补录、PPI 术语表、K2 笔误等修复）；
  H6（管道 refs == 光谱库）机器验证。
- 评审记录：`.planning/lab-content-expansion/REVIEW_LOG.md`（P0=0，P1=0 已修复）。

## 诚实范围（typed refusal 风格，逐 lab 声明）

- 平台无 CCDC/BFAST 类联合模型、无 SAR 极化分解、`rs:sar_change` 严格双时相、
  `rs:temporal_monitor` 只吃 workspace collection、无 MNF 反投影、光谱库无 `libraryPath`
  参数、PPI 结果 JSON 无法管道内直连、制图 compose/export 不是管道算子——
  全部在 lab 文档「诚实范围」+ `ISSUES.md` 声明，实验只教可执行子集。
- 实验9 把 #785（视角几何）与 #803（多波段 NoData）两个**已修复缺陷**做成回归性教学断言
  （判分 S4/S5）。
- 实验11 声明依赖：平台级 `test_mapspec` 为红由 **D10** 修绿；本 lab 断言自包含。

## 本地验证（无 CI 依赖；`CMAKE_BUILD_PARALLEL_LEVEL=2`、Ninja `-j2`、`QT_QPA_PLATFORM=offscreen`）

- 4 条管道离屏跑通 + 判分意图复核（数值证据见 `.planning/lab-content-expansion/EVIDENCE.md`）。
- `ctest -R test_lab_chains -j1` 离屏通过。
- 资源日志：`logs/resources.log`（60 s 采样；GCC 16.2.1 在 qgis_core 两个 TU 上的瞬时 ICE
  记录于构建日志，单 TU 重编后恢复）。

## 判分

本 track 只交付判分**意图**（`data/labs/grading/*.intent.json`，`grader_owner: D4`）；
判分器由 D4 实现。`test_lab_chains` 内含意图文件的幂等断言（结构、D4 归属、断言数下限）。
