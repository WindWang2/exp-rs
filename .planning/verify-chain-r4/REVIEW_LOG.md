# REVIEW_LOG — Track 16 独立对抗审查与整改记录

审查员：独立只读子代理（与实现分离），2026-09-27。
范围：`git diff origin/master(15e5c66b54)..HEAD` 全量，Standards 轴 + Spec 轴 12 项检查。
结论：**SHIP-WITH-FIXES**（全报告见审查员输出；关键摘录与整改如下）。

## 审查确认（无罪判定摘录）

- 零生产代码修改属实（14 触碰文件全部在白名单内）；所有可实测数字无虚报
  （194/294/126/264/385/225 断言、20 案、13 产出点行、72 头、47 域测试文件逐一实测吻合）。
- BASELINE 计数锚、API_AUDIT 四处签名抽查、ADAPTER_MATRIX 两格 N/A 声明、D2 三缝隙
  语义与代码逐行吻合；jsoncpp 1.9.8 locale 无关声明获评审员独立探针二次证实。
- WP-C 空规格案非重复；WP-D 两处"反直觉但正确"的钉死（全量清缓存、null-registry
  合法命中）为高价值非同义反复断言；测试 lambda 捕获无悬垂。

## Findings 与整改（提交 261b2f8000）

| 级别 | Finding | 整改 | 验证 |
|---|---|---|---|
| P1-1 | run_matrices.sh 被自家 .gitignore 白名单（仅 *.md）吞掉，提交信息却声称交付 | 白名单追加 `*.sh` 并提交脚本 | 脚本实跑 ALL MATRICES GREEN (2 rounds) |
| P1-2 | EVIDENCE.md 被三处收口文书引用但不存在 | 补写并提交（本目录） | 本文件 + BASELINE/API_AUDIT/PLAN 引用落地 |
| P2-1 | V5 行是 V1 的内存同义反复；parseSpec 产出点（BASELINE P3）无真实矩阵行 | V5 改为 canonical 文本 → 真实 parseSpec() → digest 对比 | 199 断言双跑绿 |
| P2-2 | budget 案第二阶段注释称"不同坏状态"实为同一串自赋值 | 第二阶段换真实不同坏护照（他 CRS + 他 off-list 单位，同 finding 量） | invalidation 294 断言绿 |
| P2-3 | WP-F 第三案 "provider channel serves" 无咨询证据 | 改用 SpyProvider 断言 consultations==1 | adversarial 385 断言绿 |
| P2-4 | localedef 命令的 TMPDIR 引号注入面 | 拒绝含单引号的临时路径（'…' 内唯一逃逸字符） | locale 矩阵绿 |
| P2-5 | grader 新案前半段与既有 SECTION 部分重叠 | 块头注明分工（既有=逐族 reason 链；新增=报告级区分） | 保留 |
| NIT-1 | `trace(*(&before),…)` 无意义取址解引用 | 改 `trace(before,…)` | invalidation 绿 |
| NIT-2 | SharedAuthority::find 死代码 | 删除 | 绿 |
| NIT-3 | fixture 用 std::to_string(double)（随 locale 出逗号）生成 JSON | （评估后）宿主进程恒 C locale 启动 + 值全为整数可表；不改，风险记录在案 | — |
| NIT-4 | asDouble()==0.3 焊进 12 位有效数字实现细节 | 随 P2-1 重写自然消除（该断言随 V5 旧形态移除） | 绿 |
| NIT-5 | provisionCommaLocale 进程内重复执行 | static once-cache；并修复缓存化引入的隐式副作用依赖（bite 检查前显式 tryLocale 进入敌对 locale） | 199 断言双跑绿 |

NIT-3 处置说明：`scenePassportJson` 的 pixelSize 仅取 10.0（整数可表，`std::to_string(10.0)`="10.000000" 在任何 LC_NUMERIC 下小数点均为 '.' 的问题只在逗号 locale 启动时出现；本宿主与 CI 均以 C/UTF-8 启动，测试二进制不经 locale 改写启动路径），记为已知限制不阻断。

## 整改后全绿状态

- test_verify_chain_locale_matrix：199 assertions / 2 cases × 2 轮（exit 0/0）
- test_preflight_authority_invalidation：294 assertions / 20 cases
- test_grader_engine：264 / 16；test_suitability_adversarial：385 / 28；test_verifier_engine：126 / 17；test_verify_adapters：225 / 9
- run_matrices.sh：ALL MATRICES GREEN (2 rounds)

---

# R2 独立对抗评审（2026-09-28）

评审员：独立只读 subagent（1 槽位），对象 hardening/r4-verify-chain-r2（a726d17a62 + 4 commits）。两轴（Standards/Spec）+ 红线核对。用量 2.86M tokens / 67 工具调用。

## 确认为真的关键声明（评审员独立复证）
- 冲突标记为全树唯一残留，由 merge `c747617f79` 引入（两父均无此行），两侧内容均保留且必要；master configure 必炸成立。
- 29 处 TEST_PREFIX 机械正确；vendored Catch2 + Sicnu 包装器对 TEST_PREFIX 全透传；`^r4::` 等 6 处既有选择器零破坏；RS14-05 `^test_grader` 选择器修复后反而从 0 命中变命中。
- F5 的 InvalidParameter 确为生产合同（param_guard 前置 InvalidArgument + io_operators.cpp:39 确定性映射）；no-partials 断言仍承重。
- 红线：diff 7 文件全部白名单内，零 src/ 触碰。

## 发现与处置

