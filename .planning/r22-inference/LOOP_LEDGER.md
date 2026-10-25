# R22-03 LOOP LEDGER（/goal-loop 账本）

格式：轮次 | 假设/改动 | 文件 | 验证命令 | 真实结果 | 下一步

## 工作包 C0（#1706）

- 轮0 | 启动盘点+验真：#1706 OPEN_UNFIXED（:828 assign 别名 + :780 权重每 slot 累加；调用点 :1839/:1881/:2015），#1554/#1553 FIXED_OPEN（d9f1b886f1 在 master） | BASELINE/ISSUE_TRIAGE | gh api + git 源码实读 | 与 issue 行号逐行吻合；#1673 只做 threshold 门控无重叠 | 建 worktree、写 RED
- 轮1 | RED 测试：5 个 TEST_CASE（probability 独立/labels argmax/滑窗/NoData/uncertainty） | tests/test_r22_inference_feather.cpp, tests/CMakeLists.txt(注册) | 待编译运行（configure 进行中） | pending | 修复实现
- 轮2 | configure build-r22-inference（复制 r21-B 验证参数；首轮缺 GSL → 补 GSL_*；次轮缺 netcdf → 补 NETCDF_*） | build-r22-inference/ | cmake -S . -B …（完整参数见 BASELINE 环境事实） | 第3轮 "Generating done (28.6s)"，exit 0；tests/CMakeLists.txt:16466 有 teaching_admin 库告警（既有环境告警，不阻塞本轨目标） | 锁内 -j4 编译 test_r22_inference_feather 跑 RED
- 轮3 | RED 构建（共享锁 /tmp/exp-rs-r21-build.lock，-j4） | build-r22-inference/ | flock … cmake --build … --target test_r22_inference_feather -j4 | 首次全量：vendor libsqlite3.a non-PIC 链接失败 → 改系统 .so；再链：/usr/lib/libcblas.so.3 64 位后缀符号缺失 → 采用 r21-B link-shim + --allow-shlib-undefined 配方（CROSS_TRACK X1）；成功 | 跑 RED
- 轮4 | RED 运行（未修复树） | 同上 | test_r22_inference_feather | **5 用例全败/39 断言失败，特征精确**：所有波段=0.3333=mean(0.8,0.1,0.1)；uncertainty=0.3131=(1+margin)/4；证据存 .planning/.../RED_run1.txt | 实施修复
- 轮5 | 修复：per-slot m_sum + per-slot m_weight（resetWindow/add/slideUp/三处 finalize）；derived 部分定义栈 fail-closed | tile_inference_engine.cpp | 重建+双跑 | **GREEN×2：1136 断言/5 用例全过**；test_model_runtime_9 17 用例、test_model_runtime 25 用例/110429 断言、test_r14_B_tile_engine 4 用例全绿 | commit b55aa5fd9a
- 轮6 | R1 独立评审（全新 subagent，只读，反对式） | diff b55aa5fd9a | 评审报告 | **APPROVE**（0×P0/P1/P2；4×P3：mask/remap/poison 测试缺口、内存+slots、噪声级 ptr hoist、NoData 用例强度说明；out-of-diff 多 head derived 崩溃疑点） | 疑点证伪（守卫 :1056/:1060 已拒）；P3 测试补强 commit 1381e5d80a（7 用例 1203 断言双跑全过） | R2
- 轮7 | R2 独立评审（另一 subagent，不复用 R1，独立 python 重推全部数学） | b55aa5fd9a + 1381e5d80a | 评审报告 | **APPROVE**（0×P0-P2；P3 注释算术漂移已修 2d5191447e；P3 derived 死分支备忘 X4；确证 runMultiInput 不可达、fail-closed 不改变全覆盖像元、C=1 位相同、per-slot 除法正确性反例） | 最终双跑四套件 ×2 全绿（1203/1226/110429/20695） | 交付
- 轮8 | 交付：TEST_RESULTS/PERF_BASELINE/CROSS_TRACK/FINAL_REPORT/PR_BODY + push 前复查（master 仍 911e0b17；#1673 head 未变；新增 #1708 弱交集 .gitignore 尾部/测试注册，无 src 交集，已记矩阵） | 全部文档 | gh api + gh pr list | 见 PR_COLLISION_MATRIX push 前复查节 | push + gh pr create


