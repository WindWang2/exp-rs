# DEDUPE — 与既有 dossier / 已闭环 issue 的逐条对照

**基线**（见 `.planning/whole-repo-line-review/DECISIONS.md` D-002）：
- `gh issue list --state all`：250 个 issue（#595–#945，全部 CLOSED）→ `DEDUPE_BASELINE_ISSUES.txt`
- 历史审计 findings：`.scratch/audit-final/findings.md`（F-001..F-023）、`.scratch/findings-audit.md`（F-101..F-106）、`.scratch/audit-final/issue_dedupe.md`
- `PROJECT.md`（#773–#817 全 DONE，含 45 项契约表）、`.agents/ORIGINAL_REQUEST.md`（#848–#882 三批次、PR #837–#847）
- 指名的 `AUDIT_DOSSIER_ISSUES_747_760.md` / `PROJECT_REVIEW_DOSSIER_5.0.md` 是未跟踪历史文件（检出中不存在，见 BASELINE.md 引用）；其产物即 #747–#760 issue 批次，已包含在 250 条基线内。

## 新发现对照表

| Finding | 对照批次/条目 | 结论 |
|---|---|---|
| **F-OPS-1** class_mapping 产品类值未按输出编码校验（P2） | #646（declared-but-unenforced 清扫，先于该字段）、#878（渲染端容器规则）、#632/#705（dtype/输出校验族） | **new**——class_mapping 为 Platform 8.0 WP-E 新增，其单射/非负校验已有，编码上界缺口无任何条目涉及 |
| **F-OPS-2** TensorBlob::fromMat ND 非连续回退空转（P3） | #896/#925/#926（IPC 帧）、#895（docs） | **new**——tensor_blob 为 Platform 7.0 N-D 面新增，无先例 |
| **F-OPS-3** rs:qa_mask fail-open（P2） | #719（temporal point-extract QA fail-open，另一文件）、#612（threshold_raster NoData→clear）、#699（该处 UB 修复，明确"保留历史 clear 结果"）、#665 | **new**——同族不同文件/行为；#699 的注释证明该语义系有意保留，本发现针对该语义本身 |
| **F-OPS-4** io:reproject srcCrsOverride 死参数（P1） | #903（geospatial IO hardening：range_cache 锁/python worker 上限/时间戳）、#880（io:inspect schema drift）、#637（VRT provider）、#808–#811 | **new**——subagent V 补强证据：同文件 io:clip 对同名参数做了功能性消费（io_operators.cpp:383-386），证明是漂移而非设计 |
| **F-OPS-5** 检测 NMS O(n²) 且不可取消（P2） | #620（vector_inspect max_features unbounded）、#701（harness uncapped results）、#690（int32 argmax） | **new**——检测引擎 max_detections 语义（拒绝线 vs NMS 预算）与取消注入缺口未被覆盖 |
| **F-PI-1** pi 桥失步后不拆流（P2） | #623/#645/#669/#706（泄漏/取消/respawn/测试） | **new**——#645/#669 的修复均在位；失步僵尸态为其后残余缺口 |
| **F-PI-2** startup-deadline 修复未回移 exp-rs-spatial.ts（P2） | #645（exit 监听器堆叠——不同问题）、#918（planning 记录未提交） | **new**——#669/#706 抽取 mcp_bridge.ts 时引入的单向修复漂移 |

## 已审但判"不构成发现"的候选（防重复提交记录）

| 候选 | 撤下理由 | 记录处 |
|---|---|---|
| `ModelCatalog::resolve()` 绕过 mUnregistered | `unregister()` 无生产调用方，不可达 | REVIEW_LOG（operators C&D） |
| `rs_feature_normalize` histogram span==0 的 NaN→int UB | 结果立即 clamp，良性 | REVIEW_LOG |
| `bit-exact`/`bit_exact` 双词汇 | 两个独立表面，无交叉投喂点 | REVIEW_LOG |
| IR-MAD 权重帧缺 2³¹ 守卫 | bad_alloc 路径有类型化表层 | REVIEW_LOG |
| history F-001..F-023 / F-101..F-106 的全部条目 | 已在 2026-08 批次闭环或明确 NOT_REPRODUCIBLE/FALSE_POSITIVE，本次抽查其修复状态均在位（如 rs_classification_split、rpc transformer） | 本表 |
