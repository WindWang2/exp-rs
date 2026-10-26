# R21-B ORACLES — 硬性完成判据

## O-B1 R20 保护
F1–F6 + R2-1 测试两遍绿（model_runtime / model_catalog_v2 / workspace_services / fault_injection）；#1608/#1609 行为复验。

## O-B2 端到端
M5 fixture ≥1 组（导入→split→重放→推理→发布→lineage），失败 typed、资产无半成品。

## O-B3 并发
G4 对抗测试稳定（连续 3 次运行全绿）；无超时替代 PASS。

## O-B4 性能
受影响路径有前后对比（dataset 并发吞吐 / 推理小基准）；无 GPU → CPU 实证 + GPU NOT_EXECUTED 声明。

## O-B5 兼容
旧 schema/DSB 打开不退化；credential redaction 复验；同配置输出 hash 一致（F1 类回归已含）。

## O-G 通用
O-G1 盘点动态可查；O-G2 每修复 RED/GREEN×2；O-G3 性能同条件基线；O-G4 双 reviewer；O-G5 skip/not-executed 逐项归因；O-G6 PR diff 100% 白名单；O-G7 无假实现。
