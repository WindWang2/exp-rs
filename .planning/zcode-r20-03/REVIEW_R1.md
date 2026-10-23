# R20-03 REVIEW_R1 — 独立新鲜评审（第一轮）

评审者：fresh 子代理（与本实现无关，对抗式）。范围：`git diff 28e1891390..HEAD` 全部 4 commits。
jsoncpp 语义按本机链接的系统 jsoncpp 1.9.8（JSON_USE_EXCEPTION=1）逐条追溯。

## 结论：READY-WITH-FIXES（无 P0；2×P1 + 2×P2 + 7×P3）

## P1（必须修，已在本轮修复）
1. **schema_version 门用 isIntegral 仍可抛**（capability_catalog.cpp:934 / capability_relations.cpp:107）：isIntegral 对 int64/uint64/超界 real 返回 true，但 asInt() 对超 int32 范围值抛 LogicError——`"schema_version": 3000000000` 仍能中止整个 reload。**修复：改 `!version.isInt()`（isInt 同时检查类型与 int32 范围）。**
2. **catalog reload 对非对象 sidecar 文件仍抛**（capability_catalog.cpp:655-657）：validateEntry 记录 "sidecar must be an object" 后，`parsed["id"]` 的**非 const** operator[] 在 array 文档上是 resolveReference 断言/异常。**修复：validateEntry 循环后 `if (!parsed.isObject()) continue;`。**

## P2（应修，已修）
1. W1 注释把失败机理写成 "uncatchable assert abort / wild-pointer read"——在 JSON_USE_EXCEPTION=1 的 jsoncpp 1.9.8 上 resolveReference 抛**可 catch** 的 LogicError；部分消费点线程上无 catch 仍是进程死，但机理描述误导后续维护者。→ 已改写注释（含异常禁用构建下才 abort）。
2. decision_record 测试未钉 errorCode/errorCategory——回归到无类型拒绝也能过。→ 已补 `CHECK(errorCode == kInvalidParameter)` 等断言；注释不再把 pre-fix 抛错归因于 MCP 边界（schema 已声明字段在 validateAgainstRequired 已被拦，真正暴露的是直接 dispatch 路径）。

## P3（逐项处置）
1. W1 注释的 null 依据错误（ModelRuntimeCache::load 成功分支总是填 result["backendName"] 把 null 提升为对象；encoder 对 null result 直接省略字段）→ 注释改写为「null 容忍 = 与 params 门同词汇表的防御性允许」。已修。
2. decision_record inputSchema 缺 status 声明（pre-existing）→ **不改**：补声明将改变 tools/list 面并强制 surface 快照再生，超出本 PR 范围；工具内门已提供等效运行时防护。记录为后续项。
3. optionalString 显式 null 语义变化（null 从 "" 变 fallback）→ 行为改进且无 in-tree 调用方发 null；已在注释中写明。已修。
4. knowledge validator extends 允许显式 null 而 id/family/kind 不允许 → 加一词注释防误统一。已修。
5. loader 测试单例恢复 REQUIRE-fragile → 加 RAII 恢复盘。已修。
6. validator 级 object-id 子检查并非 RED（validateEntry 的 id 读原本就有门）→ 保留作为守护钉，已在 TASKS 标注不计入 RED 网。
7. operations_coordinator.h:41 注释缩进对齐。已修。

## 评审确认的关键正面结论
- 全库 157 个 catalog sidecar + capability_relations.json + 15 个 knowledge 文件零错型字段（新门不破坏任何现存数据）。
- 所有 in-tree sendResponse 生产者均发对象；fuzz 回环测试只回环成功解码的 envelope → W1 不破坏既有测试。
- 新测试对主要断言真实 RED（asInt/asString 抛错、salvaged 空、decision_record 异常穿透）。
- 范围合规：10 个文件全部在允许树内。
- OpsRunRequest 全部为成员赋值初始化（无位置聚合初始化）→ 死字段删除安全。
