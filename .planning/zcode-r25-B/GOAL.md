# R25-B GOAL

在既有 B 域（数据资产 / EO 模型推理 / 实验复现）内做**纵深深化**，不开辟新产品方向：

## 三项功能深化（每项：对外结构 + 稳定性 + 回归）

- **F1（W1）导入预检解释报告** `sicnu.import.preflight/1`：对任意本地源（目录产品/单栅格/多波段/损坏文件）产出机器可读预检报告——可识别的传感器/波段角色/CRS/时间/NoData/质量、身份可证明性与强度、重复导入预测（已有 assetId/复用）、歧义候选、不可继续的具体原因（typed refusal）。由生产 importer 代码路径（ProductAdapterRegistry + assetIdentityToken + DataManager dedup 前置查询）驱动，只读无副作用；D 轨 UI 与 CLI/Agent 均可消费。
- **F2（W4）可解释推理计划与质量摘要**：`planModelInference()` 干跑入口（模型解析→契约门→readiness→瓦片网格/批量/像素预算→输出模式×head 契约→EO 预检 advisory→警告），机器可读 `InferencePlan`；`InferenceQualitySummary` 类型化运行后摘要（覆盖率/跳过原因/类分布/EO 校验）。走 `model_execution_service` 同一 seam 的只读分支，不加新推理框架。
- **F3（W5）复现差异原因报告**：`ReplayDeviationReport` 升级为结构化差异条目（维度×领域归属[数据/模型/实验/环境]×可操作修复建议），schema v2 向后兼容（保留 v1 字段），verdict 语义不变（identity-only gate 不动）。

## Oracle（任务书 §7 映射）

见 ORACLES.md。核心：F1/F2/F3 RED→GREEN×2；邻域回归（r23_b/r24_b/data_manager/experiment/model_runtime）无新增失败；R1/R2 独立评审；push + PR。

## 非目标

- 不重做 #1716-#1723 已合并修复（清单见 BASELINE.md）。
- 不改 D 轨 UI（data_manager_panel 等）、C 轨沙箱、A 轨科学核。
- 不引入第二套 catalog / MLflow / 新推理框架。
- GPU/TRT/OV 真机：NOT_EXECUTED（stub 编译门除外）。
