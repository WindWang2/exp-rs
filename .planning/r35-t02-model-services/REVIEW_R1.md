# REVIEW_R1 — R35-T02 首批切片

- reviewer：独立 general-purpose 子 agent（未主导实现，context 全新）
- 审查对象：工作树实际 diff（4 个修改文件 + 7 个新测试/fixture 文件），base=120677e6
- 方式：读全 diff + 逐 checklist 项核源码（readLine 循环路径、registry waiter/取消路径、P1-8 flush 顺序、全仓 knob 声明扫描、JoinGuard 异常安全、CMake guard 块归属）
- 结论：**approve**，0×P0/P1/P2，6×P3

## 发现与处置

| # | severity | 发现 | 处置 |
|---|---|---|---|
| 1 | P3 | readLine deadline 边界先于消费已缓冲字节（毫秒级收紧） | 有意选择：绝对帧 deadline 语义，代码注释已写明；不改 |
| 2 | P3 | 错误类型诊断未写明 declared 类型 | **已修**：declared token 命名（string/bool/array/object，P1-8 idiom） |
| 3 | P3 | null 宽容与 max_concurrent_members（拒 null）不对称 | 有意选择（DECISIONS#7，测试 r35t02-hist-null 钉住）；不改 |
| 4 | P3 | docs/models/model-manifest.md 未写 typed 语义 | **已修**：provider 段补 typed/默认/程序化域说明 |
| 5 | P3 | ensemble 测试 arming sleep 的 seam 确定性（chain-top vs cv-poll） | 合同断言两 seam 均满足（有界退出+Cancelled+c 不启动）；不改，注释已述 |
| 6 | P3 | max_body_mb 上限 INT_MAX 而字段为 long | 有意选择（与 HTTP twin、错误消息一致、256MB 默认量级）；不改 |

reviewer 复核建议（未构建/未跑，静态审查）：重复跑新测试与回归、stash 复验 RED、额外跑 test_provider_fallback / test_model_manifest7 / test_r33_t02_wire_admission。
本会话处置：RED 已在本会话 pristine HEAD 实跑取得（见 LEDGER 第 2 轮）；GREEN 28/28 已实跑；本轮修复后重建将加跑上述三个额外回归 + 三新测试二跑（protocol"关键验证跑两遍"）。
