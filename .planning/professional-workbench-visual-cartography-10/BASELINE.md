# BASELINE — professional-workbench-visual-cartography-10

- **origin/master SHA**: `7d78059d1a6d316d606656759a506d17bc5e3b55`
  （Merge PR #958 prompt-command-hygiene-review；`git log --oneline -1 origin/master` 验证）
- **worktree**: `/home/kevin/projects/rs-studio/exp-rs-professional-workbench-visual-cartography-10`
- **branch**: `zcode/professional-workbench-visual-cartography-10`
- **configure**: `cmake --preset dev-default`（运行中/结果记 EVIDENCE.md Phase 0 节）

## 已合并 PR 去重分析（与本 track ownership 有交集）

| PR | 内容 | 与本 track 关系 |
| --- | --- | --- |
| #890 | Workbench 9.0（StateModel/命令权威/SchemaForm 5.0/分页/插件 UI） | 前序；不重做，只在其上扩展 |
| #889 | Cartography platform 9.0（solver 9.0/multi-page/export/explain） | 前序；compose/preflight/repair/export 引擎复用 |
| #888 系（9.0 四方向） | SAR/contracts/fabric/temporal | 不触 src/app/**；无冲突 |
| #958 | /goal 命令系统 review | 无代码交集 |
| #957 | whole-repo line review | findings 通过 review/DEDUPE.md 对照 |
| #956 | CN satellite product adapters | 算子域；不触 UI |
| #955 | spectral library priors | 算子域；VA 图表可消费其输出（非本 track 义务） |
| #954 | verification baseline green | 测试基建；遵守 |
| #953 | i18n zh glossary | UI 文案域；新 UI 沿用中文直书（现库现状） |
| #952 | lab copilot（tutor 约束） | Lab 投影相关；不重做 copilot |
| #951 | lab offline deploy | 无 UI shell 交集 |
| #950 | capability knowledge（111 rs 算子） | WP-F Processing UX 直接消费（只读） |
| #949 | data-driven LabSpec + guided workflows | WP-I Lab 投影复用其 LabSpec 加载 |
| #947 | lab report lineage | 无交集 |
| #946 | lab auto grading | 无交集 |
| #941/#915/#907 | TaskCenter 锁/预算修复 | TaskCenter 只调用不改 |
| #905 | workbench UI safety 修复 | 前序；marshal/scan 契约遵守 |
| #904 | geospatial IO hardening | 只调用 |
| #912 | asset preview 线程契约文档 | 预览复用 AssetPreviewService |

## 并行 10.0 Track（OPEN，本 track 开始时）

| PR | track | 冲突面 |
| --- | --- | --- |
| #976 | advanced-sar-polsar-insar-10 | 无 src/app 交集 |
| #975 | scientific-contract-verification-10 | 无 src/app 交集 |
| #974 | cloud-data-fabric-datacube-10 | 无 src/app 交集 |
| #973 | temporal-eo-phenology-change-10 | 无 src/app 交集 |
| （未开 PR）| large-scale-execution-engine-10 | worktree 已建（同基线）；TaskCenter/scheduler 域 —— 本 track 不改 scheduler，共享文件仅窄集成 |

## 历史 issues / findings 对照

- 五个 goal-prompt leads（#864/#865/#866/#867/#877）已在 cartography-platform-9 关闭复验
  （`.planning/cartography-platform-9/ISSUE_TRIAGE.md`）。
- `ISSUES.md` 开放缺口中归本 track：**C-1**（cartography 入管道）；T/S/H/C-2 其余归算子域 track。
- `review/DEDUPE.md` 新发现（F-OPS-1..5、F-PI-1/2）均非 src/app UI 域；不重复提交。
- `WHOLE_REPO_REVIEW.md`：60 行 dossier 索引；与本 track 相关条目已并入上表。

## 去重排除表（证明后动手）

| 候选 | 结论 |
| --- | --- |
| 第二套 global state / second command system | 禁止；扩展 WorkbenchStateModel/CommandRegistry |
| 自建 MapSpec compiler 副本进 UI | 禁止；GUI 经 `MapSpecCompiler`/`composition`/`quality` 既有权威 |
| 重做 wb9 的 enum provider/分页/marshal | 已存在；只复用 |
| 重做 agent→canvas 工作流投影 | `test_agent_canvas_sync` 已覆盖；只补反方向（context 投影） |
| 重做 dual viewport sync | `rs_dual_viewport_sync_controller` 已存在；N 视图 link 建立在 display manager 权威上并与该控制器收敛 |
| 重写 cartography export 原子化/确定性 | cartography-platform-9 已交付；operator 适配层只调用 |
