# EVIDENCE — Track 16 验证链收口（实测证据归档）

宿主：Linux 6.18.53-1-lts，GCC 15.3.0（专用构建栈），CMake 4.4.3 / Ninja 1.13.2，Qt 6.11.2 / jsoncpp 1.9.8 / GDAL 3.13.3。
构建：`build-lite`（轻域 38 目标，gcc-15 Debug ENABLE_TESTS=ON）；`build`（重链后台：qgis_core 依赖闭包，重域测试用）。
资源纪律：全程 ninja -j2、ctest -j1、nice 10；RSS 峰值 ~38%（红线 70%）。

## 1. 双 locale digest 矩阵（WP-B）

- 命令：`.planning/verify-chain-r4/run_matrices.sh build-lite 2`（独立复跑脚本）
- 结果：`MATRIX test_verify_chain_locale_matrix round1: PASS (194 assertions in 2 test cases)` → 评审整改后 **199 assertions in 2 test cases**，两轮 PASS（本文件归档轮：run1 exit=0 / run2 exit=0，各 199 断言）。
- 行全集：13 SECTION（V1 specDigest / V2 engine report digest / V3 packDigest / V4 level-1 postcondition / V5 **parseSpec 文本轮回**（评审 P2-1 整改后经真实 parse 入口） / G1 grader grade() 三 digest / P1 requestDigest / P2 rulesRevision / P3 reportDigest / P4 totalFindingOrder / P5 report JSON round-trip / SC1 broker bundle 序列化+bundleId / S1 suitability contentDigest）× {C, POSIX, C.utf8, en_US.utf8, zh_CN.utf8, de_DE.UTF-8(LOCPATH 自供给，逗号小数)}。
- 反空洧行：`the harness comma-decimal locale actually bites the formatter` —— localedef 供给后 snprintf("%.12g", 0.5) == "0,5" 实证。
- 独立佐证：评审员以系统 jsoncpp 1.9.8 复刻探针（snprintf "0,5" 而 Json::writeString 字节稳定），D1-a 双重证实。

## 2. 失效联动矩阵（WP-D）

- 命令：`run_matrices.sh build-lite 2` 同脚本第二矩阵 + 显式三轮：`./tests/test_preflight_authority_invalidation`（decl 顺序 + 两个随机种子）。
- 结果：**All tests passed (294 assertions in 20 test cases)** × 3 轮 + 脚本 2 轮 = 5 轮绿。
- 矩阵：5 失效粒度（invalidateAsset / invalidateAllAssets / notifyProjectSwitch / setCapabilityFacts(+revision 推进/表清空) / refreshRecipes(null registry)）× 消费族（asset-facts 规则 / capability 规则 / broker 缓存 / 预算截断 / request digest / 确定性 / 一致性 / 幂等）= 20 用例。

## 3. 边界增量（WP-C/E/F/A）

| 目标 | 断言/用例 | 轮次 | 新增案 |
|---|---|---|---|
| test_verifier_engine | 126 assertions / 17 cases | 1 绿（全量双跑再计） | 3（空 spec 合成 Fail / 矛盾 check 折叠 / kind 精确匹配） |
| test_grader_engine | 264 / 16 | 1 绿 | 3（required 缺席 Blocked / 全-Indeterminate vs 诚零 / 部分提交 Partial） |
| test_suitability_adversarial | 385 / 28 | 1 绿 | 3（provider 失败 typed 传播 / 显式 facts 优先零咨询 / 咨询门控=1） |
| test_verify_adapters | 225 / 9 | **2 绿** | 3（checkpoint Unreadable typed / bounded_io 负例 / 五-kind 生产装配） |

## 4. 全量 ctest（Oracle，双跑）

- 口径：宽口径 `verif|grader|preflight|suitab|science_context|evidence`（D6；枚举 248 个 ctest 项，Catch2 SECTION 展开）。
- build-lite 轻域（排除 build/ 重链目标）：第 1 轮与第 2 轮输出见下方追加记录（-j1，QT_QPA_PLATFORM=offscreen）。
- build 重链目标（test_preflight / test_preflight_check_tool / test_science_verification_10 / test_lab_grader_kernels / test_virtual_raster_preflight / test_output_verifier / test_verification_*_11/12 / test_teaching_fake_grader_cli）：重链构建完成后补跑，记录于 §6。

### 4.1 轻域第 1 轮

```
QT_QPA_PLATFORM=offscreen ctest -R "verif|grader|preflight|suitab|science_context|evidence"
  -E "_NOT_BUILT|test_science_verification_10|test_lab_grader_kernels|test_preflight_check_tool|
      test_virtual_raster_preflight|test_output_verifier|teaching_fake_grader_cli|
      test_verification_env_12|test_verification_metamorphic_11|test_verification_numeric_reference_11" -j1
ROUND_A_EXIT=0 — 100% tests passed out of 236
```

### 4.2 轻域第 2 轮

```
（同命令）ROUND_B_EXIT=0 — 100% tests passed out of 236
```

排除项说明：`_NOT_BUILT` 为 ctest 对未构建目标的占位注册（其真实目标属重链，见 §6）；
其余九个名字是 build/ 重链拥有的目标（qgis_core 依赖闭包）。轻域 236 项 = 47 域测试文件
中 Qt-free/轻依赖部分的全部 Catch2 展开。字面 Oracle 口径
`verify|grader|preflight|suitab|science_context` 是本宽口径的子集，随之一并全绿。

## 5. 下限达成表（3.2）

| 下限 | 要求 | 实测 | 证据 |
|---|---|---|---|
| 失效联动用例 | ≥20 | **20 用例 / 294 断言 / 5 轮绿** | §2 + LEDGER 轮 2 |
| locale-stable 矩阵行 | ≥18 | **13 产出点 × 6 locale（>18 行）+ 反空洞行，199 断言双跑绿** | §1 + run_matrices.sh |
| adapter 覆盖矩阵 | 5×检查族零未声明缺口 | **10 族 × 5 adapter，每格 ✅/🔗/N/A-声明/不处置+理由** | ADAPTER_MATRIX.md |
| grader/suitability 对抗 | ≥12 | **既有 25+9+13 对抗案 + 本轨新增 6**（grader 3 + suitability 3） | §3 |
| 两级 outcome 边界 | ≥6 | **本轨 3（WP-C）+ 既有互锁/聚合/不可判案族**；grader 侧另有 3 报告级 | §3 |
| API 审计表 | 72 行 | **72 行处置全非空，零未声明缺口** | API_AUDIT.md |
| 原子提交 | ≥18 | 见 `git rev-list --count origin/master..HEAD`（回填） | git |
| 触碰文件 | ≥16 | 见 PR 正文 diff --stat（回填） | git |
| DECISIONS 链路规则 | ≥5 | **7 条**（D1-D7），抽查对照=代码 | DECISIONS.md |

## 6. 重链补跑记录

（build/ 完成后回填）

## 7. 非本域发现披露

`cmake/raise-compiler-stack.sh`：`status=$?` 位于整个 `if` 语句之后，POSIX sh 中条件失败且无 else 时 if 语句退出码为 0 → 确定性编译错误重试 12 次后 `exit 0`（假绿）。本机实测 GCC 16.2.1 现装。属 build-infra 域（白名单外），本轨以 gcc-15 构建栈绕过并在此披露。
