# ADOPTION_GUIDE — 让算子采用统一执行底座（Execution Runtime 11.0）

目标读者：`rs:`/`io:` 域算子的维护者（各域 track）。本 track 交付的底座是**纯增量**的——不采用它的一切现有算子行为不变。

## 30 秒接入

```cpp
#include "operators/framework/chunked_run.h"

Json::Value MyOperator::run( const Json::Value &params, RSOperatorContext &context ) override
{
    // 1) 声明分区（partition digest 自动进入 run identity → resume/出版门自动正确）
    sicnu::runtime::chunk::TileRunPartition partition;
    partition.rasterWidth = width;  partition.rasterHeight = height;
    partition.tileWidth = 256;      partition.tileHeight = 256;
    partition.halo = myKernelRadius();
    partition.bands = bandCount;

    // 2) 每_tile 核：收到含 halo 的 TileSpec，返回 bufferElementCount() 个 float
    auto kernel = [&]( const sicnu::runtime::chunk::TileSpec &s ) {
        return computeOneTile( s );   // 你的科学内核
    };

    // 3) 每_tile 消费：按 index 顺序收到已验证的 payload（resume 时来自磁盘）
    std::vector<float> out;
    auto sink = [&]( const sicnu::runtime::chunk::TilePayload &p ) {
        writeCorePixels( out, p );
    };

    // 4) 一次调用获得：有界内存 / 取消桥 / 进度桥 / 类型化错误 / crash-safe resume
    auto result = sicnu::operators::runChunkedOperator(
        name(), canonicalParams, context, partition,
        sicnu::runtime::chunk::TileRunDeterminism::BitExact, kernel, sink );

    // 5) 把最终输出 .part → rename 出版（幂等重建式）
    publishAtomically( out, outputParam );
    return makeResult();
}
```

## 你免费得到什么（对照手写循环的过去）

| 关注点 | 手写循环（before） | runChunkedOperator（after） |
|---|---|---|
| 内存上界 | 每算子自查 | 队列/驱动器有界；`ramBudgetBytes` 可选硬准入（拒绝=typed `ResourceBudgetExceeded`，绝不 bad_alloc） |
| 取消 | 各自轮询 `throwIfCancelled` | context flag+callback 双形态桥接，between-tiles 粒度；取消终态=typed `Cancelled` |
| 崩溃恢复 | 无（重跑全量） | journal+checkpoint：已提交 tile **零重算**（digest 验证后从磁盘重放）；身份漂移自动拒绝复用 |
| 出版 | PartialOutputGuard 各自为战 | run 级 `PUBLISHED` marker：恰好一次；崩溃后 consume/publish 幂等重放 |
| 遥测 | 无 | tiles_processed 精确计数 + 采样 span（queue wait/stage 时长），事件量 ≤ tiles/rate+O(1) |
| 错误信封 | runtime_error 字符串 | chunk 异常族 → RSOperatorError 类型码全表映射 |

## 合约义务（接入即承诺）

1. **kernel 必须确定**：同 (identity, tileIndex) → 同 bytes。身份 = operatorId + canonicalParams + partition digest；**内核算法版本变化必须体现进 canonicalParams**（例如 `"kernelVersion": 2`），否则 resume 会错误复用旧 tile。
2. **sink/publish 必须幂等重建**：崩溃后重放会对全部 tile 重新 sink 并重新 publish——sink 应覆盖写（不要 append 两次语义），publish 应写 `.part` 再 rename。
3. **halo 声明一次**：`partition.halo` 是唯一事实；kernel 收到的 buffer 含 halo（bufferWidth=width+2·halo），sink 只消费 core 区。
4. **resumeStateBase 放在输出旁边**（durable、不被 scratch 清扫），默认在 context.workDir() 下。
5. NoData/CRS/provenance 语义仍在算子层——底座搬运 float 与生命周期，不解释科学含义。

## 模式选择

- `Mode::Resumable`（默认）：顺序驱动、O(1 tile) 内存、crash-safe。适合小时级大任务。
- `Mode::Pipeline`：ChunkPipeline 流水线（默认 queueCapacity=2）、吞吐优先、无 resume。两模式输出 byte-equal（有合约测试）。

## 已知边界

- Windows 下 fsync 为 no-op：rename 是原子性边界（与 ScratchRegistry/TileCheckpoint 既有契约一致）。
- 遥测默认关闭（`SICNU_TELEMETRY=1` 开启）；计数器恒生效。
- resume 期间被复用的 tile 从磁盘重放（含 digest 校验），kernel 不重算——这是 Oracle #2 的含义。

参照实现：`tests/test_chunk_adoption_11.cpp`（synthetic reference adopter，含 known-answer 真值与全部合约断言）。
