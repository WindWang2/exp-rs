# R21-B REVIEW_R2（独立终审，2026-10-24）

评审人：fresh general-purpose 子代理（与 R1 及实现者隔离；全程只读，未编译未运行）。
结论：**REQUEST-CHANGES**（1×P0 + 1×P1）→ 全部 P0/P1/P2 已闭环（commit de2dbd121b 等），P3 部分记录。

| ID | Severity | 发现 | 处置 |
|---|---|---|---|
| R2-F01 | **P0** | HEAD 不可编译：两个测试文件的编译修正（命名空间/AssetState）只在工作树、未提交 | ✅ de2dbd121b 一并提交；随后重建+重跑验证 |
| R2-F02 | **P1** | 多输入 lane `headNames[h]` 越界读（manifest 未声明 tensor_names 时 headNames={""} 而 headOutputs≥2） | ✅ 改索引 `resolvedHeadNames[h]`（与 headOutputs 等长） |
| R2-F03 | P2 | 两个 lane 残留死变量 headRole（位置索引模式，F3 明令禁止） | ✅ 删除 |
| R2-F04 | P2 | WorkspaceCatalog 的 stamp 竞态未同步修（R1-F5 只修了 artifact_store） | ✅ INSERT OR IGNORE |
| R2-F05 | P3 | A5 早拒不可达重复 | ✅ 删除，注释改为描述真实保护（补偿腿） |
| R2-F06 | P3 | B5 文档"whatever the postprocess block says"过宽（derived lane 仍无门控；既有缺口） | 记录为既有契约缺口（output.format 与 head role 无关联校验），不扩大本轮范围 |
| R2-F07 | P3 | .gitignore r21-B 白名单不过滤非 md | ✅ 收紧为 *.md |
| R2-F08 | P3 | PR_BODY 引用了不存在的 REVIEW 文件、占位 SHA、×2 声明与证据不符 | ✅ 本文件 + PR_BODY 重写 |
| R2-F09 | P3 | A1 后 findByPath 对缺失文件的非规范拼法不再解析 | 记录为可接受行为变化（record key 集只存规范拼法；查询 tiers 覆盖 raw/absolute） |
| R2-F10 | P3 | createSchema 内 newer-stamp 复查不可达（纵深防御） | 注释标注为 intentional defense-in-depth |
| R2-F11 | P3 | A5 崩溃窗口（metrics 与 run 之间进程崩溃仍留孤儿行） | 记录为 accepted risk（PR body 声明；单事务跨两写才可闭合） |

## R2 核实通过面
- 六项修复恢复完整性：逐 commit diff 比对零丢失
- B5：legacy flat、heads<tensorNames、uncertainty pre-threshold 收集、ModelHeadContract 可见性全部正确
- A4：两库 key PRIMARY KEY 支持 OR IGNORE；stepFailed 语义；无调用方依赖 schemaVersion 等于构建版本
- A5：runId 时序（setRunId 先于 install 与 compensation）；deleteMetricRecord 镜像 saveMetricRecord 守卫
- A6：errcode 先读、无介入语句、无外层事务
- #1647-1：QByteArray 临时量生命周期、注册顺序（先于 shim）、无 m_mutex 死锁
- 测试质量：A5 trigger 重跑/二次导入无碰撞；seg_emb 标量 initializer 为合法 rank-0 ONNX；并发不变量 PK 保证
- Merge-loss：范围内无 merge commit，逐文件历史为延伸非回滚
- 白名单：28 文件 100% 合规
