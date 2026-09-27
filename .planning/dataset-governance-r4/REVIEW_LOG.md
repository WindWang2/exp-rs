# REVIEW_LOG — Track 13 R4 Dataset Governance Hardening

## 评审设置

独立对抗性 review（只读 subagent，1 个槽位），范围 `origin/master..HEAD` 全量 diff，双轴：
- Standards：C++20/Qt 6 正确性、内存/边界、冗余、canonical 单点；
- Spec：矩阵组独立性、KAV 真值独立性、白名单合规、3.2 下限足量性。
评审时点：9 个提交（脚手架 + WP-A…F + 文档），评审结论 **SHIP-AFTER-P1**（评审报告原文：findings 1-3 must land before PR; 4-6 should follow immediately）。

## 审前自证（提交前已完成的佐证）

1. 矩阵注入自证：#1336 类 CRLF 截断回归注入 `textToJson` → 第 1 组立即红（exit 42）→ 还原复绿。
2. KAV 独立性：python hashlib / 公开 splitmix64 参考向量 / 按头文件算法文本离线重算的 Pcg32 向量（修正过一次"seeder 输出即 state"的语义误读——非循环论证）。
3. 白名单口径注记：Scope 列 `tests/test_dataset*`/`test_data*`，而 WP 指令自身点名扩展 test_mlops9_split / test_sample_label_annotation / tests/CMakeLists.txt（WP-G 注册缝）。本分支实际触碰的测试文件全部为数据域：test_dataset_fingerprint_determinism（新增，WP-A 明示允许）、test_split_reproducibility（新增）、test_leakage_audit_coverage（新增）、test_sample_label_annotation（WP-D 明示扩展；含既有"annotation chains"fixture 的 schema 化——新门改变了此前被接受的 ingest，评审确认为有意收紧）、test_d19_dataset_foundry（A2 修复的回归用例——feature join 的唯一既有测试文件，超出字面白名单一行用例，如实申报）。.gitignore + tests/CMakeLists.txt（注册 + 测试链接层断链修复）为最小必要基础设施。
4. src/agent 不在白名单：agent_loop 断链修在测试链接层，上游修复建议写入提交信息与 PR。

## 评审发现与处置（全部回填）

| # | 级别 | 发现（摘要） | 处置 | 提交 |
|---|---|---|---|---|
| 1 | P1 | `$<LINK_LIBRARY:WHOLE_ARCHIVE>` 需 CMake≥3.24，根声明 3.20——3.20-3.23 全测试树 configure 报错 | `CMAKE_VERSION` 护栏 + FATAL_ERROR 指明真实补救（升级或上游 PUBLIC-link 修复） | c6cd8659 |
| 2 | P1 | pre_post 去重把"仅单侧携带 counterpart"的对**整个丢掉**——重复上报变成静默审计缺口 | 仅在"对端回链"（真对称对）时按 id 锚定去重；单侧引用仍从链接侧上报；新增单侧对用例 + 证据定位断言 | a27f8601 |
| 3 | P1 | 矩阵 9/10/12 行同义反复（QJsonObject 内部有序 / QJsonValue 双精度构造两侧同字节），"12 组独立"夸大 | 全部改为**字节级**变体：平文档手写两序、序列化文档字节替换（1/1.0/1e0、0/-0.0、0.1/1e-1）；值变体行标 sameAsBase=false | 0e0678e9 |
| 4 | P2 | seed_hex 非字符串类型（数字/null）绕过缺失门→仍静默 0 | 增加 isString 门；wrong-type 用例钉死 | 9dcdb32b33 |
| 5 | P2 | EVIDENCE 措辞越位（golden tripwire 属 drift pin 非独立 oracle；部分日志未入库） | §3 拆分"独立权威/drift pin"；决定性摘录内联正文；raw logs 按仓约定留本地（见 #7） | 文档终稿提交 |
| 6 | P2 | 双跑为同进程，抓不到 Qt6 逐进程哈希重播种；切分缺跨进程 pin | 钉切分指纹跨进程常数 `88c811e3…37f`（指纹契约排除墙钟字段） | d1b9bee7 |
| 7 | P2 | .gitignore 偏离仓约定（sibling 用三行式"markdown only; logs stay local"），却提交了 .out | 改三行式；raw logs 移出版本库、摘录内联 EVIDENCE | 文档终稿提交 |
| 8 | P2 | 触碰两个既有测试文件（字面白名单外）+ fixture 修改证明新门收紧 ingest | 前者见上方"白名单口径注记"（全部数据域测试面，如实申报）；后者为有意收紧，评审确认 | —（文档申报） |
| 9 | P2 | WHOLE_ARCHIVE 施加于全部 sicnu_add_test 目标（体积/静态初始化/未来符号冲突面） | 作为已记录的临时停gap保留；上游 PUBLIC-link 修复列为 merge-gate 跟进（E5） | —（文档申报） |
| 10 | P2 | fold 侧证据归一已修但无断言 | 覆盖矩阵逐行新增 role_a/role_b ↔ sampleA/sampleB 定位断言 | a27f8601 |
| 11 | P2 | 注记 schema 门先于版本状态检查（错误优先级反转）；损坏 schema JSON 报"not found"轻微误导 | 门移至 versionStatusLocked/notDraft 之后；损坏 schema 报错文案保留"not found"语义（如实描述读取失败），不再调整 | cc6e1a7f |

评审同时确认的正面结论：无合法载荷被新门拒收（promotion 已自带 pair distinct/membership 守卫）；parseWktPolygon 对畸形输入全程 typed failure 无崩溃；SQL 参数全绑定无注入；无死锁（门内自持 StoreStmt，不调用加锁的 labelSchema()）；diff 排序键全序且上游 QHash 去重使重复键不可能；QString::arg 链与 -Wall -Wextra 无隐患。

## 终验

评审修复全部落地后，以最终树重跑 Oracle：`ctest -R "dataset|fingerprint|split|leak|ontology" -j1` 双轮 49/49 exit 0（见 EVIDENCE.md §1）；受影响套件（sample_label_annotation 127×2、leakage_audit_coverage 169×2、split_reproducibility 491×3）双跑以上全绿。
