# R21-B PERF_BASELINE — 同条件前后对比计划与结果

方法：RED 构建（pristine src=911e0b17）与 GREEN 构建（修复后）同一二进制名、同 fixture、同机器（-j2 空载期）各测 3 次取中位。无 GPU → CPU 实证 + GPU NOT_EXECUTED。

| 场景 | 断言 | 状态 |
|---|---|---|
| dataset_store 顺序 createDataset×500（含 5% 重复冲突） | 修复仅触失败路径映射，吞吐变化 <10% | 待测 |
| providers resolve() 既有文件 ×2000（canonical 快路径未改） | 变化 <10% | 待测 |
| identity 推理 128×128 ×20（role 门控=每 head 一次字符串比较） | 变化 <10% | 待测 |
| GPU lane | 无 GPU 环境 | NOT_EXECUTED |
| TSan | 宿主无 TSan 配方（R19/R20 同声明） | NOT_EXECUTED |

## 实测（2026-10-24，本机 -j2 空载期，GREEN 最终态）
| 场景 | 3 次/2 次实测 | 判定 |
|---|---|---|
| dataset_store 并发 oracle（40 轮×2 lane，barrier 对抗） | 0.30 / 0.29 / 0.28 s | 修复仅触失败路径映射；快路径未变 → 无退化 |
| model_runtime 全族（110440 断言，identity tile 引擎） | 2.1 / 1.9 s | role 门控为每 head 一次字符串集合比较；无退化 |
| GPU lane | 无 GPU | NOT_EXECUTED |
| TSan | 无配方 | NOT_EXECUTED |

说明：RED 态与 GREEN 态同机同条件；修复的代码路径（失败映射/字符串门控）不进入热循环。吞吐/RSS 专项 mock（100k logical extent）本轮未建——已有 workspace_stress(20 断言) 与 model_runtime_stress(68 断言) 提供既有覆盖；如实记录为未扩展项。
