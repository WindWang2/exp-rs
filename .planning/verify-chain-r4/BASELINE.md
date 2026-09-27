# BASELINE — Track 16: 验证链收口（R4 Deep Edition）

实测时间：2026-09-27（本机 Linux，gcc-15.3.0 备选 / gcc-16.2.1 系统）

## 1. 实测基线

- `origin/master` 实测 SHA：**`15e5c66b54`**（Merge PR #1333 exp-capsule-debugger-study-r3）。与提示词写作值一致，未漂移。
- 本仓主 worktree（master）落后 origin/master **159** 提交、领先 0（主 worktree 属其它轨道，本轨道不触碰）。
- 本轨道 worktree：`/home/kevin/projects/rs-studio/exp-rs-verify-chain-r4`，分支 `hardening/r4-verify-chain`，起点 `15e5c66b54`。
- 构建配置：`build/`，Ninja，`-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/bin/gcc-15 -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 -DENABLE_TESTS=ON`。
  - **为何 gcc-15**：GCC 16.2.1 会触发 `cmake/raise-compiler-stack.sh` 作为 COMPILER_LAUNCHER；该脚本 `status=$?` 在整个 `if` 语句之后捕获，POSIX sh 中条件失败且无 else 时 if 语句退出码为 0 → 确定性编译错误重试 12 次后 `exit 0`（假绿陷阱，见 memory exp-rs-build-launcher-trap）。实测脚本未修复。gcc-15 不满足 `VERSION_GREATER_EQUAL 16`，launcher 不挂载，编译失败正常浮现。
  - r2 轨道（PR #1293）先例：GCC 16.2.1 在 Debug `-g` 下对 qgis_core/agent/experiment 多个 TU ICE；gcc-15 是已验证的构建栈。

## 2. 在途 PR 盘点与 file-overlap map（2026-09-27 实测，7 个 open）

| PR | 分支 | 文件数 | 与本域白名单重叠 |
|---|---|---|---|
| #1340 | hardening/r4-operator-oracles | 24 | `tests/CMakeLists.txt`；其新文件（test_operator_*、support/r4_operator_fixtures.h）不触碰 |
| #1339 | hardening/r4-i18n-help | 38 | `tests/CMakeLists.txt` |
| #1338 | hardening/closure-io-processing-r4 | 29 | 无（io/processing/raster 域） |
| #1337 | hardening/closure-workflow-contracts-r4 | 25 | `tests/test_preflight.cpp`（该 PR 改它；**本轨道不触碰 test_preflight.cpp**，新建独立文件） |
| #1336 | hardening/closure-ui-runtime-r4 | 13 | 无（UI/help 域） |
| #1335 | fix/review-p0-build-restore | 19 | `tests/CMakeLists.txt`；`test_verification_env_12.cpp`（其文件，本轨道不触碰） |
| #1334 | fix/review-p1-security | 30 | `tests/CMakeLists.txt` |

**结论**：
- `src/verify`、`src/verify_adapters`、`src/grader`、`src/preflight`、`src/suitability`、`src/science_context` 六目录 **零在途 PR 占据** —— 源码域完全干净。
- `tests/CMakeLists.txt` 被 4 个 open PR 触碰 → 本轨道的新测试注册行收敛到**每个新测试目标一个紧凑块**，并计划在上述 PR 合并后 rebase；冲突面单行级。
- 避开文件：`tests/test_preflight.cpp`（#1337）、`tests/test_verification_env_12.cpp`（#1335）、`tests/support/r4_operator_fixtures.h` 与 `tests/test_operator_*.cpp`（#1340）。
- #1335 描述将 preflight 列入"科学/逻辑断言（约 45 个既有失败）"族——以其基线为准；**本轨道以 origin/master 15e5c66b54 的本地实测为准**（Phase 1 首轮 ctest 实测红集），域内既有红测属收口范围，域外红测记录不处置。

## 3. 评审材料

