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
