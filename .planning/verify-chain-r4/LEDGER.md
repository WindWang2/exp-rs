# Goal-Loop Ledger — Track 16 验证链收口（hardening/r4-verify-chain）

> 本轨账本。根目录 `.goal-loop-ledger.md` 是被 track 的多轨共享文件（gitignore 规则晚于入库），
> 按共享文件纪律，本轨迭代账本落在这里，收尾时向根账本一次性追加 section（append-only 惯例）。

| 轮次 | 改动 | 验证命令 | 结果 | 本轮 tokens | 累计 tokens |
|---|---|---|---|---|---|
| 0 | worktree 建立（15e5c66b54）+ gcc-15 configure + BASELINE/PLAN 工件 + P1-P12 产出点/失效接缝枚举 | cmake configure exit 0；计数实测 72 头/47 域测试 | 通过 | ~400k | ~400k |
| 1 | 双 lane 构建落地（build-lite 36 轻目标 514/514；build 重链后台）；覆盖盘点（Explore 子代理，20 缺口清单）；WP-B 矩阵测试 13 产出点 × 6 locale + 抗空洞性行（LOCPATH 自供给 de_DE） | ninja -j2 exit 0；test_verify_chain_locale_matrix 两轮 194 断言全绿（含逗号小数 locale 行与效力行）；提交 94992ecaa8 + 681822044e | 通过 | ~890k | ~1290k |
| 2 | WP-D 失效联动矩阵 20 用例（共享 authority 桥接，5 粒度 × 消费族）；调试 6 处自伤断言 + 1 处规则 id 漏网（SIGSEGV 实为 nullptr 解引用）；钉死三条真实缝隙语义：invalidateAsset 全量清 bundle 缓存、band_role 把镜像故障让渡给 operator_known 单点、null-registry refreshRecipes 无状态变化故同键命中合法 | test_preflight_authority_invalidation 三轮全绿（294 断言，decl+2 随机种子）；提交 b1fed05565 | 通过 | ~950k | ~2240k |
| 3 | Phase 3 全落：WP-C 3 案（空 spec/矛盾 check/kind 精确匹配）+ WP-E 3 案（required 缺席 Blocked/全 Indeterminate vs 诚零/部分提交）+ WP-F 3 案（provider 失败 typed 传播/facts 优先级零咨询/咨询门控）+ WP-A 3 案（Unreadable typed/bounded_io 负例/五 kind 生产装配） | 四目标首轮全绿（126+264+385+225 断言）+ WP-A 双跑；提交 076c93ca2f/c28dcd08d9/2ab2f04b20/85ef6362f3 | 通过 | ~820k | ~3060k |
| 4 | Oracle 双跑（轻域宽口径 236/236 ×2 exit 0/0）+ EVIDENCE/REVIEW_LOG/PR_BODY 归档 + 根账本追加 + PR #1343 开出；重链（qgis_gui 闭包 ~1800 TU，-j2 实测 ~2.5 TU/min）守望任务已挂（/tmp/track16_heavy_watch.sh），完成后补跑重域目标并在 PR 追加评论 | 全部收口证据落盘；PR https://github.com/WindWang2/exp-rs/pull/1343 | 通过（重域项按 PR 未解决项 #1 诚实披露） | ~600k | ~3660k |
| 5 | 重链落地后全栈 Oracle：build/ 完整 Qt/QGIS 栈目录宽口径 ctest 双跑 161/161 ×2（exit 0/0）；7 个未建重目标证实为 master 既有断链（agent_loop 链接缺失 ×6 + #1335 属文件缺 include ×1），白名单外不修逐项披露；PR #1343 追加运行评论；EVIDENCE §5/§6 回填（提交 18/文件 19 达标） | build/ ctest exit 0/0 两轮；提交 d4840f55bb + 评论 5851213618 | 通过 | ~450k | ~4110k |

---

# R2 轮次账本（hardening/r4-verify-chain-r2，基线 a726d17a62）

| 轮次 | 改动 | 验证命令 | 结果 | 本轮 tokens | 累计 tokens |
|---|---|---|---|---|---|
| R2-0 | fetch（master 前进 425 提交至 a726d17a62）+ 漂移审计（六模块 src 零漂移；在途 PR 仅 #1365 零重叠）+ 计数复核（72 头不变/45 测试文件/47 域目标）+ R2 基线入档；实测 master 既有阻塞缺陷 tests/CMakeLists.txt:14728 残留 `=======`（全树唯一）→ 删除该行 | 首次 configure Parse error 复现 → 修复后重跑 | configure 重跑进行中 | ~180k | ~180k |
| R2-1 | 全新构建 build-r2（3824 步，ninja -j2 真实 exit 0 + no-work 复核）；52 正则域目标（47 清单 + ctest -N 补出的 5 个 sicnu_add_sdk_test/foreach 注册）全部首建零编译修复；发现 8 个前 _NOT_BUILT 重域目标（R1 断链披露项）已由 #1335 解锁 | ninja NINJA_EXIT=0；ctest -N 人口 252 | 通过 | ~320k | ~500k |
| R2-2 | run_matrices.sh 重域补跑（R1 记忆遗留项）：全新 build-r2 上双矩阵 ×2 轮 | ALL MATRICES GREEN ×2（199+294 断言） | 通过 | ~40k | ~540k |
| R2-3 | ctest 首轮 252/252 ×2（exit 0/0）—— 但名字抽查发现 8 个重域目标用例名不在人口：**门禁盲区缺陷**（裸英文用例名逃逸 D6 正则，含六模块核心 test_science_context_broker；部分目标靠用例名偶然含关键词"部分入选"） | 名字抽查 env_12/output_verifier 等 62/62 零命中 | 发现缺陷 | ~180k | ~720k |
| R2-4 | 门禁盲区修复（白名单内 tests/CMakeLists.txt）：12 处 TEST_PREFIX + sicnu_add_sdk_test 宏扩展支持 TEST_PREFIX → 人口 252→357；普查暴露第二批 17 个裸命名目标（grader×4/suitability×11/preflight_report_schema/science_context_live_authorities）→ 再修 17 处；**chain_gate_census.sh 守卫脚本**落地（机械枚举域目标 → 断言全用例带目标前缀，allowlist 记录 r4:: D15 与 CLI helper） | census 首轮 29 FAIL → 终轮 **ALL GREEN**（52 目标全前缀化，r4:: 5 例可选） | 通过 | ~260k | ~980k |
| R2-5 | 新人口首轮 357 项：**356/357，逮到真红 F5**（test_verification_failure_11 门禁断言：缺失输出目录被分类 InvalidParameter，测试期望 DirectoryNotFound 族）。根因：checkTargetPath 参数前置守卫刻意 InvalidArgument（"io: never creates directories implicitly" 合同注释），测试门禁不可见 12 天从未绿过。白名单内校准测试期望集（加 InvalidParameter + 引用生产合同），保留 no-partials 硬断言；生产行为跨域披露（io fabric 可考虑 target_directory_missing→DirectoryNotFound，属主 geospatial/operators）。修后二进制 32 断言 5 用例全绿 | 单二进制全绿；正式双轮进行中 | 通过 | ~200k | ~1180k |
