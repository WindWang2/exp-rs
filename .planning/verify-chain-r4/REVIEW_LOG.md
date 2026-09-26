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
