# R18-E ORACLES — 验证契约

## Oracle-A 重复性
每项修复: (1) 在未修复代码上先写最小失败测试并运行记录 RED（退出码/断言文本）；(2) 修复后同测试 GREEN×2（连续两次运行）；(3) 无法复现的候选缺陷标 FALSE POSITIVE 并撤测试或改判。RED 证据写入 LOOP_LEDGER 对应行。

## Oracle-B 本地验证
- 构建: `flock /tmp/exp-rs-five-tracks-build.lock resource_guard ... cmake --build build-r18-e --parallel 2`，退出码入 TEST_RESULTS.md。
- 本域测试（每轮相关子集 + 收尾全量本域）: `ctest --test-dir build-r18-e -j1 -R '<regex>' --output-on-failure`，QT_QPA_PLATFORM=offscreen。
- 环境受限（GPU/设备/平台）用例标 NOT_EXECUTED + 原因 + 补偿验证。不因线上 CI 拖延。

## Oracle-C 性能
- 基线机器/命令/配置/输入规模/重复次数/峰值 RSS（/usr/bin/time -v）/时延记录于 PERFORMANCE.md。
- 对照至少三次（热/冷分开），不声明未测比例，不改输出语义换速度。

## Oracle-D 回归契约
- 用户路径不退化；数据不完整/无权限/取消/超时/崩溃重启/并发 → 可辨识状态（Fail/Indeterminate/Cancelled），绝不伪 Success。
- 具体到本轨: 未知 check kind → Indeterminate（禁止 skip）；缺 provider → Indeterminate；批量评分取消 → cancelled 行；篡改证据 → 不提供 Pass 且诊断可追溯。
- 线程安全/错误类型/schema/资源释放有覆盖证据（review 时核）。

## Oracle-E 所有权
- 每轮 `git diff --name-only origin/master...HEAD` 对照 OWNERSHIP.md；共享 CMake 仅最小追加并在 PR 标出。
- 基线变化（新 fetch）时重跑 overlap_scan。

## Oracle-F Review
- R1: 未参与修改的 reviewer 对全 diff 逆向审核，P0/P1/P2/P3 + 文件位置 + 可复现意见；须尝试提出反例/证伪。
- 修复 P0/P1 与可复现 P2 后 R2 由另一未写 patch 的 context 再审；R2 仍有 P0/P1 则继续迭代。
- 未处理 P2/P3 记录原因与影响。

## Oracle-G 交付
- 原子 commits、无 secrets/大数据/构建产物、`git diff --check` = 0、本域测试完整 GREEN、review_pack.py 证据包、push 成功、`gh pr create` 真实 URL。无权限时保留 commit+PR body 并说明真实原因。

## 迭代纪律
- 阻滞 3 轮 → 重新列 3 种独立原因假设；8 轮 → 重新核查前提、缩小问题。
- 禁止忙等、无限重编译、伪造观察。上下文将尽时把状态写入 LOOP_LEDGER 续跑。
