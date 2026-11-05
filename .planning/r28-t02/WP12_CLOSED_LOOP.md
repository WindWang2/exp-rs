# WP12 底座压力与故障闭环 — 执行计划

## 场景 A：批量体检→部分导入→取消→恢复→分析/推理→写出→重开（真实用户流程）

1. 6×真实 GTiff fixture → `ImportPreflight::inspectBatch`（批次体检 JSON：counts+suggested_action）
2. ImportSession（journal）stepwise 驱动 → 第 2 项后 queued cancel（复用 driveStepwise）
3. resume() → 完成其余项；`verifyFacts()` 无警告（文件未动）
4. 对已导入资产跑真实推理（WP10 identity ONNX，OpenCV DNN CPU）：plan→infer→写出
5. 产物 REOPEN 校验（几何+像元）；catalog findByPath 与 journal 一致（G2 证据）
6. 故障注入变体：目录替换（rewrite 源文件）/journal 失败（只读路径）/cache 损坏（写坏 cache 块文件后重读 typed 失败）

## 场景 B：多任务排队→取消/超时/关机

1. TaskCenter 提交 N 个 mixed 任务（exclusive/normal/低优先）→ 中途 cancelPipeline
2. watchdog 缩短（setCancelWatchdogMs 小值）+ 永不 poll 的算子 → finalize Canceled（typed reason 保留）
3. JobEngine shutdown(deadline 注入 250ms) + stuck worker → stuck 计数=返回值 + trace 事件（WP02 增量）
4. 断言：终态恰一次/迟到 cancel 不改写成功/无半成品发布

## 资源上界（按契约）

- 全程 -j2/-j1；场景 A/B 的 RSS 由 throttleWait 有界（#1603 已修）+ telemetry 记录；压测有明确 maxSteps/预算上限；禁止生产数据。

## 当前缺口→落点

- 基线取消簇复现（WP01）先行；A/B 场景并入 test_r28_t02_* 套件 + 一个 RUN_SERIAL 压力 case。
