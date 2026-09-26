# SURFACE_PARITY — CLI / MCP / GUI 三面对照表（Track 14 WP-D）

判定原则：**以实现真值为准**（同一 registry/store/协调器的落盘效果），不采信任何一侧的文档声明。
取证：`THREE_SURFACE_FACTS.md`（MCP/GUI 侧，带 文件:行号）+ 本仓库 CLI 源码（BASELINE §4/§5）。
自动化断言：`tests/test_surface_parity_r4.cpp`（≥10 条，断言 CLI 侧可观测行为与三面共真源一致；GUI 侧为人工核对记录，见 §3）。

## 1. 共真源锚（三面"同源"的架构证据）

| 真源 | CLI | MCP | GUI |
|---|---|---|---|
| AtomicAlgorithmRegistry | `run`/`algorithms` | execute_algorithm 族 | JobEngine 对话框/batch |
| RSOperatorRegistry | --list/--schema（legacy） | execute_operator 族 | 算子目录面板 |
| DatasetStore/ExperimentStore | dataset/experiment/reproduce | dataset:*/experiment:* | workbench 面板 |
| WorkflowRunCoordinator+TaskCenter | workflow/pipeline run/resume | run/resume_workflow | WorkflowSessionController |
| WorkflowRunCoordinator.checkpointDirectory | workflow list-runs | get_workflow_status | pipeline dock |

## 2. 操作对照（16 行；漂移=三面参数名/默认值/错误口径不一致）

| # | 操作 | CLI 形态 | MCP 形态 | GUI 形态 | 漂移裁决（实现真值） |
|---|---|---|---|---|---|
| 1 | 算法列表 | `algorithms list`（无分页，全量） | list_algorithms limit=50,cursor=0(clamp 1-500) | 算子目录面板全量 | **默认值漂移**：CLI 无分页 vs MCP 50/页——共享 registry 真值一致，分页为 MCP 传输层语义，**记录不改**（改 CLI 加分页=新功能，越界） |
| 2 | 算法搜索 | `algorithms search [text] --group --tag --purpose --task --modality --input-type --output-type --large-raster-safe --limit --cursor` | search_algorithms query/group/tag/purpose/task/modality/input_type/output_type/large_raster_safe/limit/cursor | 面板搜索框（query 语义） | **参数名风格漂移**（`--input-type` vs `input_type`）：CLI kebab vs MCP snake，为各自面惯例；过滤语义 AND/ANY-of 两面一致（同引擎）→ 一致 |
| 3 | 算法 schema | `algorithms schema <id>`；未知→**5**+[E-5] | get_algorithm_schema；未知→isError 无 code | 算子帮助面板 | **错误口径漂移**：MCP data 面 fail() 抛 runtime_error 无结构化 code（data_platform_tools.cpp:139）——落点在 MCP 侧，本轨道只读不改，移交 |
| 4 | 算法执行 | `run <id> --param k=v --params-file f`；未知→**5**；参数坏→**6**；执行败→**3**；取消→**4** | execute_algorithm algorithm_id+parameters{}；异步 execution_id | rs.* 对话框（JobEngine 同步） | 执行模型差异（同步/异步/JobEngine）为面语义；参数命名 k=v vs object 为惯例；**退出码/错误码合同只有 CLI 有**→ CLI 侧为本轨道收口面 ✓ |
| 5 | 工作流启动 | `workflow run <file>`；schema 坏→**2** | run_workflow pipeline(str/obj)；拒绝→INVALID_PIPELINE；seed<0→INVALID_ARGUMENTS | workflow.run（编辑器收集参数） | 错误口径：CLI 2 vs MCP INVALID_PIPELINE——语义同类、编码不同（MCP 有 errorCode、CLI 有 exit class），**记录对照不改码**（跨面统一错误码是独立工程） |
| 6 | 工作流恢复 | `workflow resume <id>`；失败→**3** | resume_workflow run_id ^[A-Za-z0-9_.-]+$ 禁..；失败→RESUME_FAILED | WorkflowSessionController resume | run_id 字符集校验：MCP 显式拒绝非法字符（INVALID_PARAMETER）；CLI 侧 resume 走 runner 错误→**3**——CLI 对非法字符无 parse 期校验，**已记 WP-E 遗留**（runner 内部处理，非静默） |
| 7 | 运行状态 | `workflow list-runs`（本机 checkpoint 目录） | get_workflow_status pipeline_id(int) | pipeline dock 状态列 | 形态漂移（列表 vs 单查）：语义互补非冲突，同源 TaskCenter→一致 |
| 8 | 数据集列表 | `dataset list --dataset-db`；缺 db→**6** | dataset:list dataset_db 必需；缺→isError "dataset_db is required" | workbench 面板（用户选库） | **参数名漂移**：`--dataset-db` vs `dataset_db`（kebab vs snake 惯例）；缺失错误 CLI=6+[E-6:INVALID_INPUT] vs MCP isError 无 code——CLI 侧已合同化 ✓ |
| 9 | 数据集检查 | `dataset inspect`；version 不存在→**5** | dataset:inspect / dataset:version | 同面板 | 资源不存在语义一致（not found）；错误编码 CLI 5 结构化 ✓ |
| 10 | 数据集校验 | `dataset validate`；校验败→**2** | dataset:validate | 同面板 | 一致（validation 失败同类；CLI=2 有诊断载荷） |
| 11 | 数据集统计 | `dataset stats --dataset <id>`；坏 id→**6**、不存在→**5** | dataset:stats | 同面板 | 一致 |
| 12 | 泄漏审计 | `dataset leakage --split <id>`；无报告→**5** | dataset:leakage_audit | 同面板 | 参数名 `--split` vs leakage_audit（子命令名本身不同：leakage vs leakage_audit）——**记录**：CLI 词表（usage/doc）与 MCP 工具名各自成立，不属同参数漂移 |
| 13 | 实验对比 | `experiment compare --a --b`；run 不存在→**5** | experiment:compare | experiment_studio | 一致 |
| 14 | 复现导出 | `reproduce export --run --out`；缺参→**6**（store 不创建，WP-E） | reproducibility:export | experiment_studio 导出 | 一致（缺参 parse 期拒 + 副作用零） |
| 15 | 会话驱动 | `session <action>`；未知 action→**6**+全词表 | scientific:agent_session action 必需；缺→INVALID_PARAMETER | agent copilot | 一致（动词集同源 OpsDriver；CLI usage 列 14 动词含下划线别名） |
| 16 | 资产护照 | `passport --path` | data:asset_passport | （GUI 无独立面板——经 cartography 导出侧） | N/A 记录：GUI 侧无对应入口 |

