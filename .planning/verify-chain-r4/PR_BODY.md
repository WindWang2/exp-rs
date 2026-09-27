## Track 16 — 验证链收口：verifier / adapters / grader / preflight / suitability / science_context（R4）

标题说明：任务建议标题为"验证链收口"，实际内容与之相符——本轨交付为**链路合同的可执行钉死**（测试 + 规划工件），生产代码零修改（见下）。

### 基线与漂移

- 起点：origin/master `15e5c66b54`（PR #1333 合并点，实时 fetch 实测，未沿用提示词 SHA）。
- 本轨 worktree：`hardening/r4-verify-chain`，与全部并行轨道物理隔离。
- 在途 PR 重叠实测（#1334-#1340）：六个 src 域目录零在途占据；重叠仅在共享文件——`tests/CMakeLists.txt`（4 个 open PR 触碰；本轨仅在尾部追加两个独立注册块，冲突面单行级）与 `tests/test_preflight.cpp`（#1337 触碰；本轨未触碰它，联动测试走独立新文件）。

### 为什么是"钉死合同"而非修码

六个域经 #1285 / #1318 / #1277 / #1279 / #1320 / #1292 / #1324 收口后，本轨的实测（72 头逐行审计 + 全产出点 locale 矩阵 + 失效联动矩阵 + 12 个边界增量案）**未发现可判定的生产缺陷**。按轨道铁律（只修复既有缺陷、拒绝为做而做），交付形态为：

1. **WP-B** `tests/test_verify_chain_locale_matrix.cpp`：13 个 digest 产出点 × {C, POSIX, C.utf8, en_US, zh_CN, de_DE(逗号小数，LOCPATH 自供给)} 双 locale 字节一致矩阵 + 反空洞效力行（证明敌对 locale 真咬到格式化器）。**实证修正了一个前提**：jsoncpp 1.9.8 的写出器与读入器均 locale 无关（snprintf 出 "0,5" 而 writeString 出 "0.3"），grader 走 std::to_chars 规范无关——矩阵把这一事实钉为合同而非假设。199 断言 × 2 轮绿。
2. **WP-D** `tests/test_preflight_authority_invalidation.cpp`：共享权威桥接（一个 PassportResolver 同喂 broker.assets() 与 StateAssetFactsProvider；一张能力表同喂 CapabilityFactsLookup 与 ICapabilityProvider）下的失效联动矩阵：5 粒度 × 消费族 = **20 用例 / 294 断言**。钉死三条真实缝隙语义：invalidateAsset 无条件全量清 bundle 缓存（比直觉粗，是有意语义）；band_role 等 5 族规则在镜像不可读时把故障让渡给 operator_known 单点；refreshRecipes 在 registry=null 时早退不清缓存——但权威状态零变化，同键命中合法（不陈旧）。
3. **WP-C/E/F/A** 四个既有套件各 +3 边界案：空 spec 合成 Fail / 矛盾 check 折叠 / kind 精确匹配；required 证据缺席 Blocked / 全-Indeterminate 与诚实零分机器可区分 / 部分提交 Partial；provider 失败 typed 传播 / 显式 facts 优先零咨询 / 咨询门控；checkpoint Unreadable typed / bounded_io 负例 / 五-kind 生产适配器装配。
4. **WP-G** `.planning/verify-chain-r4/`：BASELINE（实测基线+产出点/接缝清单）、API_AUDIT（72 头逐行处置，零未声明缺口）、ADAPTER_MATRIX（10 检查族 × 5 生产适配器，每格有主）、DECISIONS（7 条链路规则，抽查对照=代码）、EVIDENCE（双跑记录）、REVIEW_LOG（独立对抗审查 + 整改）、run_matrices.sh（独立复跑脚本）。

### 用户可感知行为变化

**无。** 生产代码零修改；diff 全部为测试与 planning 工件（`git diff --stat`：15 文件 +2323 行，其中 0 行 src/）。

### 3.2 下限逐项达成表

| 下限 | 要求 | 实测 |
|---|---|---|
| 失效联动用例 | ≥20 | 20 用例 / 294 断言 / 5 轮绿 |
| locale-stable 矩阵行 | ≥18 | 13 产出点 × 6 locale + 反空氧行（199 断言双跑绿） |
| adapter 覆盖矩阵 | 5×检查族零未声明缺口 | 10×5 全格有主（ADAPTER_MATRIX.md） |
| grader/suitability 对抗 | ≥12 | 既有 47 对抗案 + 本轨新增 6 |
| 两级 outcome 边界 | ≥6 | 本轨 3 + 既有互锁/聚合/不可判案族（报告级另 +3） |
| API 审计表 | 72 行 | 72 行处置非空 |
| 原子提交 | ≥18 | **18**（实测 rev-list 计数） |
| 触碰文件 | ≥16 | **19**（实测 diff --name-only） |
| DECISIONS 链路规则 | ≥5 | 7 条（D1-D7） |

> 备注：18 提交 / 19 文件均含 planning 工件的阶段性提交；每个提交独立可评审，`git log --oneline origin/master..HEAD` 逐条可核对。

### 本地验证（未等待线上 CI）

- Oracle（轻域，含全部字面口径）：`ctest -R "verif|grader|preflight|suitab|science_context|evidence"`（宽口径，D6 修正：字面正则漏 verifier/verification 家族）**连续两轮 100%（236/236），exit 0/0**。
- 矩阵复跑：`run_matrices.sh build-lite 2` → ALL MATRICES GREEN。
- **全栈 Oracle（build/ 完整 Qt/QGIS 栈目录）**：同宽口径 ctest **连续两轮 100%（161/161），exit 0/0**——包含本轨全部新增测试、全部轻域既有套件与 2 个建成重域目标；19 个 `_NOT_BUILT` 占位 = 7 个 master 既有断链目标（见未解决项 1）+ 2 个非本域未建目标，逐项披露于 EVIDENCE §6.1。
- 独立对抗 review：SHIP-WITH-FIXES，2×P1 + 5×P2 + 5×NIT 全部整改（REVIEW_LOG.md 逐条对照），整改后全部套件重跑绿——含整改自身引入的一处 budget 案 finding 数回归（双跑抓到并修复，1f307197ce）。
- 构建栈：gcc-15 专用目录（系统 GCC 16.2.1 会触发 `cmake/raise-compiler-stack.sh`，该脚本 `status=$?` 在 if 语句后捕获、恒 exit 0 —— 假绿陷阱，实测未修复；属 build-infra 域，本轨不越界修，在此披露并建议其所有轨跟进）。

### 未解决项

1. **【已部分收口，见下方补跑记录】** 重域目标中 2 个建成并纳入全栈双跑（test_virtual_raster_preflight、test_teaching_fake_grader_cli）；其余 7 个被 **master 既有断链**挡住（`sicnu::agent` 缺链接 `sicnu_agent_loop` → 6 目标 undefined reference；test_verification_env_12 缺 `#include <QTemporaryDir>`，文件属 #1335）——本分支 src/ 零改动，断链在 master 等价存在，属 build-restore/agent 轨域，白名单外不修、EVIDENCE §6.1 逐项披露。
2. metric seam 无生产 IMetricView：产品空位（事实源在他域），非缺陷，ADAPTER_MATRIX.md 已声明。
3. suitability "冲突/过期事实"判定面：合同无此概念（单 facts 通道、无采集时间戳），按铁律不新增语义；两事实源的真实形态（显式 facts vs provider）已由 precedence 案钉死。
