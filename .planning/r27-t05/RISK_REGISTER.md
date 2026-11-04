# R27-T05 Risk Register

| 风险 | 缓解 |
|---|---|
| tests/CMakeLists 重建遗漏某 target | 提取脚本断言 31/31；configure 通过；与各分支 tip 逐 target 比对 |
| #1744 语义变化破坏下游 rubric | data/ 无 linearWindow 用例；仅 2 个测试文件钉旧行为，已按 issue 授权更新 |
| #1776 allow-list 破坏既有 pipeline | data/pipelines 仅用 WORK/S2_SAFE_DIR/LANDSAT_MTL_OR_SCENE_DIR（全部在名单） |
| task_center 闸门误伤合法运行 | 闸门仅拒未解析 step/task ref（原样下发本身就是 bug）；既有 task_center 测试全量回归 |
| #1777 否定词误报（"cannot" 含 "not"?） | 边界对齐检查：token 前必须非词字符 |
| 构建资源与并行轨竞争 | 全程 flock + -j2；RAM 监控 |
| #1737 契约期望与 t01 轨改动交叉 | 只按 BASE_SHA 事实更新；PR 描述声明基准 |
