# Goal-Loop Ledger — Track 16 验证链收口（hardening/r4-verify-chain）

> 本轨账本。根目录 `.goal-loop-ledger.md` 是被 track 的多轨共享文件（gitignore 规则晚于入库），
> 按共享文件纪律，本轨迭代账本落在这里，收尾时向根账本一次性追加 section（append-only 惯例）。

| 轮次 | 改动 | 验证命令 | 结果 | 本轮 tokens | 累计 tokens |
|---|---|---|---|---|---|
| 0 | worktree 建立（15e5c66b54）+ gcc-15 configure + BASELINE/PLAN 工件 + P1-P12 产出点/失效接缝枚举 | cmake configure exit 0；计数实测 72 头/47 域测试 | 通过 | ~400k | ~400k |
| 1 | 双 lane 构建落地（build-lite 36 轻目标 514/514；build 重链后台）；覆盖盘点（Explore 子代理，20 缺口清单）；WP-B 矩阵测试 13 产出点 × 6 locale + 抗空洞性行（LOCPATH 自供给 de_DE） | ninja -j2 exit 0；test_verify_chain_locale_matrix 两轮 194 断言全绿（含逗号小数 locale 行与效力行）；提交 94992ecaa8 + 681822044e | 通过 | ~890k | ~1290k |
| 2 | WP-D 失效联动矩阵 20 用例（共享 authority 桥接，5 粒度 × 消费族）；调试 6 处自伤断言 + 1 处规则 id 漏网（SIGSEGV 实为 nullptr 解引用）；钉死三条真实缝隙语义：invalidateAsset 全量清 bundle 缓存、band_role 把镜像故障让渡给 operator_known 单点、null-registry refreshRecipes 无状态变化故同键命中合法 | test_preflight_authority_invalidation 三轮全绿（294 断言，decl+2 随机种子）；提交 b1fed05565 | 通过 | ~950k | ~2240k |
