# R21-E REVIEW_R2 — 独立评审记录（fresh subagent #2，2026-10-24）

评审人：R2（fresh，独立上下文，78 次调用，自行运行四个新测试二进制）。**终判：READY-WITH-FIXES**；以下全部处置完毕并复测 GREEN×2。

## 处置清单

| # | 发现 | 严重度 | 处置 |
|---|---|---|---|
| P0-1 | batch_stress 断言的记账不变式不是 runner 的性质（非 graded 且 verdict≠"unavailable" 的行不计入任何桶） | **P0** | ✅ 测试主张改为 runner 真实四分类（graded⊇duplicates / isolated / unavailable / unverifiable），unverifiable 显式计数并钉 0（本 lane 注入器不产该类）；注释引用 lab_batch_runner.cpp 契约。不越权改 src/cli（白名单外）→ 记账事实记录于 PR 正文 |
| P1-1 | studio 正例"故障被抓"是空洞的（grid 形状本身触发 must_fail；无角色敏感规则种类可区分角色互换） | P1 | ✅ 测试主张改写为诚实声明（证明接线真实读取扣分，不断言故障区分度）+ 新增**控制组**（must_fail 声明一个不会触发的断言 → 场景必须失败），使折叠非空洞；文件头说明为何故障区分度不可测（grader 指数用位置波段、band_role_swap 仅换标签），故障可观测性归 faultlab 期望 lane |
| P1-2 | 两个测试依赖 PROJ 数据库，无环境守卫 | P1 | ✅ 两 lane 加 `Crs::fromAuthid("EPSG:4326").isValid()` 能力探针，不可用时 WARN+skip（红→黄） |
| P2-1 | spec_digest 绑绝对路径，跨机不可移植 | P2 | ✅ composer 头部文档化（路径即被断言的契约的一部分；同输入 byte 恒等） |
| P2-2 | 每次评分白读至多 64MiB 计算无人消费的 digest | P2 | ✅ composer 以 digest 预算 0 构造 FsArtifactProbe（教学 spec 无 reproducibility 检查） |
| P2-3 | 缺文件走 usage 类 → 无证据块，测试把缺口钉成预期 | P2 | ✅ 保留分类（改类会动 CLI 退出码契约 2→3，影响最常见学生失败）+ attach 文档化决策理由 |
| P2-4 | indeterminate 拒绝文档形状与正常证据不一致（无 checks/counts） | P2 | ✅ 统一形状（checks:[]、counts 零、reason 字段） |
| P3-1 | schema 允许 rules_ref:""/must_fail:[]/无依赖，loader 更严 | P3 | ✅ schema 收紧（minLength:1、minItems:1、anyOf 依赖约束），与 loader 一致；JSON 合法性已验证 |
| P3-2 | grid 复检是 crs_grid 契约的严格子集（pixel_size 未复检）；"首个声明胜出"依赖未验证注释 | P3 | ✅ 头部文档化复检范围；多 crs_grid 时钉第一个 |
| P3-3 | file-mode 实验室得仅 exists 检查 → "pass" 语义弱 | P3 | ✅ 头部文档化（证据文档自身只携带那一个检查，读者可见） |
| P3-4 | verifier_skipped_no_fault 分支无覆盖 | P3 | ✅ 新增测试（角色缺失→故障不应用→跳过+warning，钩子未被调用，scenario 仍失败） |

## 复测证据（R2 修复后，GREEN×2）
- test_r21_e_verifier_evidence：22 断言 / 4 case ×2 ✅
- test_r21_e_faultlab_verifier：66 断言 / 10 case ×2 ✅（含新增 P3-4）
- test_r21_e_batch_stress：31 断言 / 3 case ×2 ✅（含 P0-1 修正）
- test_r21_e_studio_verifier_hook：11 断言 / 3 section ×2 ✅（含 P1-1 控制组）
- 回归 sweep（28 个在轨目标）：**全绿，0 失败** ✅
- e2e 离线全链路：ALL E2E STEPS PASSED（score=100、verifier status=pass、批量 15 行 2 unverifiable 隔离、重跑 byte 等同）✅

## R2 明确验证为稳健的项（备查）
validateSpec 接受一切可达 spec；6 挂载点覆盖全部 artifact-class 路径；W2 fail-closed 无 passed=true 漏洞；RasterWriter 用法与波段语义一致；CMake PIC 必要且无环；无 golden digest 被破坏。
