# R20-03 REVIEW_R2 — 独立新鲜评审（第二轮，全范围 + 失败矩阵）

评审者：第二个 fresh 子代理（独立于实现者与 R1 评审者）。范围：`git diff 28e1891390..HEAD`（当时 7 commits，含 R1 修复轮）。
方法：静态失败矩阵逐格走查（jsoncpp 1.9.x 源码级语义核对）+ 别名/逃逸分析 + 注释-代码逐条核对 + W1 wire 形状核验 + W5 聚合初始化全库排查。

## 结论：READY-WITH-FIXES（无 P0；1×P1 + 1×P2 + 2×P3；已全部在 88a637df6b 修复）

## P1（已修）— validateEntry 的 variants[].when.param 仍可抛穿门
- `capability_knowledge.cpp:791-793`：`when.get("param","").asString()` 对 array/object 抛 Json::LogicError——恰是 W2 声称关闭的逃逸类；且 reload() 开头已清空 mEntries/mFamilyDefaults 并置 mLoaded=true，异常逃逸后进程留下**静默空目录**。
- 修复：param 先 isString 门（数字/布尔也从静默 coerce 改为拒）；RED loader SECTION 钉住（坏 variants 文件只损失该文件）。

## P2（已修）— 查询时 variantParams 腿无门
- `capability_knowledge.cpp:334-337`（mergedEntry）：`variantParams.get(param,"").asString()` 的输入是**调用方**的 node/tool params（workflow_planner/repair/analysis、data_platform_tools、tool_shortlist 六处调用面），数组型参数（band/resolution 列表常见）在查询/extends 行走路径上抛出。
- 修复：非 string 供给值跳过该 variant 匹配（continue）；REQUIRE_NOTHROW 查询测试钉住。

## P3（已修）— where 前缀跨文件误配
- `capability_knowledge.cpp:197-199`、`capability_catalog.cpp:668-671`：`p.rfind(where,0)==0` 裸前缀会把名为 `a.json[1].json` 文件的问题误配给另一文件的数组位置 `a.json[1]`，静默跳过健康条目。修复：匹配两加载器实际推送的 `where + ": "` 精确分隔前缀。

## P3（已修，注释）— test_exprs_ipc null-producer 注释残留
- R1 已要求从源码注释删除的错误依据（ModelRuntimeCache::load 成功分支总把 result 填成对象；encoder 对 null 直接省略字段）在测试注释中残留弱化版。已随本轮测试更新清除。

## R2 显式核验的五项（全部通过）
1. **isInt/asInt 双精度矩阵**：jsoncpp 1.9.x 源码级核对——isInt() 对 real 要求 [minInt,maxInt] 且整除、对 int/uint 要求范围 ⇒ isInt()==true 时 asInt() 永不抛；4294967295.0/2.5/-0.0/Int64(3e9)/uint(4294967295) 逐格走查无抛点。
2. **optionalString 矩阵**：bool/int/double/array/object 全部走 typed 拒绝分支，绝不用 jsoncpp 的数字→字符串静默转换——比 schema 声明与 registry seam 更严格且一致；input 非对象退化为通用 typed 拒绝，无崩溃。
3. **其余未门控读**：P1/P2 是仅有的两处（catalog validateEntry 其余全门控；relations checkOperator/exclusive/grid_fixer 全门控；mFamilyDefaults[id]/mEntries[id] 非对象类型上不存在 resolveReference 危险）。
4. **测试别名/逃逸**：无循环内 REQUIRE 单例副作用；RestoreDirs 析构次序正确（SECTION 级 QTemporaryDir 先亡，TEST_CASE 级守卫后亡，失败回卷时也成立）。
5. **W1 wire 形状**：手构 envelope 与 encodeEnvelope Response 分支逐字段一致（v=1/type/id Int64/ok bool/result null 省略），各 REQUIRE_FALSE 失败原因专属于 result 类型；全库 ok-response 生产者兼容；decodeEnvelopePayload 是唯一解码缝。

## 其他确认
- W5：approvePendingRepair 全库零残留；OpsRunRequest 无位置/花括号聚合初始化、无序列化包含该字段。
- W4：TooLarge 路径 pending 恰为 4 字节（resume 只能在合法前缀后携带额外字节）；抢救顺序（payload→pending.clear→error）使别名调用方最终持有 E6003 消息；生产调用点（ipc_channel.cpp:695）用独立字符串。
- 注释引用的 file:line 全部与现码一致（plugin_host_proxies.cpp:201、plugin_host_process_runtime.cpp:207/551、ipc_channel.cpp:714-730 等）。
- 已知残留（记录不修）：~RestoreDirs 析构中 reload() 若抛出会在栈回卷时 terminate——P1 修复移除了唯一现实抛源，属理论边界。
