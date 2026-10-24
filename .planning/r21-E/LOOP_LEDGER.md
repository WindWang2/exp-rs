# R21-E LOOP_LEDGER — goal-loop 记账

| 轮 | 里程碑 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|---|
| 1 | M0 | LIVE 盘点（git/gh/issue/源码） | BASELINE/ISSUE_TRIAGE/PR_COLLISION/OWNERSHIP/RISK 落盘 | ✅ | worktree+配置 |
| 2 | M0 | worktree 创建（BASE=911e0b17 实证=gh API 远程 SHA） | git worktree list | ✅ | CMake 配置 |
| 3 | M0 | CMake 配置尝试1（裸） | exit 1 缺 GDAL | ❌ | 补依赖路径 |
| 4 | M0 | 配置尝试2（+GDAL） | exit 1 缺 PROJ | ❌ | 继续补 |
| 5 | M0 | 配置尝试3（+PROJ/GEOS） | exit 1 缺 GSL | ❌ | 继续补 |
| 6 | M0 | 配置尝试4（+GSL include） | exit 1 缺 netcdf | ❌ | 继续补 |
| 7 | M0 | 配置尝试5（+netcdf） | exit 0 Generating done | ✅ | 构建域内库 |
| 8 | M0 | E0 复测 13 项 + #1601 考古 | 全部实锤（BASELINE 表） | ✅ | W1 RED |
| 9 | M1 | 域内 9 库构建启动（flock+j2） | 进行中 | ⏳ | 写 TASKS/ORACLES；W1 设计定稿 |
| 10 | M1 | W1 实现（composer+agent 胶水 6 挂载点+头字段+cmake） | 待 BASE agent 构建出 RED | ⏳ | RED run |
| 11 | M1 | W2 RED 实锤（BASE: REQUIRE(result.ok) 失败——loader 拒绝 schema 文档化 verifier） | test_r21_e_faultlab_verifier BASE 1 failed | ✅RED | W2 GREEN |
| 12 | M1 | W2 GREEN（loader 接受+runner 折叠+hook 类型）+ 扩展 3 组 runner 语义测试 | 同测试 8 assertions 全过 | ✅ | studio_live 钩子编译验证 |
| 13 | M1 | W3 复测：#1614/#1615/#1616/#1639/#1636 全部已在 master 修复（代码+注释实证）→ 分诊改 FIXED-OPEN | 无需修复 | ✅ | W5 |
| 14 | M1 | W4 弱断言扫描（70 域内测试文件）：CHECK(true)/真空 size/epsilon(1)/空 catch 全零命中 | grep 实证 | ✅ | — |
| 15 | M1 | W5 压力测试编写（100/500/1000，记账=graded+isolated+unavailable 分割，duplicates⊆graded）+ 注册 | 待编译 | ⏳ | 构建+跑 |
| 16 | M1 | W1 编译期 RED 实锤（BASE 头无 verifier 成员 → g++ -fsyntax-only error）；直链 TU 闭包因 vendor GDAL 依赖链（poppler/curl/blosc）止损 | RED_GREEN.md 记录 | ✅RED | 链条行为级 RED |
| 17 | M1 | 构建锁被 r21-D 全量构建持有（4100+ 对象仍增长）；链条脚本（BASE→RED→pop→GREEN）后台排队 | redgreen_chain.log | ⏳ | 等锁/复查 D 进度 |
| 18 | M1 | studio_live 生产钩子 e2e 测试编写+注册（正例 must_fail 被抓→pass；反例未知 lab→fail-closed） | 待编译 | ⏳ | 随链 |
| 19 | M1 | 回归 sweep 脚本（46 目标 ×2 轮）+ PR_BODY 草稿 + TEST_RESULTS 骨架就绪 | — | ✅ | 链后执行 |
| 20 | M6 | R1 双审（fresh subagent ×2，56/27 调用） | R1a: P0=stash 假象；P1 测试 rulesDir（SICNU_SOURCE_DIR 私有定义）→已修；P2 file-kind/crs 映射实际已处理；P3 kernel-walk artifact 漏挂（确认）→postfix 脚本。R1b: P1 runner/loader declared 不对称→已修；P2 vacuous rules_ref→已修；P2 死 declared()→已删；P2 传递 GUI 链接→CMake 注释诚实化+CT 记录；P3 早退可见性→已修；P3 16x16 注释→已修；P3 scale/offset→已修 | 🔧 | 等链跑完执行 postfix |
| 21 | M6 | R1 修复落地+自证：vacuous 规则作用域 bug（写在 isMember 块内→删除成员永不触发）→移到块外；runner declared 谓词与 loader 对齐（非对象 fail-closed）；早退可见性；helper 期望方向修正（delta_ge→delta_le，canonical 对齐）；Qt6::Core 链入测试目标 | test_r21_e_faultlab_verifier: All tests passed (58 assertions / 9 cases) | ✅ | 等链 |
| 22 | M6 | R2 评审（fresh subagent，78 调用，自行跑测试）：READY-WITH-FIXES；P0-1/P1-1/P1-2/P2-1..4/P3-1..4 全部处置 | 四 lane GREEN×2 + sweep 28 全绿 + e2e PASS | ✅ | 提交+PR |
| 23 | M7 | 提交与 PR 准备 | — | ⏳ | — |
| 24 | M7 | PR #1674 建成（11 提交/42 文件/+2534−24）；分支经 Git Database API 发布（git 传输层不可达），远端树与本地 HEAD 逐字节一致；不自动合并、不等 CI | gh pr view: OPEN | ✅ | 终报完成 |
