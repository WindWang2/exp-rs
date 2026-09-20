# DECISIONS — ds41-capability-help-sync

## D1 — 不新建第二真相：修复数据，不改生成器投影

**决策**：19 个损坏 sidecar 用现有 `capability_knowledge_tool gen-meta` 重生修复，不为"绕过损坏"新建旁路 loader 或第二套 sidecar。
**替代方案**：(a) 手写修复 19 个文件 — 放弃，易引入新漂移且无法证明与描述符一致；(b) 改 CapabilityCatalog 容忍损坏文件 — 放弃，那是把 gate 降级。
**代价**：gen-meta 读取已损坏文件时 authored 键会丢失，因此需要一步**可审计的抢救**（脚本从损坏文件中抽取完整副本的 authored 键，写入修复中间态，再由 gen-meta 规范化），抢救清单与逐文件结果记录在 EVIDENCE.md。

## D2 — pin 值是"陈旧标记"，更新 pin 是修复而非放水

**决策**：`test_algorithm_meta_drift.cpp` 的 `REQUIRE(expectedCatalog.size() == 43)` 随 live 事实更新为实测值，并把注释改成"与 registry 声明的 taskFamily 集合精确相等"的语义说明。
**替代方案**：放宽为 `>= 43` — 放弃，那会丧失精确 membership gate（正是本 Track 要消灭的漂移类型）。
**依据**：53 个 sidecar 全部由 `exportCatalog` 从 live descriptors 重新生成，byte-for-byte 与磁盘一致，双向 membership 由测试第 1/2 节强制。

## D3 — surface parity gate 只做 algorithm/capability 面，扩展而非重做 tool 面

**决策**：新测试 `tests/test_capability_surface_parity.cpp` 覆盖：registry rs: 集合 ↔ sidecar A ↔ sidecar B ↔ help `operator.*` 主题 ↔ CLI `algorithms list`（子进程）↔ MCP `list_algorithms`/`list_operators`（in-process handler）id 集合一致性 + schema 关键样本一致；显式 exemption 列表（id+reason+owner）随代码走。
**不做的**：不改 CLI/MCP 投影的 key 词汇表（`source: plugin|builtin` vs `rs|provider` 不一致）——那是生产 surface 行为变更，超出 owner 边界；作为 known limitation 登记，parity gate 断言"名集合一致 + 字段可从 descriptor 双向验证"而非"字节相同"。
**替代方案**：要求 CLI/MCP 输出字节一致 — 放弃，会破坏已发布 surface 兼容性与既有 test_surface_parity 契约。

## D4 — 完整性 gate 分层：硬断言 + 显式豁免 + 诚实 WARN

**决策**：
- REQUIRE：summary 非空、failure_modes 非空、io.outputs 非空；
- REQUIRE + 豁免清单：io.inputs 非空（23 个集合类算子输入为路径数组参数，逐条登记原因）；
- WARN：units/NoData 覆盖（`rs_schema.h` 无 unit 字段，属基础设施缺口，不在本 Track owner 内编造）。
**为什么**：Track 非目标明确"不自动编造科学说明"；把基础设施缺口写成硬失败只会逼出假数据。

## D5 — 补全 3+1 个空 authored 键的内容来源

**决策**：rs-change/rs-classify/rs-regress 的 summary 与 failure_modes 从**算子已声明的元数据**（purpose/notes/description/taskFamily 与代码中的实际错误路径 `rs_operator_error.h` 编码）归纳，不引入外部科学论断；rs-atmospheric-dos2 的 failure_modes 从其真实失败条件（DOS 系列暗像素假设）与已登记错误码归纳。

## D6 — ADR 引用漂移修复范围

**决策**：修正 D8 capability 相关文件中对不存在的 `ADR 0146` 的引用为 `ADR 0154`（`scripts/capability_knowledge_tool.cpp`、`tests/test_capability_knowledge.cpp`、`data/processing/algorithm_meta/README.md`、`tests/test_cn_products.cpp` 的 CN 产品引用为 0157）。`review/DEEP_REVIEW_R2.md` 已记录 "ADR 0146 九重复用" 问题，本 Track 只修 capability 语义指向，不动 labspec 自身的 0146 引用。

## D7 — 资源策略

**决策**：worktree 构建目录 `build-cap/` 复用主仓 `build-dev/vcpkg_installed`（同 manifest，避免数小时依赖重编）；编译 `-j2`（Track 上限），全程只构建受影响 target（generator + 4 个测试）；测试运行 QT_QPA_PLATFORM=offscreen；不在线等 CI。
