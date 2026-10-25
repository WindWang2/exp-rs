# R22-02 (r22-data) LOOP_LEDGER — 轮次账本

格式：`轮次 | 假设 | 文件 | 测试命令 | 真实结果 | 下一步`

## L1 盘点轮
- 假设：任务快照（master=911e0b17、5 open PR、issue 状态）仍最新。
- 动作：gh api 核 SHA、pr list、issue list、worktree list；建 `agent/r22-data-20261025T132358Z`。
- 结果：master 一致；open PR=5（R21 轨）；6 个目标 issue 全部 OPEN_UNFIXED（源码逐行验真，BASELINE.md 表）。本地 master behind 59（祖先正常）。
- 下一步：规划文档 + 修复。

## L2 修复轮（6 issue → 生产代码）
- 假设：每个 issue 的修复面 = BASELINE 表所列行。
- 文件：offline_gate.{cpp,h}、range_cache{,_disk}.{cpp,h}、atomic_fs.cpp、artifact_object_pool.cpp、sample_promotion.cpp、dataset_store.{cpp,h}、raster_reader.{cpp,h}。
- 结果：全部修改完成（含 #1703-③ 同型 4 处、#1679 DNS 分支统一 + pinning 纵深 + 头文件注释更新）。
- 下一步：RED 证据 + 测试。

## L3 RED 证据轮
- 命令：`git show origin/master:src/geospatial/remote/offline_gate.cpp` → /tmp/r22red 独立编译（g++ -std=c++20，cpl_conv.h 打桩）。
- 结果：master 版 9 形态中 7 判公网（漏判）；修复版 9/9 判私网。其余 issue 以「master 代码路径 × 新断言」逐行矛盾记录于 RED_GREEN.md。
- 下一步：新测试文件 + 注册 + 构建。

## L4 测试轮（进行中）
- 文件：tests/test_r22_data_security_consistency.cpp（7 TEST_CASE）+ test_io_range_cache.cpp 追加 2 段（basis 格式更新 + 几何切换 e2e）+ tests/CMakeLists.txt append-only 注册。
- 命令：`flock /tmp/exp-rs-r21-build.lock + /home/kevin/project/exp-rs-build.lock; cmake --build build-r22-data --target test_r22_data_security_consistency test_io_remote_validator test_io_range_cache -j4`。
- 结果（实时）：configure ✅（164.7s）；编译中——发现缓存缺 qgis_core 对象（R21-B 只建过 io/模型轨目标），sicnu_add_test 链条需全量编 qgis_core；同时段兄弟轨 r22-inference 持锁串行中（锁机制按预期工作，load 16.6/40 核）。
- 下一步：跑新测试（需 PROJ_LIB/LD_LIBRARY_PATH/link-shim 环境，沿 r22-inference 轨公开命令形态）→ 域族 sweep → 双跑。

## 待续轮
- L5 测试执行与修复闭环；L6 R1 独立 review；L7 R2 独立 review；L8 交付（文档+push+PR）。

## L5 测试执行轮
- 命令：`flock 双锁; cmake --build --target <族> -j4` + `PROJ_LIB/LD_LIBRARY_PATH/link-shim QT_QPA_PLATFORM=offscreen`。
- 结果：15 族全绿；关键族双跑（/tmp/r22_pass2.log）；两处测试期望错误（recover 无事可做返回 false；fixture 缺 model_id）修正后 8/8。
- 下一步：R1。

## L6 R1 轮
- 结果：APPROVE + 8 findings；F1（vacuous #1700 集成测试）以 store 层 oracle 重写 + RED_GREEN 纠错；F2 锁折叠；F5 注明；F6 start gate。双跑绿（327/39、87/8）。
- 下一步：R2。

## L7 R2 轮
- 主动前置：等 R2 期间自证 B5 场景（move-aside fallback × 损坏 journal × 孤儿 .bak 唯一副本），发现 R1 轮清扫会删除唯一副本——先修（5fc544a6c6 复活语义）+ 用例。
- R2 报告：APPROVE @ 5fc544a6c6；确认上述为 R1 轮真实引入的 P1（R2-1，已被 5fc544a6c6 关闭）；R2-2 测试强化（block journal + ONLY-COPY 断言）、R2-3 注释、R2-6 措辞——eb24ccb4dd 闭环。
- 最终验证：双跑 pass A/B 全绿（94/9、327/39、232/27、79/14、425-427/18、283/19）。
- 下一步：交付（PR_BODY + push + PR）。

## L8 交付轮
- push 前：git fetch 复核 origin/master 与 open PR（含 #1673）无新交集 → push 分支 → gh pr create。

## L8 交付轮（完成）
- push：第 2 组重试第 1 次成功（TLS 间歇；commits 完整推上）。
- PR：https://github.com/WindWang2/exp-rs/pull/1710（OPEN，head cc9103c931，8 commits，base master@911e0b17）。
- 不自动合并、不等待 CI。worktree 保留供修订。
