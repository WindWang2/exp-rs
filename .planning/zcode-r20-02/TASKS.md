# R20-02 TASKS

## Work Package 状态
| WP | 名称 | 状态 |
|---|---|---|
| B01 | 数据流盘点 | IN_PROGRESS（审计子代理完成 dataset/data/experiment 全量走读）|
| B02 | 产品导入/格式可靠性 | UPSTREAM_FIXED（#1637-1/-2/-3、#1579 已修验证）|
| B03 | 范围缓存/网络安全 | UPSTREAM_FIXED（#1545 已修验证 + O2 回归）|
| B04 | 资产身份一致性 | IN_PROGRESS（A1 候选：canonical 空回退双身份 — 本轮降优先，记 RISK）|
| B05 | SQLite 原子持久化 | IN_PROGRESS（F5/F6 修复；A4 候选难确定性触发 → RISK）|
| B06 | 划分/防泄漏 | UPSTREAM_FIXED（#1582-#1586 验证）|
| B07 | 实验复现链 | UPSTREAM_FIXED（#1546/#1547/#1580/#1581 验证）|
| B08 | 块执行反压 | UPSTREAM_FIXED（#1553 验证 + chunk 家族回归）|
| B09 | manifest 契约 | IN_PROGRESS（F3 修复；F4 修复）|
| B10 | session 生命周期 | UPSTREAM_FIXED（#1554 验证）|
| B11 | 推理分块/发布 | IN_PROGRESS（F1/F2 修复；B 审计候选 5 记 RISK）|
| B12 | Python IPC 桥 | UPSTREAM_FIXED（#1542/#1543/#1555/#1556 验证）|
| B13 | 可观测/限额 | UPSTREAM_FIXED（#1603/#1604/#1605 验证）|
| B14 | 大样本/跨平台 | TODO（本轮 NOT_EXECUTED 声明：无 GPU/Windows/macOS）|

## 本轮修复任务（全部经本人实读源码核实，非仅信审计）
| ID | 缺陷 | 文件 | 触发案例 | 失败 oracle | 状态 |
|---|---|---|---|---|---|
| F1 | 边缘 tile 尺寸不一 → 混批硬拒绝，整次 run 失败（10 个随库 manifest 在非整除栅格上必败）| tile_inference_engine.cpp:1400-1406（累积 2136-2155）| tile 64/batch 4、100×100 栅格：末列 tile 宽 36 ≠ 64 → InvalidInputData | 同输入断言完整 100×100 输出（identity 模型逐像素）| RED→FIX |
| F2 | NoData 像素 uncertainty=0（最大确信）而非 NaN；类别带 NaN 恢复但 unc 带没有 | tile_inference_engine.cpp:1976-1992（单输入）/ 3752-3758（多输入）；headUncertainty 4215-4238 | 2 波段带 -9999 nodata 区域 + uncertainty=entropy | 断言 unc 带在 nodata 像素处为 NaN（当前 0.0）| RED→FIX |
| F3 | preprocess.interpolation 非词表 token 静默退化为 bilinear（违反 #646 fail-closed 类）| model_catalog.cpp:99（parse 无校验；对照 normalize 1062-1067 有校验）| manifest 写 "interpolation": "bicubic" | 断言 readiness==InvalidManifest 且 reason 提及 interpolation | RED→FIX |
| F4 | output.uncertainty 无 ≥2 通道头时静默丢弃声明（发布成功但无该带）| tile_inference_engine.cpp:1541-1554 / 3613-3627 | 单通道 identity + uncertainty:entropy → 成功但 0 不确定带 | 断言 typed refusal（InvalidInputData）| RED→FIX |
| F5 | 快照 DB 拷贝失败仍 report.ok=true（工程文件拷贝有检、DB 拷贝无检）| workspace_lifecycle.cpp:102 | chmod 000 治理库后 createSnapshot | 断言 ok==false 且快照目录清理 | RED→FIX |
| F6 | 对象池 put 撞损坏对象永久 nullopt（不自愈）+ rename 前无 fsync | artifact_object_pool.cpp:141,156 | put→外部截断对象→再 put | 断言第二次 put 返回有效对象 | RED→FIX |

## 降优先（记录于 RISK_REGISTER，不虚构修复）
- A1 canonicalFilePath 空回退双身份（symlink/大小写）— 修复设计牵 SourceKey 重索引，独立批次。
- A4 artifact_store/workspace_catalog 探测 prepare 失败当 fresh — WAL 下难确定性触发。
- A5 bundle 导入 metrics 先落库孤儿行（需 fault 点）。
- A6 createDataset TOCTOU 误码（数据无损）。
- B 审计候选 5（mask_threshold 阈值化 embedding 头）。
