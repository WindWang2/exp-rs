# DECISIONS — hardening/r4-processing-meta

格式：主题 | 声明（提示词/既有文档怎么说） | 实测 | 处置 | 理由。

1. **sparse sidecar 缺口** | 提示词 WP-B：手补 ≥70 个顶层 sidecar 至 157 全覆盖 | `test_algorithm_meta_drift.cpp` 把顶层目录钉死在"代码中声明 taskFamily 的描述符集合"（`REQUIRE(expectedCatalog.size()==57)` 双断言 + 字节可再生 + 往返恰好 57）；README（ADR 0122）明令 sidecar 为生成工件、禁手改 | **不执行**手补；记录 101/157 未声明 taskFamily 为设计内 opt-in（56 rs: + 1 gdal:polygonize = 57），转而收敛**字段级** authored enrichment 缺口 | 手补即打红仓库自身门禁=回归；铁律"前提不成立→记录证据后收窄"。
2. **gen-meta / gen-pages 工具不存在** | 提示词：全仓零命中，禁引用 | 二者是 `capability_knowledge_tool`（tests/CMakeLists.txt:10801，源 scripts/capability_knowledge_tool.cpp）的**子命令**：`gen-meta <root>` / `gen-pages <root> [--check]`；README 明载 | 全程经由该真实通道再生成 | 提示词的 rg 只搜了文件名；修正记账。
3. **capability_enrichment.py 账本** | 脚本 --strict 要求 115/115 全覆盖 | 实测红：语料已 157，42 算子无账本条目；无任何 ctest/CI 挂载 | **不改该脚本**（scripts/ 白名单外）；authored 键按 README 许可直接编辑侧车 JSON（仅 authored 键，永不改 derived 键），账本过期记 backlog | 白名单纪律优先；仓库的真实强制门禁是 ctest 族，不是该手工工具。
4. **快照 gate 断言方式** | 提示词 WP-D：`contract_inventory --check` 双跑 + 注入测试 | `--check` 退出 0 要求**字节一致且 findings 为空**；graph 装配是源扫描（不迭代注册表），assembler 级注入需整树拷贝 | 新测试 `test_snapshot_gate_r4`：①生成器双跑字节稳定（图+census）②提交快照==活生成字节（两快照，把 CLI gate 变成本地红绿断言）③合成扰动必须被 `formatSnapshotDiff` 归因到元素与字段。assembler 级文件注入由②间接强制（任何真实源/元数据变更都改活 JSON→红） | ②就是"改了没跟必红"的忠实实现；整树拷贝代价与收益不成比例，记录。
5. **authored 键修复路径** | README：authored 走 capability_enrichment.py 或直接编辑 | 直接编辑 + `gen-meta` 规范化（authored 保留、derived 重算、闭键校验、格式归一）为等效合规管道 | 每批：apply authored JSON → gen-meta → 提交 | 与 gen-meta 保留语义（capability_catalog.cpp:525-551）一致，diff 最小。
6. **门禁收紧范围** | WP-G："新增算子忘配 meta 即红" | 既有 D8 coverage 门禁已钉 sidecar 集合==注册表；真实缺口是 authored 键无门禁 | 新增四键 census 测试（`test_capability_completeness.cpp` 追加 TEST_CASE），census 数等式钉 157，无豁免表 | 与 kIoInputsExempt 等式惯例一致；不重复实现快照职责（snapshot gate 独立）。
7. **在途 PR 重叠** | 提示词列 3 个 open PR | 实测 5 个（#1334–#1338）；#1337 动 `data/contracts/contract_graph.snap.json` + `src/contracts/error_code_scanner.*`，#1335 动 `tests/test_capability_knowledge.cpp`，#1336/#1337 动 `data/help/commands.json` | 基于 origin/master `15e5c66b5` 工作；收尾时若目标 PR 已合并则 rebase 并重跑双跑门禁 | 提示词 Phase 0 预案。
8. **accuracy / gpu 字段** | 提示词：不臆造，无依据写 unknown | 实测 157 侧车 accuracy 有值者 0、gpu 显式声明 51 | 本轨道不新增任何 accuracy/gpu 声明 | 同"以实现真值为准、不臆造数值"。
9. **token 记账** | 提示词要求账本记录轮次 token | 会话无精确计费接口 | 账本记录**上下文增量估算值**并注明口径；PR 如实标注为估计 | 不虚报数字。
