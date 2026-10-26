# R23-B TASKS — 工作包与 slice 账本

状态机：PENDING → RED → FIX → GREEN1 → GREEN2 → REVIEWED。每 slice 一主题 commit。

## W1（#1576/#1577）provider TU 合并回归恢复 — 状态：GREEN（gate）×2 + 变异复验；待 ctest 注册运行确认
- 文件：src/operators/runtime/openvino_provider.cpp、src/operators/runtime/tensorrt_provider.cpp、tests/test_r23_b_provider_stub_compile.sh、tests/fixtures/provider_stubs/**、tests/CMakeLists.txt（append-only）
- RED（master TU stub-compile，g++ 16 -fsyntax-only）：OV `m_artifactPath undeclared`(:50) + `selectDevice not declared`(:60) + `const method locks non-mutable mutex`(:128)；TRT `m_artifactPath undeclared`(:148) —— 输出留档 RED_GREEN.md
- FIX：恢复 selectDevice/manifestPinsCpu/joinDevices（9b42bc93f7 语义，#1576 CUDA-缺失 GPU 降级修复）+ 删除残留赋值 + `mutable m_healthMutex` + TRT `artifactPath(){return mArtifact;}`
- GREEN：stub-compile 0 err ×2 轮；gate 反证：喂回 master OV TU → 3 err FAIL，恢复 → PASS
- commit ca1413a4ef

## W2（B5 残留）derived output.format × head role/confidence 前置门 — 状态：FIX 就绪（patch 暂存 /tmp/r23b_w2_engine.patch），测试就绪，批量构建排队（RED 态编译中）
- 文件：src/operators/runtime/tile_inference_engine.cpp、tests/test_r23_b_derived_output_roles.cpp（新）、tests/CMakeLists.txt（append-only）
- 契约：heads[0].role ∉ {segmentation,classification} → typed 拒绝；confidence distance 全拒、logit 仅 labels（argmax 保序）放行；无 heads[] 旧清单保持历史行为
- Oracle：不存在的输入路径区分门（preflight 先于 raster open）——master 上全部落入 "failed to open input raster"（RED）
- 验收：拒(role×3) + 拒(distance/logit-mask/logit-confidence) + 放行(legacy/logit-labels/legal) 三组用例

## W3（A5 残留）bundle 导入 metrics+run 单事务 — 状态：INVESTIGATE（见下）
- 现状：reproduction_bundle_import.cpp saveMetricRecord→upsertRun→失败补偿 deleteMetricRecord（补偿失败仅 warning；崩溃窗口孤儿行）
- 方案：ExperimentStore 增加 `upsertRunWithMetric(run, record)`（同一 SQLite 连接单事务：BEGIN IMMEDIATE → upsert run → save metric → COMMIT；任一败即 ROLLBACK）；导入器改调新 API，补偿路径删除
- 验收：注入 run 安装失败（如重复 id 冲突）→ metric 表零残留（RED：master 上孤儿行存在）；正常导入等价
- 风险：experiment_store.h 公共 API 扩展（B 白名单内）；需要仔细复用既有 upsertRun 校验/transition 逻辑，不复制第二套

## W4（#1637 sub-4）probeRemote 重定向私网盲区 — 状态：PENDING
- probe_remote.cpp:100-107 首跳私网检查 + policyFetch 进程策略在位，但 CPLHTTPFetch 自动跟随重定向，目标主机不再过检
- 方案评估中：redirectGuard 受控传输已存在（http_fetch.cpp），probeRemote 切换到 guarded transport 或禁自动重定向+逐跳校验
- 注意：无生产调用方（latent）；P3；时间受限则记 RISK_REGISTER 不强做

## 基线健康复验（O-B1 等价物）
- test_io_vector_contract（#1491 核验）、test_model_runtime、test_dataset_core、test_experiment_consistency_r4、test_r22_data_security_consistency、test_r21_b_end_to_end —— 批量构建后 -j1 串行实跑，结果入 TEST_RESULTS.md

## 非目标（明确不做）
- 重开 B01-B11 全量盘点型新架构（本轮按 LIVE 证据裁剪）
- 100k 合成样本压测（r21-B 已声明未建；本轮无新证据支撑其优先级，PERF 按既有族实测）
- 任何 src/processing、src/analysis、src/agent、src/app、src/sdk、pi/ 生产文件改动