| id | 级 | 轴 | 摘要 | 处置 |
|---|---|---|---|---|
| F1 | P1 | Spec | census 只查域二进制内部，源码级 ~189 个非域套件用例撞 D6 关键词不可见 | **事实修正 + 工具补强**：机械复算本构建人口（52 目标闭包，530 例）裸撞名 = 0（评审 189 例属未入闭包的 agent/cartography 等域套件，仅全量构建口径进入人口）；census v2 增加反向断言（任意构建口径下，D6 匹配且无 `::` 前缀的用例即 FAIL），EVIDENCE/PR 改三层口径并披露 189 例为他域卫生 |
| F2 | P2 | Standards | 域目标枚举只覆盖 14 种注册宏中 6 种 | census v2 改 `sicnu_add_[a-z_]+\(` 通配全部宏 |
| F3 | P2 | Spec | LEDGER R2-4/R2-5 时序矛盾（357 vs 525；"52 全前缀化" vs "50+2"） | LEDGER 拆为 R2-4a/4b/5 真实时序；措辞改为"50 前缀化 + 2 allowlist" |
| F4 | P2 | Spec | 人口阶梯口径与"entirely escaped"措辞过强 | EVIDENCE R2.2 改三层口径（完全隐藏/偶然入选/残余 0）；suitability 90/142 基线本就偶然入选如实入档 |
| F5 | P3 | Standards | TEST_PREFIX 链路展开未加引号（未来含空格前缀会拆参） | 4 处展开加引号（现值无空格，零风险） |
| F6 | P3 | Standards | r4:: 计数只打印不强制 | census v2 加 `[ "$r4" -lt 1 ] && overall=1` |
| F7 | P3 | Standards | disc glob 多行/多配置隐患 | census v2 逐文件取第一个存在文件 |
| F8 | P3 | Spec | F5 接受集偏松（DirectoryNotFound 为死码选项） | 收紧为精确码 `CHECK(code == "InvalidParameter")`，注释写明合同变更协议 |
| F9 | P3 | Spec | "文档化合同"表述偏强（合同仅在生产源码注释） | 措辞改"生产合同注释"（EVIDENCE/测试注释/PR 同步） |

**结论：SHIP-WITH-FIXES → 全部 P1/P2/P3 已整改**（F1 以"事实修正 + census v2 反向断言"落地；非域 189 例前缀卫生披露给他域，本轨不越界代修）。整改后复验：census v2 ALL GREEN（前向 + 反向 + r4:: 强制），ctest 双轮见 R2-6。

## R3 独立评审（2026-09-29，只读 subagent，3.40M tokens / 69 工具调用 / 23 分钟）

评审对象：78249bf29f..a31ee878dd（4 提交，基线 cf2d41647e）。评审员独立复算了全部可复算声称（census 复跑、ctest -N 人口、discovery 逐格 grep、/tmp 独立枚举、foreach 反例构造、三套件直跑、矩阵复跑），并以构造反例实证 v3 强于 v2（foreach 风格 D6 名目标：v3 FAIL 可见 / v2 双盲零输出）。

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| F1 | P1 | 漂移量标签 "713 提交" 归属错误：R2 基线 a726d17a62..cf2d41647e 实为 129（自 R2 合并 91）；713 是本地 master 落后 origin 的数字。分析窗口本身正确（129 窗口复算全部成立），纯标签错误 | **已修**：BASELINE R3.1/R3.2/表格、EVIDENCE R3.1、ledger 表头改 129 并注明 713 的真实含义 |
| F2 | P2 | EVIDENCE R3.4 人口阶梯分解不成立：共有套件逐格比对零变化，"+24 = 23 + 1 关键词撞名例随前缀化进入人口"，无"既有套件增长/evidence_seam 扩容" | **已修**：按评审复算改写 |
| F3 | P2 | 披露数 33/58 不可复算（脚本未归档）且与评审员宽口径 85/176 不一致 | **已修**：归档 `source_level_collision_scan.sh`（严格关联下界 33 目标/57 用例，方法注明），EVIDENCE R3.6.1 改为区间披露（57 下界 / 评审宽口径 176 / 无关联上界 490 含已前缀），处置不变 |
| F4 | P3 | grounding 13 例中 2 例名含 "verification" 修复前已被关键词误选，"13 例逃逸" 不精确 | **已修**：表格精化为 11 完全逃逸 + 2 关键词误选 |
| F5 | P3 | "Track 8 新增 7 个套件" 计数松：test_agent_loop_resume 系 #1201 既有 | **已修**：改 6 个并注明 |
| F6 | P3 | v3 BUILD-GAP 对 allowlist 未建目标静默零输出，allowlist 漂移不可观测 | **已修**：补可见性行（"not built (allowlisted: exempt)"），复跑 ×2 全绿且行可见 |
| F7 | P3 | 脚本头 "every registration channel" 对 maxdepth-2 布局过claim | **已修**：注释限定 build 根与 tests/ 布局 |
| F8 | P3 | 残余结构盲区未点名：foreach + D6 名 + 未入构建人口的套件在闭包口径下双盲（全量口径可检出） | **已修**：脚本头 "Residual blind spot" 段落明示，闭包/全量两口径关系写明 |
| F9 | P3 | ledger R3-3 闭包构成描述不准（58 巧合一致但构成写错） | **已修**：账本 R3-9 勘误行（append-only） |

总裁决：FIX → 1 P1 + 2 P2 + 6 P3 全部同轮整改完毕，census 复跑 ×2 全绿。评审同时确认：门与代码本身（3 行 TEST_PREFIX + census v3）经全部机械复算与反例实证成立，越界轴/白名单/零新方向全部通过。