## 3. GUI 侧人工核对记录（非自动化，逐条记载）

- G1：`layer.addRaster/addVector`（command_defs.cpp:124-134）对应 CLI 无直呼（数据导入经 `project import --remote` / `data stac`）——面差异记录。
- G2：`rs.spectralIndex` 等算法命令（command_defs.cpp:482-569）→ 打开对话框收集参数→JobEngine→AtomicAlgorithmRegistry：与 CLI `run` 同真源 ✓（对话框默认值如 clip=2.0 为 GUI 层参数，CLI `--param` 显式传入，无默认值冲突面）。
- G3：`workflow.run/stop`（:615-633）→ WorkflowSessionController.startTrackedPipeline（workflow_session_controller.cpp:461,518）= CLI workflow run 同一协调器 ✓。
- G4：`teaching.labCockpit.show`（:805-811）↔ CLI `lab`：GUI 是查看台、CLI 是评分驱动，语义互补 ✓。
- G5：`workbench.datasetExperiment`（:294-302）↔ CLI dataset/experiment：同 DatasetStore/ExperimentStore 真源 ✓。

## 4. 结论

- 16 对照中：11 判"一致（共享真源 + 已合同化错误口径）"，3 判"惯例差异记录在案"（#1 分页、#2/#8 参数名风格），2 判"MCP 侧结构化缺口移交"（#3 错误码、#6 run_id parse 期校验）。
- 本轨道只改 CLI 侧与文档侧（白名单内），未触碰 MCP/GUI 实现 ✓（Standards 轴）。