- `PROJECT_REVIEW_DOSSIER_5.0.md` / `AUDIT_DOSSIER_ISSUES_747_760.md` / `PR_TRIAGE_REPORT_2026-09-16.md` / `docs/PARALLEL_TRACKS_10.md`：**均不存在于当前 master**（按提示词预案，以 git 历史与 PR 描述为准）。
- 已通读 open PR 描述中与本域相关的部分：#1335 的既有失败分类（preflight 在列）、#1336 的未解决项（簇 A/B/E 未收口，均非本域）。
- 已并入的本域 PR（无在途占据）：#1285 verifier engine、#1318 production adapters + locale-stable digests、#1277 grader、#1279/#1320 preflight fail-closed、#1292/#1324 science_context live authorities + invalidation、#1330 r3-preflight-live-integration。

## 4. 开放 issue

实测 `gh issue list --state open` = **0**。与写作口径一致。

## 5. 本轨道边界声明（白名单）

- 允许：`src/verify/`、`src/verify_adapters/`、`src/grader/`、`src/preflight/`、`src/suitability/`、`src/science_context/`、`tests/` 对应文件、`.planning/verify-chain-r4/`。
- 明确不做：新增功能方向/工作台/产品模块/算子/实验/检查族；白名单外目录；等待线上 CI；触碰他轨在途文件（§2 清单）。
- `tests/CMakeLists.txt` 属共享文件：仅追加新测试注册块，不改他人行；rebase 计划见 §2。

## 6. 计数锚定复核（实测回填 3.1）

| 锚点 | 写作值 | 实测（15e5c66b54） | 复核命令 |
|---|---|---|---|
| 六模块头文件 | 72（11/5/8/12/21/15） | **72 = 11+5+8+12+21+15** ✅ 逐模块与写作值完全一致 | `ls src/{d}/*.h \| wc -l` |
| 对应测试文件 | 42-43 | 窄正则 `verify\|grader\|preflight\|suitab\|science\|context` = **42**；域真集 `verif\|grader\|preflight\|suitab\|science_context\|evidence_seam` = **47**。**钉死口径：47**（窄口径漏掉 test_verifier_* 家族 8 文件 + test_verification_*_11/12 4 文件 + test_output_verifier + test_evidence_seam；研究员口径 43 与两机正则差异源于此） | `ls tests/*.cpp \| grep -cE ...` |
| production adapters | 5 | 5（provenance_sidecar_view / gdal_grid_probe / fs_artifact_probe / checkpoint_state_view / bounded_io）✅ | `ls src/verify_adapters/*.h` |
| locale 钉定点 | ClassicNumericLocale 1 类 | verify_locale.h:25 class，per-thread scoped ✅ | rg |
| 自包含 SHA-256 | 2 套 | verify_sha256.h + grader_sha256.h；**复测补充第三套：preflight/sha256.h**（engine 自带 sha256Hex/shortDigest） | rg |
| preflight 确定性锚 | 4 | computeRequestDigest / computeRulesRevision / totalFindingOrder / 单点 ack ✅（engine.h:99-107 + ack 引擎单点裁决） | rg |
| 失效接缝 | 3 + setCapabilityFacts | broker.h:57,72-74 四接缝 ✅（实现 broker.cpp:70-112，全部清 cache） | rg |
| grader 纯函数入口 | 1 | grade(rubric, evidence) → GradeOutcome ✅ | rg |
| suitability 上限 | kMaxScenes=1000 | ✅ | rg |
| 词表-引擎互锁 | hasEvaluator | ✅ verify_engine.h:36 | rg |
| 检查族词表 | （补充锚） | **kCheckKinds = 10 族**（state.invariant / artifact.exists / artifact.type / artifact.grid / artifact.schema / metric.range / relational.consistency / provenance.complete / reproducibility.digest / cross.output.consistency） | verify_types.cpp:21 |
| preflight 规则族 | （补充锚） | **10 族**（BandRole/PairCrs/ResolutionRatio/RadiometricState/Modality/QualityMask/Temporal/Leakage/ModelCompat/OperatorKnown） | rules.cpp:1008-1056 |

## 7. 摘要产出点清单（WP-B 矩阵行全集，机械枚举）

`rg -n 'ClassicNumericLocale|sha256Hex|hexDigest|canonicalJsonText' src/verify src/grader src/preflight` 全集（文件:行，语义）：

