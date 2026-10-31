# R25-B ORACLES（可验证完成条件）

## O-1 链路图与最小实测（任务书 Oracle 1）
资产→模型→实验→复现资产流：从生产 importer（ProductAdapterRegistry/DataManager）→ DatasetStore → ModelCatalog/ModelExecutionService → tile engine → ExperimentStore/reproduction bundle 的实码链路图（DATA_INFERENCE_MAP.md），每条路径有 file:line 与最小实测（错误路径含 typed refusal 断言）。

## O-2 F1 导入预检（深化①）
- `test_r25_b_import_preflight`：master 上不存在（RED=链接失败/用例缺失）；新模块后 GREEN。
- 断言面：正例（GeoTIFF 带 CRS/NoData/角色）识别字段完整；重复导入预测命中已注册 asset；损坏文件→typed refusal + 具体原因；目录产品 completeness/missingConstituents；报告 `schema="sicnu.import.preflight/1"`、只读（预检后 DataManager 状态零变化——资产数不变）；JSON 可序列化且字段稳定。
- ×2 轮通过。

## O-3 F2 推理计划与质量摘要（深化②）
- `test_r25_b_inference_plan`：plan 干跑对正例给出瓦片网格/batch/像素预算/输出约束/警告；对 temporal 模型单输入→与 runModelInference 同一 typed refusal（契约门共享）；对 head 不兼容 outputMode→r23-B 契约谓词同判；**不写任何输出文件、不创建 session**（副作用=0 断言）。
- 质量摘要：从真实执行结果构造摘要（覆盖率、跳过 NoData、类分布占比、EO 预检存在性）；非法输入（空 stats）→typed 拒绝。
- ×2 轮通过。

## O-4 F3 复现差异原因（深化③）
- `test_r25_b_replay_deviation_v2`：构造 deviated run（model pin 差异）→ 每条差异带 `domain` 归属（model）与 `remediation` 非空可操作建议；dataset pin 差异→domain=dataset；environment drift→domain=environment 且不判 deviated（回归钉 #1547 语义）；v1 字段（deviations 字符串等）保持存在（向后兼容）；Identical verdict 无 remediation。
- ×2 轮通过。

## O-5 邻域回归
`ctest -R "r25_b_|r24_b_|r23_b_|test_data_manager|test_experiment_consistency_r4|test_model_runtime|test_dataset_core"` 基线绿 → 改后仍绿（无新增失败）。

## O-6 性能/内存（任务书 Oracle 5，≥3 组指标）
- F1 预检开销 vs registerSource（中位 5 次，fixture 小栅格）。
- F2 plan 干跑开销（不得接近真实推理；只读解析级）。
- F3 analyze v2 vs v1 开销（同一 store 双跑）。
- 每组附输出一致性判别器（同输入→同报告 digest）。共享宿主墙钟不设硬门槛，只报数据。

## O-7 安全不回退（Oracle 6）
- F1 不读远程（本地路径 only→远程引用在报告中标记 unsupported_remote 而非发起请求；offline 语义保持）。
- 路径遍历/SSI：预检报告不泄露工作区外内容（错误信息只有路径本身）。
- 既有 remote/SSRF 测试（test_io_remote_range/test_offline_mode）不回退。

## O-8 评审与 PR（Oracle 7）
- R1（fresh 子代理，全量 diff，path:line+严重度+复现）→ fix → 回归。
- R2（另一 fresh 上下文，复审 R1 修复+并行合并风险）→ fix → 回归。
- 注册完整性：`python scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs` exit 0。
- `git diff --check` 干净；push 分支；`gh pr create` 成功并给出 URL；不 auto-merge、不等 CI。
