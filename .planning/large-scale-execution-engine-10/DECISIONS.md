# DECISIONS — large-scale-execution-engine-10

autonomy=full 下的预答与已决事项（追加式）。

## Autonomy defaults（预答清单）

1. **格式/来源**：既有权威优先——fingerprint 用 `data/execution_fingerprint.h` 的 contract v2 语义；tile 几何以 `runtime/chunk/tile_spec.h` 为准（与 GdalBlockStream::Tile 同构）；原子写复用 `workflow_checkpoint` 的 tmp+fsync+rename 模式与 `geospatial/util` 的原子发布工具；错误分类沿用 typed error 字符串前缀家族（`worker ...:`、`ChunkCancelled`）并新增封闭枚举。
2. **失败项处置**：单个测试/文件失败先诊断；属本 track 修复，属他轨记录 OUT_OF_SCOPE；flaky 测试标注并给确定性 reproducer，不跳过掩盖。
3. **命名/编号**：新文件 `src/runtime/chunk/` 蛇形命名；新测试 `test_large_scale_execution_10.cpp`；ADR 编号顺延取 `0148`（0146 被 9 文件复用属 #969 过程缺陷，本 track 取最大未用号 0148，避开争议区）；planning 文件用 GOAL 模板列出的名字。
4. **资源与超时**：单命令默认 timeout 600s（构建 600s、可后台续跑）；测试单条 120s；超时降 `-j1` 重试一次，再超时记录并收窄范围。
5. **对外动作**：`git fetch origin`、`git push -u origin zcode/<slug>`、`gh pr create` 允许；不改 GitHub issue 状态、不评论；force push 禁止。
6. **范围外发现**：记 EVIDENCE.md `OUT_OF_SCOPE` 节；P0 级另在 PR_BODY.md 顶部标注。
7. **依赖新增**：禁止新增第三方依赖；并发原语用 std/Qt 既有设施；哈希用仓库既有 SHA-256 实现。

## 架构决策（Phase 1 起追加）

### D-1 本 track 不建第二调度器
执行链保持 `WorkflowRunCoordinator → TaskCenter → JobEngine → Executor`（ADR 0144 先例）。Tile DAG、memory planner、scratch、checkpoint 全部作为 TaskCenter/JobEngine/chunk 模块的**库级**扩展，不新增进程/线程池权威。

### D-2 能力契约扩展方式：在 RSOperator 上加默认虚函数，不改旧算子
`memoryPolicy()` 已是先例。新增 `tileDependency()`（none/halo/global_reduction/multipass/external_memory）+ halo 半径声明，全部带向后兼容默认值；schema 投影为 additive 字段。不批量改 111 个算子。

### D-3 Tile DAG 用"多输入 ChunkGraph"扩展 chunk 模块，不推翻 ChunkPipeline
ChunkPipeline（单链）保留并成为 ChunkGraph 的特例；join 节点以 N:1 有界输入队列表达 fan-in；TileSpec 增加 bandRange/timeIndex（additive，默认全带/单时刻，旧调用方字节不变）。

### D-4 外存中间层是"scratch 租约"而非新文件系统
tile 落盘用 `ScratchRegistry`（登记、预算记账、引用计数、原子 finalize、崩溃清扫）+ 既有 temp 目录约定；不引入新存储引擎。磁盘文件布局：`<scratchRoot>/<runId>/<tileId>.tile`，头部自描述（magic+version+digest）。

### D-5 长任务 checkpoint 复用 workflow 原子写原语
`TileCheckpointWriter` 独立于 WorkflowCheckpointManager 但复用同一 fsync/rename 原语与版本门模式；resume 语义 fail-closed（身份/参数不匹配即整任务重跑），与 8.0 WP-E 方向一致。

### D-6 资源默认值保持保守
新维度 gate 默认 off（先例：7.0 tempDisk/vram/ioHeavy 默认 0）；提供 env/preset 打开路径。NVML 对接只读 Model Runtime 已有 device truth（不建第二 detector），把"free VRAM 下限"暴露给 admission 的 vram 维度。

### D-7 issue #971 的边界
只做执行面（取消注入点 + 有界工作集 + 有界 NMS 候选缓冲），不改 NMS 匹配几何语义；不改检测算子行为契约。若 review 发现与某算子轨冲突，降级为"接口建议"并记录。

### D-8 缓存 environment pin 语义
fingerprint 的 implementation identity 追加可选 `environmentPins`（GDAL major.minor、PROJ major、声明的 SICNU_* 行为开关集合）。默认 pin 集为空 ⇒ 旧条目字节不变（additive，同 8.0 WP-F 先例）。