| # | 产出点 | 位置 | canonical 写出器 | locale 钉守现状 |
|---|---|---|---|---|
| P1 | spec digest（specDigest） | verify_types.cpp:807-808 | canonicalJsonText（内含 pin :663） | ✅ 已钉 |
| P2 | context/spec 投影 digest | verify_types.cpp:925-928 | canonicalJsonText | ✅ 已钉 |
| P3 | parseSpec 入口 | verify_types.cpp:775 | pin 于解析 | ✅ 已钉 |
| P4 | pack digest（packDigest） | verify_pack.cpp:169-170 | canonicalJsonText | ✅ 已钉（parse 另有 pin :142） |
| P5 | levels digest | verify_levels.cpp:88-89 | canonicalJsonText | ✅ 传递已钉 |
| P6 | grader score/report canonical→digest | grader_engine.cpp:44-49 digestOf | canonicalizeJson = **std::to_chars**（规范上 locale 无关，grader_json.cpp:139 有显式论证） | ✅ 按设计无关（矩阵行仍双跑验证） |
| P7 | grader evidence body digest | grader_types.cpp:1500 | 同上 | ✅ 同上 |
| P8 | grader evidence computed digest | grader_types.cpp:1772 | 同上 | ✅ 同上 |
| P9 | **preflight request_digest** | preflight/engine.cpp:96-105 | **Json::writeString（snprintf %.*g）无 pin** | ❌ **红格候选 R1** |
| P10 | **preflight finding canonical（total-order tie-break）** | preflight/engine.cpp:42-51 | **同上无 pin** | ❌ **红格候选 R2** |
| P11 | **preflight report digest** | preflight/report.cpp:178-192 | **同上无 pin** | ❌ **红格候选 R3** |
| P12 | preflight shortDigest 原语 | preflight/sha256.cpp:60,94 | 字节级 sha256（输入文本已由上列决定） | 随上游 |

豁免候选（登记理由见 DECISIONS.md）：science_context broker.cpp:44-66 `assetDigestOf`（Json::writeString + fnv1a64，cache-key 用途；locale 漂移方向只会造成缓存分裂 miss，永不造成跨 locale 碰撞/陈旧命中 —— 安全方向漂移）。

## 8. 失效接缝与消费点清单（WP-D 列全集）

| 接缝 | 实现 | 语义 |
|---|---|---|
| setCapabilityFacts | broker.cpp:70-76 | 挂 authority + mCache.clear()（authority reinstall 后旧 bundle 不存活） |
| refreshRecipes | broker.cpp:84-92 | registry 不可用 → false + router 留空（fail-closed）+ cache clear |
| invalidateAsset | broker.cpp:94-100 | mAssets.invalidate(key) + mCache.clear() |
| invalidateAllAssets | broker.cpp:102-106 | 全失效 + cache clear |
| notifyProjectSwitch | broker.cpp:108-112 | 同 invalidateAllAssets 语义 |
| capability revision | capability_facts.h:56 `revision()` | 每次 synthesize 读一次，进 cache key（reload/reinstall 推进 revision → 缓存失效） |
| preflight 消费侧 | IAssetFactsProvider / ICapabilityProvider（provider.h:43-60） | 引擎无内部缓存，陈旧风险在权威侧投影（capability_mirror/StateAssetFactsProvider） |
| science_context→preflight 桥 | preflight/asset_state_adapter.h StateAssetFactsProvider | 同一 resolver 形状可共享权威（文档明示 "deployments can share one authority"） |

失效粒度 × 规则族矩阵：4 粒度（asset 级 / 全局 / 项目切换 / registry 不可用+authority 替换）× 10 规则族中的 facts/capability 消费族（Temporal、RadiometricState、Modality、BandRole、PairCrs、OperatorKnown 等）+ budget 边界 ≥ 20 用例。

## 9. ctest 口径说明（Oracle 修正记录）

提示词 Oracle 命令 `ctest -R "verify|grader|preflight|suitab|science_context"` 在 ctest 名上是**下界口径**：`test_verifier_*`（"verifier" 不含 "verify" 子串）与 `test_verification_*_11/12` 家族不被匹配。本轨道执行**更宽口径** `ctest -R "verif|grader|preflight|suitab|science_context|evidence"`（47 文件全集的超集），两轮全绿即同时覆盖字面 Oracle。两个口径的输出均入 EVIDENCE.md。
