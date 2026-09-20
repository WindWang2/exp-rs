# AUTHORITY MAP — ds41-capability-help-sync

每类 metadata 的真正权威字段。**唯一真相源 = live registry**：`RSOperator::metadata()` + `RSOperator::schema()` 经 `AlgorithmDescriptorBuilder::buildFromRsOperator`（`src/processing/framework/atomic_algorithm_adapter.cpp:148-302`）产出 `AlgorithmDescriptor`/`AgentMetadata`。一切 sidecar/help/页面都是它的投影。

| 字段 | 权威（code 作者处） | 投影产物 | 守门 |
|---|---|---|---|
| taskFamily | `meta["task"]`（各 operator `metadata()`，41 处；`AgentMetadata::taskFamily` `algorithm_descriptor.h:116`） | Layer-A `task`；Layer-B v1 `task`；MCP metadata | drift 测试 byte-for-byte |
| gpu | `AgentMetadata::gpuAccelerated` + 三态 `gpuDeclared`（#707：缺失≠false） | Layer-A `gpu`（仅 declared 时） | `resolveAgainstDescriptor` 漂移登记 |
| accuracy / notes / tags | `AgentMetadata` 同名字段 | Layer-A 同名字段（tags 与 sidecar 并集合并） | drift 测试 |
| purpose | `AgentMetadata::purpose`（always emitted） | MCP `metadata.purpose` | test_mcp_server 非空断言 |
| description / displayName / group | `op->description()/displayName()/group()` | Layer-B `operator_group`；help 标题/分类；MCP schema title | family↔group 一致 gate |
| 参数 schema（名/类型/默认/enum/range） | `op->schema()`（`rs_schema.h:37-82` make*Param/setRange；**无 unit 参数**） | Layer-B `io.parameters`；help `parameter.*`；MCP input_schema | 全块 byte-equality |
| io inputs/outputs 分类 | **派生**（按端口 data kind 拆分，`capability_catalog.cpp:423-448`） | Layer-B `io` | byte-equality |
| modality | `x-rs-contract["modality"]`（operator schema 内声明） | Layer-B `modality`；search facet | 闭环词表 + 镜像一致性 |
| band_roles | **authored-only**（gen 时从 Layer-C 镜像种子） | Layer-B `band_roles` | 镜像一致性 + 正整数校验 |
| memoryPolicy | `op->memoryPolicy()` | Layer-B `determinism.memory_policy`；help fact；search facet | byte-equality |
| determinism grade | `op->determinism()`（ADR 0124 词表） | Layer-B `determinism.grade`；help；MCP stamp | REQUIRE∈{bit_exact,tolerance} |
| stochastic / sideEffects / largeRasterSafe / costClass | `AgentMetadata` 同名字段（largeRasterSafe 由 policy 派生+手署并存） | Layer-B `determinism.*` | byte-equality |
| requiresProjected | **authored**（Layer-B sidecar 内） | Layer-B `crs.requires_projected` | 校验器 |
| requiresSharedGrid | **派生**（任一输入端口 `x-rs-contract.gridRelation=="same-grid"`） | Layer-B `crs` + `capability_relations.json` | 关系图一致性 gate |
| prerequisites / limitations | `AgentMetadata` 列表（derived-first + authored 追加、去重） | Layer-B 同名字段 | byte-equality |
| **summary** | **authored-only**（无代码派生路径；validator 允许空串） | Layer-B `summary`；manifestPage；knowledge pages | 完整性 gate（本 Track 新增） |
| **failure_modes** | **authored-only**（code 必须来自 `rs_operator_error.h` 闭环词表，每项带 when+remedy） | Layer-B `failure_modes`；errorCatalog | 词表校验（本 Track 补非空 gate） |
| applicability / teaching_use | **authored-only** | Layer-B；manifestPage | 形状校验 |
| 错误码 | `enum class ErrorCode`（`rs_operator_error.h:24-69`） | 诊断 help 页；failure_modes.code | test_help_coverage REQUIRE |
| **units** | **不存在**：`rs_schema.h` make*Param 仅 (name, description, default) | — | 无（known limitation，不编造） |
| **NoData 语义** | 仅 `x-rs-contract` 散文与 ad-hoc 参数 | — | 无（known limitation） |

## 派生 vs authored 纪律（ADR 0154 §2 延续）

- 机械可派生的一律由 `capability_knowledge_tool gen-meta` 从 live descriptors 生成；手改派生字段 = 构建失败（`test_capability_knowledge.cpp:170-196` byte-equality）。
- authored 键（summary/failure_modes/applicability/teaching_use + authored modality/band_roles/requires_projected/额外 prerequisites）在重新生成时保留——**这正是损坏文件必须先抢救 authored 键再重生的原因**（DECISIONS D1）。
- Layer-A（`algorithm_meta/*.json`）与 Layer-B（`capability/rs-*.json`）是两个不同投影：Layer-A 进 MCP `catalog` 块；Layer-B 进 harness/Pi/GUI 目录面板。**两者互不校验是与 registry 各自校验，不构成第二真相**；本 Track 的 parity gate 把两侧都锚定到同一 live registry 集合上。

## 探索结论

`data/agent/capabilities/*.json`（ADR 0142 可行性层）是 modality/band_roles 的**种子权威**，Layer-B 冻结副本并由 guard test 交叉核对——一处事实、两个消费者，符合"不建第二真相"。
