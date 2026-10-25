# R22-03 LOOP LEDGER（/goal-loop 账本）

格式：轮次 | 假设/改动 | 文件 | 验证命令 | 真实结果 | 下一步

## 工作包 C0（#1706）

- 轮0 | 启动盘点+验真：#1706 OPEN_UNFIXED（:828 assign 别名 + :780 权重每 slot 累加；调用点 :1839/:1881/:2015），#1554/#1553 FIXED_OPEN（d9f1b886f1 在 master） | BASELINE/ISSUE_TRIAGE | gh api + git 源码实读 | 与 issue 行号逐行吻合；#1673 只做 threshold 门控无重叠 | 建 worktree、写 RED
- 轮1 | RED 测试：5 个 TEST_CASE（probability 独立/labels argmax/滑窗/NoData/uncertainty） | tests/test_r22_inference_feather.cpp, tests/CMakeLists.txt(注册) | 待编译运行（configure 进行中） | pending | 修复实现
- 轮2 | configure build-r22-inference（复制 r21-B 验证参数；首轮缺 GSL → 补 GSL_*；次轮缺 netcdf → 补 NETCDF_*） | build-r22-inference/ | cmake -S . -B …（完整参数见 BASELINE 环境事实） | 第3轮 "Generating done (28.6s)"，exit 0；tests/CMakeLists.txt:16466 有 teaching_admin 库告警（既有环境告警，不阻塞本轨目标） | 锁内 -j4 编译 test_r22_inference_feather 跑 RED
- 轮3 | RED 构建（共享锁 /tmp/exp-rs-r21-build.lock，-j4） | build-r22-inference/ | flock … cmake --build … --target test_r22_inference_feather -j4 | 进行中（首次全量含 qgis_core） | 跑 RED → 修 FeatherAccumulator
